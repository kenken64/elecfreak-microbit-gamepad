/*
 * micro:bit V2 + ELECFREAKS Joystick:bit V1  ->  Bluetooth LE gamepad for Windows 11
 *
 * Pair:   Settings > Bluetooth & devices > Add device > Bluetooth >
 *         "micro:bit Gamepad"
 * Test:   Win+R  ->  joy.cpl  ->  micro:bit Gamepad  ->  Properties
 * Forget: hold A + B while powering on / resetting (wipes all bonds)
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include "gamepad.h"
#include "hog.h"
#include "input.h"
#include "ui.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

/* Name goes in the scan response (Windows scans actively) to keep ad[] < 31 bytes */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct bt_conn *active_conn;
static K_MUTEX_DEFINE(conn_lock);

/* ------------------------------------------------------------------------ */
/* Advertising                                                               */
/* ------------------------------------------------------------------------ */

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err == -EALREADY) {
		return;
	}
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
}

static K_WORK_DEFINE(adv_work, adv_work_handler);

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

/* ------------------------------------------------------------------------ */
/* Connection callbacks                                                      */
/* ------------------------------------------------------------------------ */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_WRN("Connection to %s failed (0x%02x %s)", addr, err, bt_hci_err_to_str(err));
		return;
	}

	LOG_INF("Connected %s", addr);

	k_mutex_lock(&conn_lock, K_FOREVER);
	if (!active_conn) {
		active_conn = bt_conn_ref(conn);
	}
	k_mutex_unlock(&conn_lock);

	/* Ask the host to encrypt (or pair, the first time) right away */
	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_WRN("Failed to request security (err %d)", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected %s (0x%02x %s)", addr, reason, bt_hci_err_to_str(reason));

	k_mutex_lock(&conn_lock, K_FOREVER);
	if (active_conn == conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	k_mutex_unlock(&conn_lock);
}

/* The connection object is free again -> we can advertise again */
static void recycled(void)
{
	advertising_start();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	if (err) {
		LOG_WRN("Security failed: level %u, err %d %s", level, err,
			bt_security_err_to_str(err));
		LOG_WRN("If the PC was paired before, remove \"%s\" in Windows "
			"Bluetooth settings and pair again", CONFIG_BT_DEVICE_NAME);
		return;
	}

	LOG_INF("Link secured (level %u)", level);
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	LOG_INF("Conn params: interval %u.%02u ms, latency %u, timeout %u ms",
		interval * 125 / 100, (interval * 125) % 100, latency, timeout * 10);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.security_changed = security_changed,
	.le_param_updated = le_param_updated,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete (%s)", bonded ? "bonded" : "not bonded");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_WRN("Pairing failed: %d %s", reason, bt_security_err_to_str(reason));
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static struct bt_conn *get_active_conn(void)
{
	struct bt_conn *conn = NULL;

	k_mutex_lock(&conn_lock, K_FOREVER);
	if (active_conn) {
		conn = bt_conn_ref(active_conn);
	}
	k_mutex_unlock(&conn_lock);

	return conn;
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

int main(void)
{
	struct gamepad_report report = {0};
	struct gamepad_report last_sent = {0};
	bool host_synced = false;
	int err;

	LOG_INF("micro:bit Joystick:bit gamepad starting");

	ui_init();

	err = input_init();
	if (err) {
		LOG_ERR("Input init failed (err %d)", err);
	}

	bt_conn_auth_info_cb_register(&auth_info_cb);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	/* Restore bonds (and their notification subscriptions) from flash */
	settings_load();

	if (input_a_and_b_held()) {
		LOG_WRN("A+B held at boot: deleting all pairings");
		bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
		ui_show_bonds_cleared();
		while (input_a_and_b_held()) {
			k_msleep(50);
		}
		k_msleep(500);
	}

	advertising_start();

	for (;;) {
		struct bt_conn *conn;
		enum ui_link link = UI_ADVERTISING;

		input_read(&report);
		hog_set_report(&report);

		conn = get_active_conn();
		if (conn) {
			if (hog_is_ready(conn)) {
				link = UI_READY;

				if (!host_synced || memcmp(&report, &last_sent, sizeof(report)) != 0) {
					err = hog_send(conn, &report);
					if (err == 0) {
						last_sent = report;
						host_synced = true;
					}
					/* on error (e.g. -ENOMEM) retry next cycle */
				}
			} else {
				link = UI_PAIRING;
				host_synced = false;
			}
			bt_conn_unref(conn);
		} else {
			host_synced = false;
		}

		ui_update(link, &report);
		k_msleep(GP_POLL_INTERVAL_MS);
	}

	return 0;
}

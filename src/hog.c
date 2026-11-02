/*
 * HID over GATT (HOGP) gamepad service.
 *
 * Windows 11 loads its built-in HID-over-GATT driver for this service and the
 * device shows up as a standard game controller ("Set up USB game controllers"
 * / joy.cpl, DirectInput, SDL, Steam, browsers' Gamepad API ...).
 *
 * Every HID characteristic requires an encrypted link, which makes Windows
 * pair (Just Works) and bond on first connection.
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

#include "hog.h"

enum {
	HIDS_REMOTE_WAKE = BIT(0),
	HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

enum {
	HIDS_INPUT = 0x01,
	HIDS_OUTPUT = 0x02,
	HIDS_FEATURE = 0x03,
};

struct hids_info {
	uint16_t bcd_hid; /* HID spec version, little endian */
	uint8_t country_code;
	uint8_t flags;
} __packed;

struct hids_report_ref {
	uint8_t id;
	uint8_t type;
} __packed;

static const struct hids_info info = {
	.bcd_hid = sys_cpu_to_le16(0x0111), /* HID 1.11 */
	.country_code = 0x00,
	.flags = HIDS_NORMALLY_CONNECTABLE,
};

static const struct hids_report_ref input_ref = {
	.id = GP_REPORT_ID,
	.type = HIDS_INPUT,
};

/* 8 buttons + X/Y axes, see gamepad.h */
static const uint8_t report_map[] = GP_REPORT_MAP_INIT;

BUILD_ASSERT(sizeof(struct gamepad_report) == 3, "report layout must match report_map");

static struct gamepad_report current_report;
static struct k_spinlock report_lock;
static uint8_t ctrl_point;

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &info, sizeof(info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	struct gamepad_report snapshot;
	k_spinlock_key_t key = k_spin_lock(&report_lock);

	snapshot = current_report;
	k_spin_unlock(&report_lock, key);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &snapshot, sizeof(snapshot));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &input_ref, sizeof(input_ref));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
	/* Subscription state is checked per connection in hog_is_ready() */
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	/* 0 = Suspend, 1 = Exit Suspend. Nothing to do for this device. */
	if (offset != 0 || len != sizeof(ctrl_point)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	ctrl_point = *(const uint8_t *)buf;
	return len;
}

#define PERM_R  BT_GATT_PERM_READ_ENCRYPT
#define PERM_W  BT_GATT_PERM_WRITE_ENCRYPT

/*
 * Attribute indexes:
 *  0 Primary Service         5 Report decl          8 Report Reference
 *  1/2 HID Information       6 Report value         9/10 HID Control Point
 *  3/4 Report Map            7 Report CCC
 */
BT_GATT_SERVICE_DEFINE(hog_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
			       PERM_R, read_info, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
			       PERM_R, read_report_map, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       PERM_R, read_input_report, NULL, NULL),
	BT_GATT_CCC(input_ccc_changed, PERM_R | PERM_W),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, PERM_R,
			   read_report_ref, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       PERM_W, NULL, write_ctrl_point, NULL),
);

#define INPUT_REPORT_ATTR (&hog_svc.attrs[5])

bool hog_is_ready(struct bt_conn *conn)
{
	return bt_conn_get_security(conn) >= BT_SECURITY_L2 &&
	       bt_gatt_is_subscribed(conn, INPUT_REPORT_ATTR, BT_GATT_CCC_NOTIFY);
}

void hog_set_report(const struct gamepad_report *report)
{
	k_spinlock_key_t key = k_spin_lock(&report_lock);

	current_report = *report;
	k_spin_unlock(&report_lock, key);
}

int hog_send(struct bt_conn *conn, const struct gamepad_report *report)
{
	return bt_gatt_notify(conn, INPUT_REPORT_ATTR, report, sizeof(*report));
}

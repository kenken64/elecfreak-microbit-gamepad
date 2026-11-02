/*
 * HID-over-GATT gamepad on the Nordic S130 SoftDevice (micro:bit V1, nRF51822).
 *
 * What Windows 11 needs, and how it is handled here:
 *  - HID service (0x1812) + Battery (0x180F) + Device Information/PnP ID (0x180A)
 *  - every HID attribute requires an encrypted link -> Windows pairs
 *    ("Just Works", LE legacy pairing) and bonds on first use
 *  - bonds (our LTK + the PC's identity address) are kept in the last flash
 *    page, up to 4 PCs, so Windows can reconnect after a power cycle
 *  - the PC's notification subscription (CCCD "system attributes") is saved
 *    per bond and restored on reconnect; Windows does not re-subscribe by
 *    itself, so without this the pad would connect but send nothing
 *  - a PC that "Removed" the device may simply pair again: its old bond is
 *    replaced
 */
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ble.h>
#include <ble_gap.h>
#include <ble_gatts.h>
#include <ble_hci.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>

#include "v1_ble.h"

#define DEFAULT_DEVICE_NAME "micro:bit Gamepad"
#define MAX_NAME_LEN        29 /* what fits in the 31-byte scan response */

static char device_name[MAX_NAME_LEN + 1] = DEFAULT_DEVICE_NAME;

/* Preferred connection: 7.5 - 15 ms interval, no latency, 4 s timeout */
#define CONN_INT_MIN 6   /* 1.25 ms units */
#define CONN_INT_MAX 12
#define CONN_TIMEOUT 400 /* 10 ms units */

#define ADV_INTERVAL 64 /* 40 ms in 0.625 ms units */

/* ------------------------------------------------------------------------ */
/* Logging (USB serial, 115200 baud)                                         */
/* ------------------------------------------------------------------------ */

static void ble_log(const char *fmt, ...)
{
	char buf[96];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	Serial.println(buf);
}

/* ------------------------------------------------------------------------ */
/* GATT values                                                               */
/* ------------------------------------------------------------------------ */

static const uint8_t report_map[] = GP_REPORT_MAP_INIT;

/* bcdHID 1.11, country 0, flags: normally connectable */
static const uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x02};

/* Report Reference: Report ID, type 1 = Input */
static const uint8_t report_ref[] = {GP_REPORT_ID, 0x01};

/* PnP ID: source USB-IF, VID 0x1209 / PID 0x0001 (pid.codes test PID), ver 1.00 */
static const uint8_t pnp_id[] = {0x02, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01};

static const uint8_t battery_level = 100;
static const uint8_t zero_report[sizeof(struct gamepad_report)] = {0};
static const uint8_t zero_byte = 0;

static ble_gatts_char_handles_t report_h;
static ble_gatts_char_handles_t bas_h;
static ble_gatts_char_handles_t supply_h;

/* 8c680001-c4a1-445d-b9ed-14f725715c89: supply voltage service.
 * Characteristic 8c680002-... contains uint16 millivolts, little-endian.
 * Zero means no successful sample yet. */
static const ble_uuid128_t supply_uuid_base = {{
	0x89, 0x5c, 0x71, 0x25, 0xf7, 0x14, 0xed, 0xb9,
	0x5d, 0x44, 0xa1, 0xc4, 0x00, 0x00, 0x68, 0x8c,
}};
static const uint8_t supply_initial[2] = {0, 0};

/* Advertising: flags, appearance (gamepad), 16-bit UUIDs HID + Battery */
static const uint8_t adv_data[] = {
	0x02, 0x01, 0x06,
	0x03, 0x19, (BLE_APPEARANCE_HID_GAMEPAD & 0xFF), (BLE_APPEARANCE_HID_GAMEPAD >> 8),
	0x05, 0x03, 0x12, 0x18, 0x0F, 0x18,
};
/* Scan response: complete local name (Windows scans actively) */
static uint8_t scan_rsp[31];
static uint8_t scan_rsp_len;

/* ------------------------------------------------------------------------ */
/* Bond storage (last flash page)                                            */
/* ------------------------------------------------------------------------ */

#define MAX_BONDS    4
#define SYS_ATTR_MAX 64
#define STORE_MAGIC  0x31425047UL /* "GPB1" */

struct bond {
	uint32_t seq; /* 0 or 0xFFFFFFFF = free slot; higher = newer */
	ble_gap_enc_key_t own_enc;
	ble_gap_addr_t peer;
	uint16_t sys_attr_len;
	uint8_t sys_attr[SYS_ATTR_MAX];
};

struct store_data {
	uint32_t magic;
	struct bond bonds[MAX_BONDS];
};

#define STORE_WORDS ((sizeof(struct store_data) + 3) / 4)

union bond_store {
	struct store_data d;
	uint32_t w[STORE_WORDS];
};

static_assert(STORE_WORDS <= 256, "bond store must fit one 1 KB flash page");

static union bond_store store;     /* working copy */
static union bond_store flash_buf; /* snapshot being written */

enum flash_state { FLASH_IDLE, FLASH_ERASING, FLASH_NEED_WRITE, FLASH_WRITING };
static flash_state fl_state = FLASH_IDLE;
static bool fl_dirty;

static uint32_t bond_page(void)
{
	return NRF_FICR->CODESIZE - 1;
}

static uint32_t *bond_page_addr(void)
{
	return (uint32_t *)(bond_page() * NRF_FICR->CODEPAGESIZE);
}

static bool slot_used(int i)
{
	uint32_t s = store.d.bonds[i].seq;

	return s != 0 && s != 0xFFFFFFFFUL;
}

static int bond_count(void)
{
	int n = 0;

	for (int i = 0; i < MAX_BONDS; i++) {
		n += slot_used(i);
	}
	return n;
}

static void store_load(void)
{
	memcpy(&store, bond_page_addr(), sizeof(store));
	if (store.d.magic != STORE_MAGIC) {
		memset(&store, 0, sizeof(store));
		store.d.magic = STORE_MAGIC;
	}
}

static void flash_poll(void)
{
	uint32_t err;

	if (fl_state == FLASH_IDLE && fl_dirty) {
		memcpy(&flash_buf, &store, sizeof(flash_buf));
		err = sd_flash_page_erase(bond_page());
		if (err == NRF_SUCCESS) {
			fl_dirty = false;
			fl_state = FLASH_ERASING;
		}
	} else if (fl_state == FLASH_NEED_WRITE) {
		err = sd_flash_write(bond_page_addr(), flash_buf.w, STORE_WORDS);
		if (err == NRF_SUCCESS) {
			fl_state = FLASH_WRITING;
		}
	}
}

static void on_soc_evt(uint32_t evt)
{
	if (evt == NRF_EVT_FLASH_OPERATION_SUCCESS) {
		if (fl_state == FLASH_ERASING) {
			fl_state = FLASH_NEED_WRITE;
			flash_poll();
		} else if (fl_state == FLASH_WRITING) {
			fl_state = FLASH_IDLE;
			ble_log("Bonds saved (%d PC%s)", bond_count(), bond_count() == 1 ? "" : "s");
		}
	} else if (evt == NRF_EVT_FLASH_OPERATION_ERROR) {
		/* Radio was too busy; start over */
		fl_state = FLASH_IDLE;
		fl_dirty = true;
	}
}

static int find_by_master_id(const ble_gap_master_id_t *m)
{
	for (int i = 0; i < MAX_BONDS; i++) {
		const ble_gap_master_id_t *s = &store.d.bonds[i].own_enc.master_id;

		if (slot_used(i) && s->ediv == m->ediv &&
		    memcmp(s->rand, m->rand, sizeof(s->rand)) == 0) {
			return i;
		}
	}
	return -1;
}

static int find_by_addr(const ble_gap_addr_t *a)
{
	for (int i = 0; i < MAX_BONDS; i++) {
		const ble_gap_addr_t *p = &store.d.bonds[i].peer;

		if (slot_used(i) && p->addr_type == a->addr_type &&
		    memcmp(p->addr, a->addr, sizeof(p->addr)) == 0) {
			return i;
		}
	}
	return -1;
}

static int pick_slot(const ble_gap_addr_t *peer)
{
	int oldest = 0;
	int s = find_by_addr(peer);

	if (s >= 0) {
		return s; /* same PC paired again: replace its bond */
	}

	for (int i = 0; i < MAX_BONDS; i++) {
		if (!slot_used(i)) {
			return i;
		}
		if (store.d.bonds[i].seq < store.d.bonds[oldest].seq) {
			oldest = i;
		}
	}
	return oldest;
}

static uint32_t next_seq(void)
{
	uint32_t max = 0;

	for (int i = 0; i < MAX_BONDS; i++) {
		if (slot_used(i) && store.d.bonds[i].seq > max) {
			max = store.d.bonds[i].seq;
		}
	}
	return max + 1;
}

/* ------------------------------------------------------------------------ */
/* Connection state                                                          */
/* ------------------------------------------------------------------------ */

static uint16_t conn = BLE_CONN_HANDLE_INVALID;
static bool encrypted;
static int cur_slot = -1;
static bool conn_params_requested;
static ble_gap_addr_t conn_peer;

/* Key buffers filled in by the SoftDevice during pairing */
static ble_gap_enc_key_t new_own_enc;
static ble_gap_id_key_t new_peer_id;
static ble_gap_sec_keyset_t keyset;

static void fill_sec_params(ble_gap_sec_params_t *p)
{
	memset(p, 0, sizeof(*p));
	p->bond = 1;
	p->mitm = 0;
	p->lesc = 0;
	p->keypress = 0;
	p->io_caps = BLE_GAP_IO_CAPS_NONE; /* -> Just Works */
	p->oob = 0;
	p->min_key_size = 7;
	p->max_key_size = 16;
	p->kdist_own.enc = 1;  /* we hand out an LTK the PC uses next time */
	p->kdist_peer.id = 1;  /* PC tells us its identity address */
}

static void save_sys_attr(void)
{
	uint8_t buf[SYS_ATTR_MAX];
	uint16_t len = sizeof(buf);

	if (conn == BLE_CONN_HANDLE_INVALID || cur_slot < 0) {
		return;
	}
	if (sd_ble_gatts_sys_attr_get(conn, buf, &len, 0) != NRF_SUCCESS) {
		return;
	}

	struct bond *b = &store.d.bonds[cur_slot];

	if (len == b->sys_attr_len && memcmp(buf, b->sys_attr, len) == 0) {
		return;
	}
	memcpy(b->sys_attr, buf, len);
	b->sys_attr_len = len;
	fl_dirty = true;
}

static bool restore_sys_attr(void)
{
	if (conn == BLE_CONN_HANDLE_INVALID || cur_slot < 0) {
		return false;
	}

	const struct bond *b = &store.d.bonds[cur_slot];

	if (b->sys_attr_len == 0 || b->sys_attr_len > SYS_ATTR_MAX) {
		return false;
	}
	return sd_ble_gatts_sys_attr_set(conn, b->sys_attr, b->sys_attr_len, 0) == NRF_SUCCESS;
}

static bool report_notifications_on(void)
{
	uint8_t cccd[2] = {0, 0};
	ble_gatts_value_t v;

	v.len = sizeof(cccd);
	v.offset = 0;
	v.p_value = cccd;

	if (sd_ble_gatts_value_get(conn, report_h.cccd_handle, &v) != NRF_SUCCESS) {
		return false;
	}
	return cccd[0] & BLE_GATT_HVX_NOTIFICATION;
}

static void adv_start(void)
{
	ble_gap_adv_params_t p;

	memset(&p, 0, sizeof(p));
	p.type = BLE_GAP_ADV_TYPE_ADV_IND;
	p.fp = BLE_GAP_ADV_FP_ANY;
	p.interval = ADV_INTERVAL;
	p.timeout = 0; /* advertise until connected */

	uint32_t err = sd_ble_gap_adv_start(&p);

	if (err == NRF_SUCCESS) {
		ble_log("Advertising as \"%s\" (%d bonded PC%s)", device_name, bond_count(),
		     bond_count() == 1 ? "" : "s");
	} else {
		ble_log("Advertising failed: 0x%lx", (unsigned long)err);
	}
}

static void on_ble_evt(const ble_evt_t *e)
{
	const ble_gap_evt_t *gap = &e->evt.gap_evt;

	switch (e->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED: {
		ble_gap_sec_params_t sp;

		conn = gap->conn_handle;
		encrypted = false;
		cur_slot = -1;
		conn_params_requested = false;
		conn_peer = gap->params.connected.peer_addr;
		ble_log("Connected");

		/* Security Request: PC re-encrypts (bonded) or starts pairing */
		fill_sec_params(&sp);
		sd_ble_gap_authenticate(conn, &sp);
		break;
	}

	case BLE_GAP_EVT_DISCONNECTED:
		ble_log("Disconnected (reason 0x%02x)", gap->params.disconnected.reason);
		conn = BLE_CONN_HANDLE_INVALID;
		encrypted = false;
		cur_slot = -1;
		adv_start();
		break;

	case BLE_GAP_EVT_SEC_PARAMS_REQUEST: {
		ble_gap_sec_params_t sp;

		ble_log("Pairing request");
		memset(&new_own_enc, 0, sizeof(new_own_enc));
		memset(&new_peer_id, 0, sizeof(new_peer_id));
		memset(&keyset, 0, sizeof(keyset));
		keyset.keys_own.p_enc_key = &new_own_enc;
		keyset.keys_peer.p_id_key = &new_peer_id;

		fill_sec_params(&sp);
		sd_ble_gap_sec_params_reply(conn, BLE_GAP_SEC_STATUS_SUCCESS, &sp, &keyset);
		break;
	}

	case BLE_GAP_EVT_SEC_INFO_REQUEST: {
		int s = find_by_master_id(&gap->params.sec_info_request.master_id);

		if (s >= 0) {
			cur_slot = s;
			sd_ble_gap_sec_info_reply(conn, &store.d.bonds[s].own_enc.enc_info, NULL, NULL);
			ble_log("Known PC, re-encrypting (bond %d)", s);
		} else {
			sd_ble_gap_sec_info_reply(conn, NULL, NULL, NULL);
			ble_log("Unknown key: remove \"%s\" in Windows and pair again", device_name);
		}
		break;
	}

	case BLE_GAP_EVT_CONN_SEC_UPDATE: {
		const ble_gap_conn_sec_t *cs = &gap->params.conn_sec_update.conn_sec;

		encrypted = cs->sec_mode.lv >= 2;
		ble_log("Link security level %d", cs->sec_mode.lv);

		if (encrypted && cur_slot >= 0) {
			restore_sys_attr();
		}

		if (encrypted && !conn_params_requested) {
			ble_gap_conn_params_t cp = {CONN_INT_MIN, CONN_INT_MAX, 0, CONN_TIMEOUT};

			sd_ble_gap_conn_param_update(conn, &cp);
			conn_params_requested = true;
		}
		break;
	}

	case BLE_GAP_EVT_AUTH_STATUS: {
		const ble_gap_evt_auth_status_t *a = &gap->params.auth_status;

		if (a->auth_status != BLE_GAP_SEC_STATUS_SUCCESS) {
			ble_log("Pairing failed (status 0x%02x)", a->auth_status);
			break;
		}
		if (!a->bonded) {
			ble_log("Paired without bonding");
			break;
		}

		const ble_gap_addr_t *peer = a->kdist_peer.id ? &new_peer_id.id_addr_info : &conn_peer;
		int s = pick_slot(peer);
		struct bond *b = &store.d.bonds[s];

		b->seq = next_seq();
		b->own_enc = new_own_enc;
		b->peer = *peer;
		b->sys_attr_len = 0;
		cur_slot = s;
		fl_dirty = true;
		save_sys_attr(); /* in case notifications were already enabled */
		ble_log("Paired and bonded (slot %d)", s);
		break;
	}

	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		if (!(encrypted && restore_sys_attr())) {
			sd_ble_gatts_sys_attr_set(conn, NULL, 0, 0);
		}
		break;

	case BLE_GATTS_EVT_WRITE: {
		uint16_t h = e->evt.gatts_evt.params.write.handle;

		if (h == report_h.cccd_handle || h == bas_h.cccd_handle) {
			save_sys_attr();
			if (h == report_h.cccd_handle) {
				ble_log("PC %s input reports",
				     (e->evt.gatts_evt.params.write.data[0] & 1) ? "enabled" : "disabled");
			}
		}
		break;
	}

	case BLE_GAP_EVT_CONN_PARAM_UPDATE: {
		const ble_gap_conn_params_t *cp = &gap->params.conn_param_update.conn_params;

		ble_log("Conn interval %u.%02u ms", cp->max_conn_interval * 125 / 100,
		     (cp->max_conn_interval * 125) % 100);
		break;
	}

	case BLE_GATTS_EVT_TIMEOUT:
		sd_ble_gap_disconnect(conn, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		break;

	case BLE_EVT_USER_MEM_REQUEST:
		sd_ble_user_mem_reply(conn, NULL);
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Setup                                                                     */
/* ------------------------------------------------------------------------ */

static void sd_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
	(void)id;
	(void)pc;
	(void)info;
	NVIC_SystemReset();
}

static uint32_t add_char(uint16_t svc, uint16_t uuid16, bool read, bool notify, bool write_wo_resp,
			 bool encrypted_access, const uint8_t *value, uint16_t len,
			 ble_gatts_char_handles_t *handles, uint8_t uuid_type = BLE_UUID_TYPE_BLE)
{
	ble_gatts_char_md_t char_md;
	ble_gatts_attr_md_t cccd_md;
	ble_gatts_attr_md_t attr_md;
	ble_gatts_attr_t attr;
	ble_uuid_t uuid;

	memset(&char_md, 0, sizeof(char_md));
	char_md.char_props.read = read;
	char_md.char_props.notify = notify;
	char_md.char_props.write_wo_resp = write_wo_resp;

	if (notify) {
		memset(&cccd_md, 0, sizeof(cccd_md));
		BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
		if (encrypted_access) {
			BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&cccd_md.write_perm);
		} else {
			BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
		}
		cccd_md.vloc = BLE_GATTS_VLOC_STACK;
		char_md.p_cccd_md = &cccd_md;
	}

	memset(&attr_md, 0, sizeof(attr_md));
	if (read) {
		if (encrypted_access) {
			BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&attr_md.read_perm);
		} else {
			BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
		}
	} else {
		BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.read_perm);
	}
	if (write_wo_resp) {
		BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&attr_md.write_perm);
	} else {
		BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
	}
	attr_md.vloc = BLE_GATTS_VLOC_STACK;

	uuid.uuid = uuid16;
	uuid.type = uuid_type;

	memset(&attr, 0, sizeof(attr));
	attr.p_uuid = &uuid;
	attr.p_attr_md = &attr_md;
	attr.init_len = len;
	attr.max_len = len;
	attr.p_value = (uint8_t *)value;

	return sd_ble_gatts_characteristic_add(svc, &char_md, &attr, handles);
}

static uint32_t add_report_reference(void)
{
	ble_gatts_attr_md_t md;
	ble_gatts_attr_t attr;
	ble_uuid_t uuid;
	uint16_t handle;

	memset(&md, 0, sizeof(md));
	BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&md.read_perm);
	BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&md.write_perm);
	md.vloc = BLE_GATTS_VLOC_STACK;

	uuid.uuid = 0x2908; /* Report Reference */
	uuid.type = BLE_UUID_TYPE_BLE;

	memset(&attr, 0, sizeof(attr));
	attr.p_uuid = &uuid;
	attr.p_attr_md = &md;
	attr.init_len = sizeof(report_ref);
	attr.max_len = sizeof(report_ref);
	attr.p_value = (uint8_t *)report_ref;

	/* BLE_GATT_HANDLE_INVALID: attach to the characteristic added last */
	return sd_ble_gatts_descriptor_add(BLE_GATT_HANDLE_INVALID, &attr, &handle);
}

static uint32_t add_service(uint16_t uuid16, uint16_t *svc)
{
	ble_uuid_t uuid;

	uuid.uuid = uuid16;
	uuid.type = BLE_UUID_TYPE_BLE;
	return sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &uuid, svc);
}

#define CHECK(call)                                                   \
	do {                                                          \
		uint32_t _e = (call);                                 \
		if (_e != NRF_SUCCESS) {                              \
			ble_log("%s failed: 0x%lx", #call, (unsigned long)_e); \
			return false;                                 \
		}                                                     \
	} while (0)

static bool gatt_init(void)
{
	ble_gatts_char_handles_t h;
	uint16_t svc;
	uint8_t supply_uuid_type;
	ble_uuid_t supply_uuid;

	/* HID service */
	CHECK(add_service(0x1812, &svc));
	CHECK(add_char(svc, 0x2A4A, true, false, false, true, hid_info, sizeof(hid_info), &h));
	CHECK(add_char(svc, 0x2A4B, true, false, false, true, report_map, sizeof(report_map), &h));
	CHECK(add_char(svc, 0x2A4D, true, true, false, true, zero_report, sizeof(zero_report),
		       &report_h));
	CHECK(add_report_reference());
	CHECK(add_char(svc, 0x2A4C, false, false, true, true, &zero_byte, 1, &h));

	/* Battery service (fixed 100 %: the Joystick:bit gives no battery reading) */
	CHECK(add_service(0x180F, &svc));
	CHECK(add_char(svc, 0x2A19, true, true, false, false, &battery_level, 1, &bas_h));

	/* Device Information: PnP ID */
	CHECK(add_service(0x180A, &svc));
	CHECK(add_char(svc, 0x2A50, true, false, false, false, pnp_id, sizeof(pnp_id), &h));

	/* Append the voltage service so existing HID attribute handles stay stable. */
	CHECK(sd_ble_uuid_vs_add(&supply_uuid_base, &supply_uuid_type));
	supply_uuid.uuid = 0x0001;
	supply_uuid.type = supply_uuid_type;
	CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &supply_uuid, &svc));
	CHECK(add_char(svc, 0x0002, true, false, false, false, supply_initial,
		       sizeof(supply_initial), &supply_h, supply_uuid_type));

	return true;
}

bool ble_init(const char *name)
{
	if (name && name[0]) {
		strncpy(device_name, name, MAX_NAME_LEN);
		device_name[MAX_NAME_LEN] = '\0';
	}

	extern uint32_t __data_start__;
	uint32_t app_ram_base = (uint32_t)&__data_start__;
	nrf_clock_lf_cfg_t clk;
	ble_enable_params_t ep;
	ble_gap_conn_sec_mode_t name_perm;
	ble_gap_conn_params_t cp = {CONN_INT_MIN, CONN_INT_MAX, 0, CONN_TIMEOUT};
	size_t name_len;

	name_len = strlen(device_name);
	store_load();

	/* micro:bit V1 has no 32 kHz crystal: calibrated RC oscillator */
	clk.source = NRF_CLOCK_LF_SRC_RC;
	clk.rc_ctiv = 16;
	clk.rc_temp_ctiv = 2;
	clk.xtal_accuracy = NRF_CLOCK_LF_XTAL_ACCURACY_250_PPM;
	CHECK(sd_softdevice_enable(&clk, sd_fault_handler));

	memset(&ep, 0, sizeof(ep));
	ep.common_enable_params.vs_uuid_count = BLE_UUID_VS_COUNT_MIN;
	ep.gatts_enable_params.attr_tab_size = BLE_GATTS_ATTR_TAB_SIZE_DEFAULT;
	ep.gatts_enable_params.service_changed = 1;
	ep.gap_enable_params.periph_conn_count = 1;
	ep.gap_enable_params.central_conn_count = 0;
	ep.gap_enable_params.central_sec_count = 0;
	uint32_t err = sd_ble_enable(&ep, &app_ram_base);
	if (err != NRF_SUCCESS) {
		ble_log("sd_ble_enable failed: 0x%lx (needs RAM from 0x%lx)", (unsigned long)err,
		     (unsigned long)app_ram_base);
		return false;
	}

	BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&name_perm);
	CHECK(sd_ble_gap_device_name_set(&name_perm, (const uint8_t *)device_name, name_len));
	CHECK(sd_ble_gap_appearance_set(BLE_APPEARANCE_HID_GAMEPAD));
	CHECK(sd_ble_gap_ppcp_set(&cp));

	if (!gatt_init()) {
		return false;
	}

	scan_rsp[0] = name_len + 1;
	scan_rsp[1] = 0x09; /* Complete Local Name */
	memcpy(&scan_rsp[2], device_name, name_len);
	scan_rsp_len = name_len + 2;
	CHECK(sd_ble_gap_adv_data_set(adv_data, sizeof(adv_data), scan_rsp, scan_rsp_len));

	adv_start();
	return true;
}

/* ------------------------------------------------------------------------ */
/* Runtime                                                                   */
/* ------------------------------------------------------------------------ */

/* analogRead() also uses the ADC. This runs only in the main loop, between
 * input samples, and restores its configuration before the next analogRead. */
static uint16_t read_supply_mv(void)
{
	uint32_t old_config = NRF_ADC->CONFIG;
	uint32_t old_enable = NRF_ADC->ENABLE;
	NRF_ADC->ENABLE = 1;
	NRF_ADC->CONFIG = (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
		(ADC_CONFIG_INPSEL_SupplyOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
		(ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos);
	NRF_ADC->EVENTS_END = 0;
	NRF_ADC->TASKS_START = 1;
	uint32_t start = micros();
	while (!NRF_ADC->EVENTS_END && (uint32_t)(micros() - start) < 10000) { }
	uint16_t mv = 0;
	if (NRF_ADC->EVENTS_END) {
		/* The 10-bit ADC measures VDD / 3 against a nominal 1.2 V reference. */
		mv = (uint16_t)((NRF_ADC->RESULT * 3600UL + 512) / 1024);
	}
	NRF_ADC->TASKS_STOP = 1;
	NRF_ADC->EVENTS_END = 0;
	NRF_ADC->ENABLE = old_enable;
	NRF_ADC->CONFIG = old_config;
	return mv;
}

static void update_supply(void)
{
	static uint32_t last_sample;
	static bool sampled;
	uint32_t now = millis();
	if (sampled && (uint32_t)(now - last_sample) < 5000) {
		return;
	}
	sampled = true;
	last_sample = now;
	uint16_t mv = read_supply_mv();
	if (!mv) {
		ble_log("Supply voltage sample failed");
		return;
	}
	uint8_t bytes[2] = {(uint8_t)mv, (uint8_t)(mv >> 8)};
	ble_gatts_value_t value = {sizeof(bytes), 0, bytes};
	uint32_t err = sd_ble_gatts_value_set(BLE_CONN_HANDLE_INVALID, supply_h.value_handle, &value);
	if (err == NRF_SUCCESS) {
		ble_log("Supply voltage: %u mV", mv);
	} else {
		ble_log("Supply update failed: 0x%lx", (unsigned long)err);
	}
}

void ble_poll(void)
{
	static uint32_t evt_buf[(sizeof(ble_evt_t) + GATT_MTU_SIZE_DEFAULT + 3) / 4];
	uint32_t soc_evt;
	update_supply();

	while (sd_evt_get(&soc_evt) == NRF_SUCCESS) {
		on_soc_evt(soc_evt);
	}

	for (;;) {
		uint16_t len = sizeof(evt_buf);

		if (sd_ble_evt_get((uint8_t *)evt_buf, &len) != NRF_SUCCESS) {
			break;
		}
		on_ble_evt((const ble_evt_t *)evt_buf);
	}

	flash_poll();
}

enum ble_link ble_link_state(void)
{
	if (conn == BLE_CONN_HANDLE_INVALID) {
		return LINK_ADVERTISING;
	}
	if (encrypted && report_notifications_on()) {
		return LINK_READY;
	}
	return LINK_PAIRING;
}

bool ble_send(const struct gamepad_report &report)
{
	uint16_t len = sizeof(report);
	ble_gatts_value_t v;
	ble_gatts_hvx_params_t hvx;

	/* Keep the readable value current too */
	v.len = len;
	v.offset = 0;
	v.p_value = (uint8_t *)&report;
	sd_ble_gatts_value_set(BLE_CONN_HANDLE_INVALID, report_h.value_handle, &v);

	if (ble_link_state() != LINK_READY) {
		return false;
	}

	memset(&hvx, 0, sizeof(hvx));
	hvx.handle = report_h.value_handle;
	hvx.type = BLE_GATT_HVX_NOTIFICATION;
	hvx.p_len = &len;
	hvx.p_data = (uint8_t *)&report;

	return sd_ble_gatts_hvx(conn, &hvx) == NRF_SUCCESS;
}

void ble_forget_all_bonds(void)
{
	memset(&store, 0, sizeof(store));
	store.d.magic = STORE_MAGIC;
	fl_dirty = true;
	ble_log("All pairings deleted");
}

/*
 * Gamepad runtime for the block editor. See gp_runtime.h.
 */
#include <Arduino.h>
#include <string.h>

#include "gamepad.h"
#include "gp_runtime.h"
#include "v1_ble.h"
#include "v1_display.h"
#include "v1_input.h"

/* ---- LED pictures (row bit 0 = leftmost LED) ---- */
static const struct image5 adv_frames[] = {
	{{0x00, 0x00, 0x04, 0x00, 0x00}},
	{{0x00, 0x04, 0x0A, 0x04, 0x00}},
	{{0x04, 0x0A, 0x11, 0x0A, 0x04}},
	{{0x00, 0x00, 0x00, 0x00, 0x00}},
};
static const struct image5 icons[] = {   /* same order as enum gp_icon */
	{{0x11, 0x0A, 0x04, 0x0A, 0x11}}, /* GP_ICON_CROSS  */
	{{0x00, 0x10, 0x08, 0x05, 0x02}}, /* GP_ICON_TICK   */
	{{0x00, 0x0E, 0x0A, 0x0E, 0x00}}, /* GP_ICON_SQUARE */
	{{0x04, 0x04, 0x04, 0x00, 0x04}}, /* GP_ICON_BANG   */
};

#define ADV_FRAME_MS  200
#define READY_TICK_MS 1000

static bool ab_at_start;
static bool bt_started;
static struct gamepad_report report;
static struct gamepad_report last_sent;
static bool host_synced;

/* ---- on start ------------------------------------------------------------ */

void gp_boot(void)
{
	Serial.begin(115200);
	Serial.println();
	Serial.println("micro:bit Joystick:bit gamepad (V1 / S130) starting");

	/* Timer + NVIC setup must happen before the SoftDevice is enabled */
	display_init();
	input_init();
	ab_at_start = input_a_and_b_held();
}

bool gp_ab_held_at_start(void)
{
	return ab_at_start;
}

void gp_bluetooth_begin(const char *name)
{
	if (bt_started) {
		return;
	}
	if (!ble_init(name)) {
		display_show(icons[GP_ICON_BANG]);
		for (;;) {
		}
	}
	bt_started = true;
}

void gp_forget_all_pairings(void)
{
	if (bt_started) {
		ble_forget_all_bonds();
	}
}

void gp_show_icon(enum gp_icon icon)
{
	if ((unsigned)icon < sizeof(icons) / sizeof(icons[0])) {
		display_show(icons[icon]);
	}
}

void gp_wait_ab_released(void)
{
	while (input_a_and_b_held()) {
		gp_poll();
	}
	uint32_t until = millis() + 500;
	while ((int32_t)(until - millis()) > 0) {
		gp_poll();
	}
}

/* ---- every N ms ---------------------------------------------------------- */

void gp_poll(void)
{
	if (bt_started) {
		ble_poll();
	}
}

bool gp_every(uint32_t interval_ms)
{
	static uint32_t next;
	uint32_t now = millis();

	if ((int32_t)(now - next) < 0) {
		return false;
	}
	next = now + interval_ms;
	return true;
}

void gp_inputs_update(void)
{
	input_update();
	memset(&report, 0, sizeof(report));
}

bool gp_key_pressed(int key)
{
	return key >= 1 && key <= 6 && (input_buttons() & GP_BTN_KEY(key));
}

bool gp_button_pressed(char ab)
{
	if (ab == 'A' || ab == 'a') {
		return input_buttons() & GP_BTN_A;
	}
	if (ab == 'B' || ab == 'b') {
		return input_buttons() & GP_BTN_B;
	}
	return false;
}

int gp_stick(enum gp_axis axis)
{
	return input_stick(axis == GP_AXIS_Y ? STICK_Y : STICK_X);
}

bool gp_stick_connected(enum gp_axis axis)
{
	return input_stick_connected(axis == GP_AXIS_Y ? STICK_Y : STICK_X);
}

int gp_analog_read(int pin)
{
	return input_analog_raw(pin);
}

void gp_press(int button)
{
	if (button >= 1 && button <= 8) {
		report.buttons |= GP_BIT(button - 1);
	}
}

void gp_set_axis(enum gp_axis axis, int value)
{
	if (value > 127) {
		value = 127;
	} else if (value < -127) {
		value = -127;
	}
	if (axis == GP_AXIS_Y) {
		report.y = (int8_t)value;
	} else {
		report.x = (int8_t)value;
	}
}

void gp_report_send(void)
{
	if (!bt_started) {
		return;
	}
	if (ble_link_state() != LINK_READY) {
		host_synced = false;
		return;
	}
	if (!host_synced || memcmp(&report, &last_sent, sizeof(report)) != 0) {
		if (ble_send(report)) {
			last_sent = report;
			host_synced = true;
		}
		/* else: radio buffers full, retry next cycle */
	}
}

/* -127..127 -> LED column/row 0..4 */
static int axis_to_led(int8_t v)
{
	return ((int)v + 127) * 5 / 255;
}

static void show_live(void)
{
	static const char names[8] = {'1', '2', '3', '4', '5', '6', 'A', 'B'};

	for (int i = 0; i < 8; i++) {
		if (report.buttons & GP_BIT(i)) {
			display_show(display_glyph(names[i]));
			return;
		}
	}

	struct image5 dot = {{0, 0, 0, 0, 0}};

	dot.row[axis_to_led(report.y)] = 1U << axis_to_led(report.x);
	display_show(dot);
}

void gp_status_show(void)
{
	static enum ble_link last_link = LINK_ADVERTISING;
	static uint32_t tick_until;
	enum ble_link link = bt_started ? ble_link_state() : LINK_ADVERTISING;
	uint32_t now = millis();
	bool active = report.buttons || report.x || report.y;

	if (link == LINK_READY && last_link != LINK_READY) {
		tick_until = now + READY_TICK_MS;
	}
	last_link = link;

	if (active) {
		show_live();
	} else if (link == LINK_READY) {
		if ((int32_t)(tick_until - now) > 0) {
			display_show(icons[GP_ICON_TICK]);
		} else {
			show_live();
		}
	} else if (link == LINK_PAIRING) {
		display_show(icons[GP_ICON_SQUARE]);
	} else {
		display_show(adv_frames[(now / ADV_FRAME_MS) % 4]);
	}
}

/*
 * 5x5 LED feedback.
 *
 *   advertising   expanding rings (pair me / waiting for PC)
 *   pairing       hollow square
 *   ready         tick for 1 s, then a dot that follows the stick
 *   any input     live preview: pressed key ("1".."6", "A", "B") or stick dot
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/display/mb_display.h>

#include "ui.h"

#define READY_TICK_MS 1000
#define ADV_FRAME_MS  200

enum {
	VIEW_NONE = -1,
	VIEW_ADV = 1,
	VIEW_PAIRING,
	VIEW_TICK,
	VIEW_CLEARED,
	VIEW_CHAR_BASE = 0x100, /* + character   */
	VIEW_DOT_BASE = 0x200,  /* + row * 5 + col */
};

static const struct mb_image adv_frames[] = {
	MB_IMAGE({0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 0},
		 {0, 0, 1, 0, 0},
		 {0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 0}),
	MB_IMAGE({0, 0, 0, 0, 0},
		 {0, 0, 1, 0, 0},
		 {0, 1, 0, 1, 0},
		 {0, 0, 1, 0, 0},
		 {0, 0, 0, 0, 0}),
	MB_IMAGE({0, 0, 1, 0, 0},
		 {0, 1, 0, 1, 0},
		 {1, 0, 0, 0, 1},
		 {0, 1, 0, 1, 0},
		 {0, 0, 1, 0, 0}),
	MB_IMAGE({0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 0}),
};

static const struct mb_image pairing_img =
	MB_IMAGE({0, 0, 0, 0, 0},
		 {0, 1, 1, 1, 0},
		 {0, 1, 0, 1, 0},
		 {0, 1, 1, 1, 0},
		 {0, 0, 0, 0, 0});

static const struct mb_image tick_img =
	MB_IMAGE({0, 0, 0, 0, 0},
		 {0, 0, 0, 0, 1},
		 {0, 0, 0, 1, 0},
		 {1, 0, 1, 0, 0},
		 {0, 1, 0, 0, 0});

static const struct mb_image cleared_img =
	MB_IMAGE({1, 0, 0, 0, 1},
		 {0, 1, 0, 1, 0},
		 {0, 0, 1, 0, 0},
		 {0, 1, 0, 1, 0},
		 {1, 0, 0, 0, 1});

static struct mb_display *disp;
static struct mb_image dot_img;
static int current_view = VIEW_NONE;
static enum ui_link last_link = UI_ADVERTISING;
static int64_t tick_until;

/* -127..127 -> LED column/row 0..4 */
static int axis_to_led(int8_t v)
{
	return ((int)v + 127) * 5 / 255;
}

static int live_view(const struct gamepad_report *r)
{
	static const char names[8] = {'1', '2', '3', '4', '5', '6', 'A', 'B'};

	for (int i = 0; i < 8; i++) {
		if (r->buttons & GP_BIT(i)) {
			return VIEW_CHAR_BASE + names[i];
		}
	}

	return VIEW_DOT_BASE + axis_to_led(r->y) * 5 + axis_to_led(r->x);
}

static void show(int view)
{
	if (view == current_view) {
		return;
	}
	current_view = view;

	if (view >= VIEW_DOT_BASE) {
		int idx = view - VIEW_DOT_BASE;

		memset(&dot_img, 0, sizeof(dot_img));
		dot_img.row[idx / 5] = BIT(idx % 5);
		mb_display_image(disp, MB_DISPLAY_MODE_SINGLE, SYS_FOREVER_MS, &dot_img, 1);
		return;
	}

	if (view >= VIEW_CHAR_BASE) {
		mb_display_print(disp, MB_DISPLAY_MODE_SINGLE, SYS_FOREVER_MS, "%c",
				 (char)(view - VIEW_CHAR_BASE));
		return;
	}

	switch (view) {
	case VIEW_ADV:
		mb_display_image(disp, MB_DISPLAY_MODE_SINGLE | MB_DISPLAY_FLAG_LOOP,
				 ADV_FRAME_MS, adv_frames, ARRAY_SIZE(adv_frames));
		break;
	case VIEW_PAIRING:
		mb_display_image(disp, MB_DISPLAY_MODE_SINGLE, SYS_FOREVER_MS, &pairing_img, 1);
		break;
	case VIEW_TICK:
		mb_display_image(disp, MB_DISPLAY_MODE_SINGLE, SYS_FOREVER_MS, &tick_img, 1);
		break;
	case VIEW_CLEARED:
		mb_display_image(disp, MB_DISPLAY_MODE_SINGLE, SYS_FOREVER_MS, &cleared_img, 1);
		break;
	default:
		break;
	}
}

void ui_init(void)
{
	disp = mb_display_get();
}

void ui_update(enum ui_link link, const struct gamepad_report *r)
{
	int64_t now = k_uptime_get();
	bool active = r->buttons || r->x || r->y;
	int view;

	if (link == UI_READY && last_link != UI_READY) {
		tick_until = now + READY_TICK_MS;
	}
	last_link = link;

	if (active) {
		view = live_view(r);
	} else if (link == UI_READY) {
		view = (now < tick_until) ? VIEW_TICK : live_view(r);
	} else if (link == UI_PAIRING) {
		view = VIEW_PAIRING;
	} else {
		view = VIEW_ADV;
	}

	show(view);
}

void ui_show_bonds_cleared(void)
{
	show(VIEW_CLEARED);
}

/*
 * Shared definitions for the micro:bit Joystick:bit gamepad.
 * Used by both firmwares (V2: Zephyr, top-level src/ files; V1: Arduino + S130, src/v1/).
 */
#ifndef GAMEPAD_H_
#define GAMEPAD_H_

#include <stdint.h>

#define GP_BIT(n) (1U << (n))

/* ------------------------------------------------------------------------ */
/* Tunables                                                                  */
/* ------------------------------------------------------------------------ */

/* How often inputs are sampled (ms). Reports are only sent on change. */
#define GP_POLL_INTERVAL_MS 10

/*
 * Axis direction. HID wants left = negative X, up = negative Y.
 * Checked on a real Joystick:bit V1:
 *   LEFT -> P0 low (~0),   RIGHT -> P0 high (~4095)  => X not inverted
 *                          (the ELECFREAKS wiki claims the opposite)
 *   UP   -> P1 high (~4095), DOWN -> P1 low (~0)     => Y inverted
 * Flip these if your board behaves differently (the LED dot on the
 * micro:bit shows what the PC will see).
 */
#define GP_INVERT_X 0
#define GP_INVERT_Y 1

/* Dead zone around the stick centre, in 12-bit ADC counts (4095 = VDD). */
#define GP_DEADZONE 80

/* Last N percent of travel is treated as full deflection. */
#define GP_OUTER_SATURATION_PCT 8

/* A button change is accepted after this many identical consecutive samples. */
#define GP_DEBOUNCE_SAMPLES 2

/* ------------------------------------------------------------------------ */
/* HID input report (Report ID 1)                                            */
/* ------------------------------------------------------------------------ */

#define GP_REPORT_ID 1

/* Bit positions in gamepad_report.buttons -> Windows "Button 1..8" */
#define GP_BTN_KEY(n) GP_BIT((n) - 1) /* Joystick:bit keys 1..6 -> buttons 1..6 */
#define GP_BTN_A      GP_BIT(6)       /* micro:bit button A    -> button 7 */
#define GP_BTN_B      GP_BIT(7)       /* micro:bit button B    -> button 8 */

struct __attribute__((packed)) gamepad_report {
	uint8_t buttons;
	int8_t x; /* -127 (left) .. 127 (right) */
	int8_t y; /* -127 (up)   .. 127 (down)  */
};

/*
 * HID report descriptor: 8 buttons + X/Y (signed 8-bit). Must match
 * struct gamepad_report. If you change it, remove the device in Windows and
 * pair again - Windows caches the descriptor.
 */
#define GP_REPORT_MAP_INIT { \
	0x05, 0x01,         /* Usage Page (Generic Desktop)   */ \
	0x09, 0x05,         /* Usage (Game Pad)               */ \
	0xA1, 0x01,         /* Collection (Application)       */ \
	0x85, GP_REPORT_ID, /*   Report ID                    */ \
	0x05, 0x09,         /*   Usage Page (Button)          */ \
	0x19, 0x01,         /*   Usage Minimum (Button 1)     */ \
	0x29, 0x08,         /*   Usage Maximum (Button 8)     */ \
	0x15, 0x00,         /*   Logical Minimum (0)          */ \
	0x25, 0x01,         /*   Logical Maximum (1)          */ \
	0x75, 0x01,         /*   Report Size (1)              */ \
	0x95, 0x08,         /*   Report Count (8)             */ \
	0x81, 0x02,         /*   Input (Data,Var,Abs)         */ \
	0x05, 0x01,         /*   Usage Page (Generic Desktop) */ \
	0x09, 0x01,         /*   Usage (Pointer)              */ \
	0xA1, 0x00,         /*   Collection (Physical)        */ \
	0x09, 0x30,         /*     Usage (X)                  */ \
	0x09, 0x31,         /*     Usage (Y)                  */ \
	0x15, 0x81,         /*     Logical Minimum (-127)     */ \
	0x25, 0x7F,         /*     Logical Maximum (127)      */ \
	0x75, 0x08,         /*     Report Size (8)            */ \
	0x95, 0x02,         /*     Report Count (2)           */ \
	0x81, 0x02,         /*     Input (Data,Var,Abs)       */ \
	0xC0,               /*   End Collection               */ \
	0xC0,               /* End Collection                 */ \
}

#endif /* GAMEPAD_H_ */

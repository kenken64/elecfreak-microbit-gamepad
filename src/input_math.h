/*
 * Shared input maths for both firmwares (V1: src/v1/v1_input.cpp, V2: src/input.c).
 *
 * Pure C with no hardware access, so it compiles unchanged for the micro:bit
 * and for the host-side tests in tests/test_input_math.c.
 *
 * Header-only on purpose: the V1 PlatformIO env only compiles src/v1/, so a
 * separate .c file would need build-filter changes. These are small functions.
 */
#ifndef INPUT_MATH_H_
#define INPUT_MATH_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "gamepad.h"

/* Both drivers normalise ADC readings to 12 bits (0..4095 = 0..VDD). */
#define GP_ADC_BITS 12
#define GP_ADC_MAX  ((1 << GP_ADC_BITS) - 1)

/* Joystick:bit keys 1..6 share one resistor ladder. */
#define GP_KEY_COUNT 6

/*
 * Ladder decision thresholds on the 10-bit scale (0..1023), taken from the
 * official ELECFREAKS MakeCode example for this board. Each is the midpoint
 * between two neighbouring key voltages.
 */
#define GP_KEY_THRESHOLDS_10BIT {256, 597, 725, 793, 836, 938}

/*
 * Scale one axis reading to a signed 8-bit HID value (-127..127).
 *
 * raw and center are 12-bit ADC counts. A negative raw means the read failed:
 * report a centred stick rather than pretending the stick was pushed.
 */
static inline int8_t gp_scale_axis(int raw, int center, bool invert)
{
	if (raw < 0) {
		return 0;
	}

	int delta = raw - center;
	int travel = (delta >= 0) ? (GP_ADC_MAX - center) : center;
	int span = travel * (100 - GP_OUTER_SATURATION_PCT) / 100 - GP_DEADZONE;
	int mag = abs(delta) - GP_DEADZONE;

	if (mag <= 0 || span <= 0) {
		return 0;
	}

	int v = mag * 127 / span;

	if (v > 127) {
		v = 127;
	}
	if (delta < 0) {
		v = -v;
	}

	return (int8_t)(invert ? -v : v);
}

/*
 * Decode the key ladder from a 10-bit reading.
 *
 * Returns the matching GP_BTN_KEY(1..6) bit, or 0 when no key is pressed.
 * A negative reading (failed ADC read) counts as no key.
 */
static inline uint8_t gp_decode_key_ladder(int raw10)
{
	static const uint16_t thresholds[GP_KEY_COUNT] = GP_KEY_THRESHOLDS_10BIT;

	if (raw10 < 0) {
		return 0;
	}

	for (int i = 0; i < GP_KEY_COUNT; i++) {
		if (raw10 < thresholds[i]) {
			return GP_BTN_KEY(i + 1);
		}
	}

	return 0;
}

/*
 * Button debounce: a new value is accepted once it has been read
 * GP_DEBOUNCE_SAMPLES times in a row.
 */
struct gp_debounce {
	uint8_t stable;
	uint8_t candidate;
	uint8_t count;
};

/* Start in a known state (the reading taken at power-up). */
static inline void gp_debounce_init(struct gp_debounce *d, uint8_t value)
{
	d->stable = value;
	d->candidate = value;
	d->count = 0;
}

/* Feed one raw reading and return the debounced value. */
static inline uint8_t gp_debounce_update(struct gp_debounce *d, uint8_t raw)
{
	if (raw != d->candidate) {
		d->candidate = raw;
		d->count = 1;
	} else if (d->candidate != d->stable && ++d->count >= GP_DEBOUNCE_SAMPLES) {
		d->stable = d->candidate;
	}

	return d->stable;
}

#endif /* INPUT_MATH_H_ */

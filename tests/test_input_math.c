/*
 * Host-side tests for src/input_math.h. No micro:bit, no hardware: the maths
 * that both firmwares share (axis scaling, key ladder, debounce) is compiled
 * for the PC and checked against hand-computed values.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -Isrc tests/test_input_math.c -o test_input_math
 *   ./test_input_math
 *
 * or, from the repository root:  python tools/run_tests.py
 */
#include <stdio.h>

#include "input_math.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			failures++;                                          \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                            \
	} while (0)

#define CHECK_EQ(expr, want)                                                     \
	do {                                                                     \
		long got_ = (long)(expr);                                        \
		long want_ = (long)(want);                                       \
		if (got_ != want_) {                                             \
			failures++;                                              \
			printf("FAIL %s:%d: %s == %ld, expected %ld\n",          \
			       __FILE__, __LINE__, #expr, got_, want_);          \
		}                                                                \
	} while (0)

static const int CENTRE = GP_ADC_MAX / 2; /* 2047 */

static void test_axis_centre_and_deadzone(void)
{
	/* Centred stick, and anything inside the dead zone, reports zero. */
	CHECK_EQ(gp_scale_axis(CENTRE, CENTRE, false), 0);
	CHECK_EQ(gp_scale_axis(CENTRE + GP_DEADZONE, CENTRE, false), 0);
	CHECK_EQ(gp_scale_axis(CENTRE - GP_DEADZONE, CENTRE, false), 0);
	CHECK_EQ(gp_scale_axis(CENTRE + 1, CENTRE, false), 0);

	/* First count past the dead zone that still rounds to one step. */
	CHECK_EQ(gp_scale_axis(CENTRE + GP_DEADZONE + 14, CENTRE, false), 0);
	CHECK_EQ(gp_scale_axis(CENTRE + GP_DEADZONE + 15, CENTRE, false), 1);
	CHECK_EQ(gp_scale_axis(CENTRE - GP_DEADZONE - 15, CENTRE, false), -1);
}

static void test_axis_full_deflection(void)
{
	CHECK_EQ(gp_scale_axis(GP_ADC_MAX, CENTRE, false), 127);
	CHECK_EQ(gp_scale_axis(0, CENTRE, false), -127);

	/* Outer saturation: the last GP_OUTER_SATURATION_PCT of travel is full. */
	int edge = CENTRE + (GP_ADC_MAX - CENTRE) * (100 - GP_OUTER_SATURATION_PCT) / 100;

	CHECK_EQ(gp_scale_axis(edge - 1, CENTRE, false), 126);
	CHECK_EQ(gp_scale_axis(edge, CENTRE, false), 127);
}

static void test_axis_golden_values(void)
{
	/* Pins the formula: half travel right and left with the default tunables. */
	CHECK_EQ(gp_scale_axis(CENTRE + (GP_ADC_MAX - CENTRE) / 2, CENTRE, false), 66);
	CHECK_EQ(gp_scale_axis(CENTRE / 2, CENTRE, false), -66);
}

static void test_axis_invert(void)
{
	CHECK_EQ(gp_scale_axis(GP_ADC_MAX, CENTRE, true), -127);
	CHECK_EQ(gp_scale_axis(0, CENTRE, true), 127);
	CHECK_EQ(gp_scale_axis(CENTRE, CENTRE, true), 0);
	CHECK_EQ(gp_scale_axis(CENTRE + 500, CENTRE, true),
		 -gp_scale_axis(CENTRE + 500, CENTRE, false));
}

static void test_axis_read_error(void)
{
	/* A failed ADC read must look centred, never like a pushed stick. */
	CHECK_EQ(gp_scale_axis(-1, CENTRE, false), 0);
	CHECK_EQ(gp_scale_axis(-4096, CENTRE, true), 0);
}

static void test_axis_off_centre_and_far_centres(void)
{
	/* A calibrated centre that is not the ideal one still spans the full range. */
	CHECK(gp_scale_axis(GP_ADC_MAX, 1800, false) > 0);
	CHECK(gp_scale_axis(0, 1800, false) < 0);
	CHECK_EQ(gp_scale_axis(1800, 1800, false), 0);

	CHECK_EQ(gp_scale_axis(0, 0, false), 0);
	CHECK_EQ(gp_scale_axis(GP_ADC_MAX, 0, false), 127);
	CHECK_EQ(gp_scale_axis(0, GP_ADC_MAX, false), -127);
	CHECK_EQ(gp_scale_axis(GP_ADC_MAX, GP_ADC_MAX, false), 0);
}

static void test_axis_range_and_monotonicity(void)
{
	static const int centres[] = {0, 1000, CENTRE, 3000, GP_ADC_MAX};

	for (unsigned c = 0; c < sizeof(centres) / sizeof(centres[0]); c++) {
		int previous = -128;

		for (int raw = 0; raw <= GP_ADC_MAX; raw++) {
			int v = gp_scale_axis(raw, centres[c], false);

			if (v < -127 || v > 127) {
				failures++;
				printf("FAIL centre %d raw %d: %d outside -127..127\n",
				       centres[c], raw, v);
				break;
			}
			if (v < previous) {
				failures++;
				printf("FAIL centre %d raw %d: %d < previous %d\n",
				       centres[c], raw, v, previous);
				break;
			}
			previous = v;
		}
	}
}

static void test_key_ladder(void)
{
	/* Below each threshold selects that key; the last threshold selects none. */
	CHECK_EQ(gp_decode_key_ladder(0), GP_BTN_KEY(1));
	CHECK_EQ(gp_decode_key_ladder(255), GP_BTN_KEY(1));
	CHECK_EQ(gp_decode_key_ladder(256), GP_BTN_KEY(2));
	CHECK_EQ(gp_decode_key_ladder(596), GP_BTN_KEY(2));
	CHECK_EQ(gp_decode_key_ladder(597), GP_BTN_KEY(3));
	CHECK_EQ(gp_decode_key_ladder(724), GP_BTN_KEY(3));
	CHECK_EQ(gp_decode_key_ladder(725), GP_BTN_KEY(4));
	CHECK_EQ(gp_decode_key_ladder(792), GP_BTN_KEY(4));
	CHECK_EQ(gp_decode_key_ladder(793), GP_BTN_KEY(5));
	CHECK_EQ(gp_decode_key_ladder(835), GP_BTN_KEY(5));
	CHECK_EQ(gp_decode_key_ladder(836), GP_BTN_KEY(6));
	CHECK_EQ(gp_decode_key_ladder(937), GP_BTN_KEY(6));

	/* Idle ladder (and the top of the 10-bit range) reports no key. */
	CHECK_EQ(gp_decode_key_ladder(938), 0);
	CHECK_EQ(gp_decode_key_ladder(1023), 0);
	CHECK_EQ(gp_decode_key_ladder(-1), 0);
}

static void test_key_ladder_from_12bit(void)
{
	/* Both drivers shift the 12-bit reading down, as the V2 driver does inline. */
	CHECK_EQ(gp_decode_key_ladder(0 >> 2), GP_BTN_KEY(1));
	CHECK_EQ(gp_decode_key_ladder(4095 >> 2), 0);
	CHECK_EQ(gp_decode_key_ladder(1024 >> 2), GP_BTN_KEY(2));

	/* Every key maps to exactly one distinct button bit. */
	uint8_t seen = 0;

	for (int i = 0; i < GP_KEY_COUNT; i++) {
		uint8_t bit = GP_BTN_KEY(i + 1);

		CHECK_EQ(__builtin_popcount(bit), 1);
		CHECK_EQ(seen & bit, 0);
		seen |= bit;
	}
	CHECK_EQ(seen & GP_BTN_A, 0);
	CHECK_EQ(seen & GP_BTN_B, 0);
}

static void test_debounce(void)
{
	struct gp_debounce d;

	/* A change is only accepted once it has been seen GP_DEBOUNCE_SAMPLES times. */
	gp_debounce_init(&d, 0);
	CHECK_EQ(d.stable, 0);

	for (int i = 1; i < GP_DEBOUNCE_SAMPLES; i++) {
		CHECK_EQ(gp_debounce_update(&d, GP_BTN_KEY(2)), 0);
	}
	CHECK_EQ(gp_debounce_update(&d, GP_BTN_KEY(2)), GP_BTN_KEY(2));

	/* Release the same way. */
	for (int i = 1; i < GP_DEBOUNCE_SAMPLES; i++) {
		CHECK_EQ(gp_debounce_update(&d, 0), GP_BTN_KEY(2));
	}
	CHECK_EQ(gp_debounce_update(&d, 0), 0);

	/* Two changes in a row restart the count instead of accumulating. */
	gp_debounce_init(&d, 0);
	CHECK_EQ(gp_debounce_update(&d, GP_BTN_A), 0);
	CHECK_EQ(gp_debounce_update(&d, GP_BTN_B), 0);
	CHECK_EQ(gp_debounce_update(&d, GP_BTN_A), 0);
	CHECK_EQ(gp_debounce_update(&d, GP_BTN_A), GP_BTN_A);
	CHECK_EQ(d.stable, GP_BTN_A);
}

static void test_debounce_rejects_flicker(void)
{
	/* A contact that bounces every sample never reaches the stable state. */
	struct gp_debounce d;

	gp_debounce_init(&d, 0);
	for (int i = 0; i < 100; i++) {
		CHECK_EQ(gp_debounce_update(&d, GP_BTN_KEY(3)), 0);
		CHECK_EQ(gp_debounce_update(&d, 0), 0);
	}
	CHECK_EQ(d.stable, 0);
}

static void test_debounce_init_from_current_state(void)
{
	/* Power-on state is stable immediately, so no phantom key press is sent. */
	struct gp_debounce d;

	gp_debounce_init(&d, GP_BTN_A | GP_BTN_KEY(6));
	CHECK_EQ(gp_debounce_update(&d, GP_BTN_A | GP_BTN_KEY(6)), GP_BTN_A | GP_BTN_KEY(6));
	CHECK_EQ(d.count, 0);
}

int main(void)
{
	test_axis_centre_and_deadzone();
	test_axis_full_deflection();
	test_axis_golden_values();
	test_axis_invert();
	test_axis_read_error();
	test_axis_off_centre_and_far_centres();
	test_axis_range_and_monotonicity();
	test_key_ladder();
	test_key_ladder_from_12bit();
	test_debounce();
	test_debounce_rejects_flicker();
	test_debounce_init_from_current_state();

	if (failures) {
		printf("\n%d check(s) failed\n", failures);
		return 1;
	}

	printf("OK: input maths tests passed\n");
	return 0;
}

/*
 * Input driver for the ELECFREAKS Joystick:bit V1 (EF03407) + micro:bit buttons.
 *
 *  P0 (AIN0)  joystick X, ratiometric 0..VDD
 *  P1 (AIN1)  joystick Y, ratiometric 0..VDD
 *  P2 (AIN2)  keys 1..6 on a resistor ladder; key k pulls P2 to (k-1)/k * VDD
 *             (0, 1/2, 2/3, 3/4, 4/5, 5/6), idle ~ VDD.
 *  A / B      micro:bit on-board buttons (active low, external pull-ups)
 *
 * The ladder can only report one key at a time; if several are held the
 * lowest-numbered one wins. That is a hardware limitation of the board.
 */
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "input.h"

LOG_MODULE_REGISTER(input, LOG_LEVEL_INF);

/*
 * All readings are normalised to 12 bits (0..4095 = 0..VDD) so the same maths
 * works on the micro:bit V2 (12-bit SAADC) and V1 (10-bit ADC).
 */
#define ADC_BITS 12
#define ADC_MAX  ((1 << ADC_BITS) - 1)

/* Samples averaged per reading to tame noise (each takes ~45 us). */
#define AXIS_AVG_SAMPLES 4

static const struct adc_dt_spec adc_x = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), joy_x);
static const struct adc_dt_spec adc_y = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), joy_y);
static const struct adc_dt_spec adc_keys = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), keys);

static const struct gpio_dt_spec btn_a = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec btn_b = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);

/*
 * Ladder decision thresholds on a 10-bit scale (0..1023), taken from the
 * official ELECFREAKS MakeCode example for this board. Each is the midpoint
 * between two neighbouring key voltages.
 */
static const uint16_t key_thresholds_10bit[] = {
	256, /* < 256 -> key 1 (~0)    */
	597, /* < 597 -> key 2 (~512)  */
	725, /* < 725 -> key 3 (~683)  */
	793, /* < 793 -> key 4 (~768)  */
	836, /* < 836 -> key 5 (~819)  */
	938, /* < 938 -> key 6 (~853)  */
	     /* else  -> no key (~1023) */
};

static int center_x = ADC_MAX / 2;
static int center_y = ADC_MAX / 2;

static uint8_t btn_stable;
static uint8_t btn_candidate;
static uint8_t btn_candidate_count;

static int adc_sample(const struct adc_dt_spec *spec, int count)
{
	int16_t buf;
	struct adc_sequence seq = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	const int shift = ADC_BITS - spec->resolution;
	const int native_max = (1 << spec->resolution) - 1;
	int sum = 0;
	int err;

	adc_sequence_init_dt(spec, &seq);

	for (int i = 0; i < count; i++) {
		err = adc_read_dt(spec, &seq);
		if (err) {
			LOG_ERR("ADC channel %d read failed (%d)", spec->channel_id, err);
			return -1;
		}
		sum += CLAMP(buf, 0, native_max);
	}

	return (sum / count) << shift;
}

static int8_t scale_axis(int raw, int center, bool invert)
{
	if (raw < 0) {
		return 0; /* read error -> report centred */
	}

	int delta = raw - center;
	int travel = (delta >= 0) ? (ADC_MAX - center) : center;
	int span = travel * (100 - GP_OUTER_SATURATION_PCT) / 100 - GP_DEADZONE;
	int mag = abs(delta) - GP_DEADZONE;

	if (mag <= 0 || span <= 0) {
		return 0;
	}

	int v = MIN(mag * 127 / span, 127);

	if (delta < 0) {
		v = -v;
	}

	return (int8_t)(invert ? -v : v);
}

static uint8_t decode_key_ladder(int raw)
{
	if (raw < 0) {
		return 0;
	}

	int v10 = raw >> 2; /* 12-bit -> 10-bit, same scale as MakeCode */

	for (int i = 0; i < ARRAY_SIZE(key_thresholds_10bit); i++) {
		if (v10 < key_thresholds_10bit[i]) {
			return GP_BTN_KEY(i + 1);
		}
	}

	return 0;
}

static uint8_t read_buttons_raw(void)
{
	uint8_t b = decode_key_ladder(adc_sample(&adc_keys, 1));

	if (gpio_pin_get_dt(&btn_a) > 0) {
		b |= GP_BTN_A;
	}
	if (gpio_pin_get_dt(&btn_b) > 0) {
		b |= GP_BTN_B;
	}

	return b;
}

static int calibrate_axis(const struct adc_dt_spec *spec, const char *name)
{
	int sum = 0;
	const int n = 16;

	for (int i = 0; i < n; i++) {
		int v = adc_sample(spec, AXIS_AVG_SAMPLES);

		if (v < 0) {
			return ADC_MAX / 2;
		}
		sum += v;
		k_msleep(2);
	}

	int c = sum / n;

	/* Stick obviously deflected at power-up? Fall back to the ideal centre. */
	if (c < ADC_MAX / 4 || c > ADC_MAX * 3 / 4) {
		LOG_WRN("%s centre %d looks deflected, using %d", name, c, ADC_MAX / 2);
		c = ADC_MAX / 2;
	}

	LOG_INF("%s centre = %d", name, c);
	return c;
}

int input_init(void)
{
	const struct adc_dt_spec *chans[] = {&adc_x, &adc_y, &adc_keys};
	int err;

	for (int i = 0; i < ARRAY_SIZE(chans); i++) {
		if (!adc_is_ready_dt(chans[i])) {
			LOG_ERR("ADC not ready");
			return -ENODEV;
		}
		err = adc_channel_setup_dt(chans[i]);
		if (err) {
			LOG_ERR("ADC channel %d setup failed (%d)", chans[i]->channel_id, err);
			return err;
		}
	}

	if (!gpio_is_ready_dt(&btn_a) || !gpio_is_ready_dt(&btn_b)) {
		LOG_ERR("Button GPIO not ready");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&btn_a, GPIO_INPUT);
	gpio_pin_configure_dt(&btn_b, GPIO_INPUT);

	center_x = calibrate_axis(&adc_x, "X");
	center_y = calibrate_axis(&adc_y, "Y");

	btn_stable = read_buttons_raw();
	btn_candidate = btn_stable;

	return 0;
}

void input_read(struct gamepad_report *out)
{
	uint8_t raw = read_buttons_raw();

	/* Debounce: accept a new button set once it has been seen N times in a row */
	if (raw != btn_candidate) {
		btn_candidate = raw;
		btn_candidate_count = 1;
	} else if (btn_candidate != btn_stable && ++btn_candidate_count >= GP_DEBOUNCE_SAMPLES) {
		btn_stable = btn_candidate;
	}

	out->buttons = btn_stable;
	out->x = scale_axis(adc_sample(&adc_x, AXIS_AVG_SAMPLES), center_x, GP_INVERT_X);
	out->y = scale_axis(adc_sample(&adc_y, AXIS_AVG_SAMPLES), center_y, GP_INVERT_Y);
}

bool input_a_and_b_held(void)
{
	return gpio_pin_get_dt(&btn_a) > 0 && gpio_pin_get_dt(&btn_b) > 0;
}

/*
 * ELECFREAKS Joystick:bit V1 (EF03407) + micro:bit V1 buttons.
 *
 *  P0 (Arduino pin 0, AIN4)  joystick X, ratiometric 0..VDD
 *  P1 (Arduino pin 1, AIN3)  joystick Y, ratiometric 0..VDD
 *  P2 (Arduino pin 2, AIN2)  keys 1..6 on a resistor ladder; key k pulls P2
 *                            to (k-1)/k * VDD, idle ~ VDD
 *  A / B (pins 5 / 11)       on-board buttons, active low
 *
 * The core's analogRead() uses VDD/3 reference with 1/3 input prescaling,
 * i.e. the same ratiometric 10-bit scale as MakeCode's analogReadPin().
 * The ladder only reports one key at a time.
 */
#include <Arduino.h>
#include <stdlib.h>

#include "v1_input.h"

#define PIN_JOY_X 0
#define PIN_JOY_Y 1
#define PIN_KEYS  2

#define ADC_MAX 4095 /* readings normalised to 12 bits */

#define AXIS_AVG_SAMPLES 4

/* Ladder thresholds (10-bit), from the official ELECFREAKS MakeCode sample */
static const uint16_t key_thresholds_10bit[] = {256, 597, 725, 793, 836, 938};

static uint8_t btn_stable;
static uint8_t btn_candidate;
static uint8_t btn_candidate_count;

static int read_12bit(int pin, int count)
{
	int sum = 0;

	for (int i = 0; i < count; i++) {
		sum += analogRead(pin); /* 0..1023 */
	}

	return (sum / count) << 2;
}

static int8_t scale_axis(int raw, int center, bool invert)
{
	int delta = raw - center;
	int travel = (delta >= 0) ? (ADC_MAX - center) : center;
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

static uint8_t decode_key_ladder(int raw10)
{
	for (unsigned i = 0; i < sizeof(key_thresholds_10bit) / sizeof(key_thresholds_10bit[0]); i++) {
		if (raw10 < key_thresholds_10bit[i]) {
			return GP_BTN_KEY(i + 1);
		}
	}

	return 0;
}

static int last_p2;           /* latest raw ladder reading (10-bit), for the log */
static bool keys_connected(void); /* P2 passes its contact self-test */

static uint8_t read_buttons_raw(void)
{
	last_p2 = analogRead(PIN_KEYS);

	uint8_t b = keys_connected() ? decode_key_ladder(last_p2) : 0;

	if (digitalRead(PIN_BUTTON_A) == LOW) {
		b |= GP_BTN_A;
	}
	if (digitalRead(PIN_BUTTON_B) == LOW) {
		b |= GP_BTN_B;
	}

	return b;
}

static int calibrate_axis(int pin)
{
	const int n = 16;
	int sum = 0;

	for (int i = 0; i < n; i++) {
		sum += read_12bit(pin, AXIS_AVG_SAMPLES);
		delay(2);
	}

	int c = sum / n;

	/* Stick deflected while calibrating? Use the ideal centre instead. */
	if (abs(c - ADC_MAX / 2) > ADC_MAX / 10) {
		c = ADC_MAX / 2;
	}

	return c;
}

/*
 * Connection self-test: read the pin with the internal pull-down, then the
 * pull-up (~13k each). A pin driven by the joystick pot / key ladder stays
 * near its own voltage; an unconnected pin (bad edge-connector contact)
 * follows the pulls all the way to ~0 and ~VDD.
 */
static bool probe_connected(const char *name, int pin, bool verbose)
{
	uint32_t gpio = g_ADigitalPinMap[pin];
	uint32_t saved = NRF_GPIO->PIN_CNF[gpio];
	int r[2];
	const uint32_t pulls[2] = {GPIO_PIN_CNF_PULL_Pulldown, GPIO_PIN_CNF_PULL_Pullup};

	for (int i = 0; i < 2; i++) {
		NRF_GPIO->PIN_CNF[gpio] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
					  (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
					  (pulls[i] << GPIO_PIN_CNF_PULL_Pos);
		delayMicroseconds(100);
		r[i] = analogRead(pin);
	}
	NRF_GPIO->PIN_CNF[gpio] = saved;
	delayMicroseconds(100);

	bool ok = !(r[0] < 60 && r[1] > 960);

	if (verbose) {
		Serial.print(name);
		Serial.print(" pull-down/up test: ");
		Serial.print(r[0]);
		Serial.print(" / ");
		Serial.print(r[1]);
		Serial.println(ok ? " -> connected"
				  : " -> NOT CONNECTED: reseat the micro:bit / clean its edge contacts");
	}

	return ok;
}

/*
 * A pin whose contact is watched on every update. One failed self-test drops
 * it at once; it is only trusted again after RECONNECT_MS of unbroken contact,
 * so a loose contact never leaks floating-pin junk (a floating P2 reads like
 * "key 1 held", a floating P0/P1 like a pushed stick).
 */
struct pin_watch {
	const char *name;
	int pin;
	bool connected;
	uint32_t good_since; /* while disconnected: when the probes started passing (0 = not) */
};

#define RECONNECT_MS 1000

/* Returns true when 'connected' changed. */
static bool pin_watch_update(struct pin_watch *w)
{
	bool ok = probe_connected(w->name, w->pin, false);
	uint32_t now = millis();

	if (ok == w->connected) {
		w->good_since = 0;
		return false;
	}
	if (ok) {
		if (w->good_since == 0) {
			w->good_since = now | 1; /* never 0 */
			return false;
		}
		if (now - w->good_since < RECONNECT_MS) {
			return false;
		}
	}
	w->good_since = 0;
	w->connected = ok;
	Serial.print(w->name);
	Serial.println(ok ? " contact OK" : " contact LOST -> ignored until it holds for 1 s");
	return true;
}

static struct pin_watch keys_watch = {"P2 (keys)", PIN_KEYS, true, 0};

struct axis {
	struct pin_watch w;
	bool invert;
	int center;
	int raw;      /* latest 12-bit reading */
	int8_t value; /* latest scaled value, 0 while disconnected */
};

static struct axis axes[2] = {
	{{"P0 (stick X)", PIN_JOY_X, true, 0}, GP_INVERT_X, ADC_MAX / 2, ADC_MAX / 2, 0},
	{{"P1 (stick Y)", PIN_JOY_Y, true, 0}, GP_INVERT_Y, ADC_MAX / 2, ADC_MAX / 2, 0},
};

static void axis_init(struct axis *a)
{
	a->w.connected = probe_connected(a->w.name, a->w.pin, true);
	a->center = calibrate_axis(a->w.pin);
}

static void axis_check(struct axis *a)
{
	if (pin_watch_update(&a->w) && a->w.connected) {
		a->center = calibrate_axis(a->w.pin);
		Serial.print(a->w.name);
		Serial.print(" centre=");
		Serial.println(a->center);
	}
}

static void axis_sample(struct axis *a)
{
	a->raw = read_12bit(a->w.pin, AXIS_AVG_SAMPLES);
	a->value = a->w.connected ? scale_axis(a->raw, a->center, a->invert) : 0;
}

void input_init(void)
{
	pinMode(PIN_BUTTON_A, INPUT); /* external pull-ups on the micro:bit */
	pinMode(PIN_BUTTON_B, INPUT);

	axis_init(&axes[STICK_X]);
	axis_init(&axes[STICK_Y]);
	keys_watch.connected = probe_connected(keys_watch.name, keys_watch.pin, true);

	Serial.print("Stick centre X=");
	Serial.print(axes[STICK_X].center);
	Serial.print(" Y=");
	Serial.println(axes[STICK_Y].center);

	btn_stable = read_buttons_raw();
	btn_candidate = btn_stable;

	Serial.print("Key ladder idle P2=");
	Serial.println(last_p2);
}

/* Serial diagnostics: raw P2 whenever it moves, and every debounced change. */
static void log_p2(void)
{
	static int logged = -100;
	static uint32_t logged_ms;
	uint32_t now = millis();

	if (keys_watch.connected && abs(last_p2 - logged) > 12 && now - logged_ms >= 40) {
		logged = last_p2;
		logged_ms = now;
		Serial.print("P2=");
		Serial.print(last_p2);
		Serial.print(" -> key ");
		uint8_t k = decode_key_ladder(last_p2);
		Serial.println(k ? 1 + __builtin_ctz(k) : 0);
	}
}

static void log_stick(void)
{
	static int lx = -1000, ly = -1000;
	static uint32_t logged_ms;
	uint32_t now = millis();
	const struct axis *x = &axes[STICK_X], *y = &axes[STICK_Y];

	if ((abs(x->raw - lx) > 150 || abs(y->raw - ly) > 150) && now - logged_ms >= 100) {
		lx = x->raw;
		ly = y->raw;
		logged_ms = now;
		Serial.print("Stick raw X=");
		Serial.print(x->raw);
		Serial.print(" Y=");
		Serial.print(y->raw);
		Serial.print(" -> x=");
		Serial.print(x->value);
		Serial.print(" y=");
		Serial.println(y->value);
	}
}

static void log_buttons(uint8_t b)
{
	static const char *const names[8] = {"1", "2", "3", "4", "5", "6", "A", "B"};

	Serial.print("Buttons:");
	for (int i = 0; i < 8; i++) {
		if (b & GP_BIT(i)) {
			Serial.print(' ');
			Serial.print(names[i]);
		}
	}
	Serial.println(b ? "" : " none");
}

static bool keys_connected(void)
{
	return keys_watch.connected;
}

void input_update(void)
{
	pin_watch_update(&keys_watch);

	uint8_t raw = read_buttons_raw();

	/* Debounce: accept a new button set once seen N times in a row */
	if (raw != btn_candidate) {
		btn_candidate = raw;
		btn_candidate_count = 1;
	} else if (btn_candidate != btn_stable && ++btn_candidate_count >= GP_DEBOUNCE_SAMPLES) {
		btn_stable = btn_candidate;
		log_buttons(btn_stable);
	}
	log_p2();

	axis_check(&axes[STICK_X]);
	axis_check(&axes[STICK_Y]);
	axis_sample(&axes[STICK_X]);
	axis_sample(&axes[STICK_Y]);
	log_stick();
}

uint8_t input_buttons(void)
{
	return btn_stable;
}

int8_t input_stick(enum stick_axis a)
{
	return axes[a].value;
}

bool input_stick_connected(enum stick_axis a)
{
	return axes[a].w.connected;
}

int input_analog_raw(int pin)
{
	return (pin >= 0 && pin <= 2) ? (int)analogRead(pin) : 0;
}

bool input_a_and_b_held(void)
{
	return digitalRead(PIN_BUTTON_A) == LOW && digitalRead(PIN_BUTTON_B) == LOW;
}

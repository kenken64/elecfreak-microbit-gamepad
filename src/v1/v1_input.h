#ifndef V1_INPUT_H_
#define V1_INPUT_H_

#include "gamepad.h"

enum stick_axis {
	STICK_X = 0, /* P0: left/right */
	STICK_Y = 1, /* P1: up/down    */
};

/* Configure pins, run the pin self-test and calibrate the stick centre
 * (keep the stick untouched). */
void input_init(void);

/* Sample keys, buttons and stick once (call every poll interval). */
void input_update(void);

/* Debounced buttons: GP_BTN_KEY(1..6), GP_BTN_A, GP_BTN_B bits. */
uint8_t input_buttons(void);

/* Stick position -127..127 (left/up negative); 0 while its pin isn't connected. */
int8_t input_stick(enum stick_axis a);

/* False while the axis pin fails the connection self-test (loose contact). */
bool input_stick_connected(enum stick_axis a);

/* Raw 10-bit reading of P0, P1 or P2 (0..1023). */
int input_analog_raw(int pin);

/* Raw (undebounced) state of micro:bit buttons A and B together. */
bool input_a_and_b_held(void);

#endif /* V1_INPUT_H_ */

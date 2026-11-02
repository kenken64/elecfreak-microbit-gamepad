#ifndef INPUT_H_
#define INPUT_H_

#include <stdbool.h>
#include "gamepad.h"

/* Configure ADC + buttons and calibrate the stick centre (keep it untouched). */
int input_init(void);

/* Sample everything once (call every GP_POLL_INTERVAL_MS) into *out. */
void input_read(struct gamepad_report *out);

/* Raw (undebounced) state of micro:bit buttons A and B together. */
bool input_a_and_b_held(void);

#endif /* INPUT_H_ */

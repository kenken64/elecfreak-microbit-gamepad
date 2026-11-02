/*
 * Gamepad runtime: the functions the block editor's generated main.cpp calls.
 *
 * Each function matches one block in tools/blocks_editor/. The heavy lifting
 * (Bluetooth, LED screen, pin reading) lives in v1_ble.cpp, v1_display.cpp
 * and v1_input.cpp; this layer only exposes it in block-sized pieces.
 */
#ifndef GP_RUNTIME_H_
#define GP_RUNTIME_H_

#include <stdint.h>

enum gp_axis {
	GP_AXIS_X = 0, /* stick left/right, gamepad X */
	GP_AXIS_Y = 1, /* stick up/down,    gamepad Y */
};

enum gp_icon {
	GP_ICON_CROSS,  /* big X           */
	GP_ICON_TICK,   /* tick            */
	GP_ICON_SQUARE, /* hollow square   */
	GP_ICON_BANG,   /* exclamation     */
};

/* ---- on start ------------------------------------------------------------ */

/* Serial log, LED screen and Joystick:bit inputs (pin self-test, stick
 * centre). Always first: the LED timer must start before Bluetooth. */
void gp_boot(void);

/* True if micro:bit A and B were both held when gp_boot() ran. */
bool gp_ab_held_at_start(void);

/* Start the Bluetooth gamepad and advertise as 'name'. Shows "!" and stops
 * if the Bluetooth stack fails to start. */
void gp_bluetooth_begin(const char *name);

void gp_forget_all_pairings(void);
void gp_show_icon(enum gp_icon icon);

/* Keep Bluetooth running until A and B are released, then 0.5 s more. */
void gp_wait_ab_released(void);

/* ---- every N ms ---------------------------------------------------------- */

/* Pump Bluetooth events. Call on every pass of loop(). */
void gp_poll(void);

/* True once every interval_ms (call once per loop()). */
bool gp_every(uint32_t interval_ms);

/* Sample keys, buttons and stick, and start an empty gamepad report. */
void gp_inputs_update(void);

bool gp_key_pressed(int key);       /* Joystick:bit key 1..6 (debounced) */
bool gp_button_pressed(char ab);    /* micro:bit 'A' or 'B' (debounced)  */
int gp_stick(enum gp_axis axis);    /* -127..127, 0 while not connected  */
bool gp_stick_connected(enum gp_axis axis);
int gp_analog_read(int pin);        /* P0..P2 raw, 0..1023               */

void gp_press(int button);                       /* gamepad button 1..8 */
void gp_set_axis(enum gp_axis axis, int value);  /* clamped to -127..127 */

/* Send the report to the PC, only when it changed and the PC is ready. */
void gp_report_send(void);

/* LED status: rings (waiting), square (pairing), tick (ready), then the
 * pressed key or a dot following the gamepad's X/Y. */
void gp_status_show(void);

#endif /* GP_RUNTIME_H_ */

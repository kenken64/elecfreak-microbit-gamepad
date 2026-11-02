#ifndef V1_BLE_H_
#define V1_BLE_H_

#include <stdint.h>
#include "gamepad.h"

enum ble_link {
	LINK_ADVERTISING, /* waiting for a PC (pairing mode)              */
	LINK_PAIRING,     /* connected, securing link / enabling reports  */
	LINK_READY,       /* Windows is receiving input reports           */
};

/*
 * Enable the S130 SoftDevice, build the GATT table and start advertising as
 * 'name' (up to 29 bytes; NULL or "" = "micro:bit Gamepad").
 */
bool ble_init(const char *name);

/* Pump SoftDevice events and pending flash writes. Call every loop. */
void ble_poll(void);

enum ble_link ble_link_state(void);

/* Update the Report characteristic and notify the PC. False = retry later. */
bool ble_send(const struct gamepad_report &report);

/* Forget every bonded PC (written to flash in the background). */
void ble_forget_all_bonds(void);

#endif /* V1_BLE_H_ */

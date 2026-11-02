#ifndef UI_H_
#define UI_H_

#include "gamepad.h"

enum ui_link {
	UI_ADVERTISING, /* waiting for Windows: animated "radar" rings          */
	UI_PAIRING,     /* connected, securing link / waiting for subscription  */
	UI_READY,       /* reports flowing: tick, then live joystick dot        */
};

void ui_init(void);

/* Call every poll cycle. Inputs that are active are always previewed. */
void ui_update(enum ui_link link, const struct gamepad_report *report);

/* Big "X" shown after the bond list was wiped. */
void ui_show_bonds_cleared(void);

#endif /* UI_H_ */

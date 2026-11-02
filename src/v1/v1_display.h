#ifndef V1_DISPLAY_H_
#define V1_DISPLAY_H_

#include <stdint.h>

/* 5x5 image: row[y] bit x (bit 0 = leftmost LED) */
struct image5 {
	uint8_t row[5];
};

/*
 * Starts the LED refresh timer (TIMER2). Call BEFORE the SoftDevice is
 * enabled: afterwards the application must not touch the NVIC directly.
 */
void display_init(void);

void display_show(const struct image5 &img);
void display_clear(void);

/* Glyph for '1'..'6', 'A', 'B' (anything else -> blank) */
const struct image5 &display_glyph(char c);

#endif /* V1_DISPLAY_H_ */

/*
 * micro:bit V1 LED matrix driver.
 *
 * The 25 LEDs are wired as 3 rows (P0.13-P0.15, active high) x 9 columns
 * (P0.04-P0.12, active low). TIMER2 scans one row every 2 ms (~166 Hz frame).
 * TIMER0 belongs to the SoftDevice; TIMER1 is used by the core's analogWrite.
 */
#include <Arduino.h>
#include <string.h>

#include "v1_display.h"

#define ROW_FIRST_PIN 13
#define COL_FIRST_PIN 4
#define ROW_MASK      (0x7UL << ROW_FIRST_PIN)
#define COL_MASK      (0x1FFUL << COL_FIRST_PIN)

/* (row << 4) | col for each LED, indexed y * 5 + x (from the board schematic) */
static const uint8_t pixel_map[25] = {
	0x00, 0x13, 0x01, 0x14, 0x02,
	0x23, 0x24, 0x25, 0x26, 0x27,
	0x11, 0x08, 0x12, 0x28, 0x10,
	0x07, 0x06, 0x05, 0x04, 0x03,
	0x22, 0x16, 0x20, 0x15, 0x21,
};

/* Columns to light per matrix row (bit n = column n) */
static volatile uint16_t frame[3];
static volatile uint8_t scan_row;

static const struct image5 glyphs[] = {
	{{0x04, 0x06, 0x04, 0x04, 0x0E}}, /* 1 */
	{{0x0F, 0x10, 0x0E, 0x01, 0x1F}}, /* 2 */
	{{0x0F, 0x10, 0x0C, 0x10, 0x0F}}, /* 3 */
	{{0x0C, 0x0A, 0x09, 0x1F, 0x08}}, /* 4 */
	{{0x1F, 0x01, 0x0F, 0x10, 0x0F}}, /* 5 */
	{{0x0C, 0x02, 0x0F, 0x11, 0x0E}}, /* 6 */
	{{0x06, 0x09, 0x0F, 0x09, 0x09}}, /* A */
	{{0x07, 0x09, 0x07, 0x09, 0x07}}, /* B */
};
static const struct image5 blank = {{0, 0, 0, 0, 0}};

extern "C" void TIMER2_IRQHandler(void)
{
	if (!NRF_TIMER2->EVENTS_COMPARE[0]) {
		return;
	}
	NRF_TIMER2->EVENTS_COMPARE[0] = 0;

	NRF_GPIO->OUTCLR = ROW_MASK; /* rows off */
	NRF_GPIO->OUTSET = COL_MASK; /* columns off (active low) */

	scan_row = (scan_row + 1) % 3;

	NRF_GPIO->OUTCLR = ((uint32_t)frame[scan_row] << COL_FIRST_PIN) & COL_MASK;
	NRF_GPIO->OUTSET = 1UL << (ROW_FIRST_PIN + scan_row);
}

void display_init(void)
{
	NRF_GPIO->OUTCLR = ROW_MASK;
	NRF_GPIO->OUTSET = COL_MASK;
	NRF_GPIO->DIRSET = ROW_MASK | COL_MASK;

	NRF_TIMER2->TASKS_STOP = 1;
	NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer;
	NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_16Bit;
	NRF_TIMER2->PRESCALER = 4; /* 16 MHz / 2^4 = 1 MHz */
	NRF_TIMER2->CC[0] = 2000;  /* 2 ms per row */
	NRF_TIMER2->SHORTS = TIMER_SHORTS_COMPARE0_CLEAR_Msk;
	NRF_TIMER2->INTENSET = TIMER_INTENSET_COMPARE0_Msk;

	/* Priority 3 = application low; 0 and 2 are reserved by the SoftDevice */
	NVIC_SetPriority(TIMER2_IRQn, 3);
	NVIC_ClearPendingIRQ(TIMER2_IRQn);
	NVIC_EnableIRQ(TIMER2_IRQn);

	NRF_TIMER2->TASKS_CLEAR = 1;
	NRF_TIMER2->TASKS_START = 1;
}

void display_show(const struct image5 &img)
{
	uint16_t f[3] = {0, 0, 0};

	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 5; x++) {
			if (img.row[y] & (1U << x)) {
				uint8_t m = pixel_map[y * 5 + x];

				f[m >> 4] |= 1U << (m & 0x0F);
			}
		}
	}

	for (int r = 0; r < 3; r++) {
		frame[r] = f[r];
	}
}

void display_clear(void)
{
	display_show(blank);
}

const struct image5 &display_glyph(char c)
{
	if (c >= '1' && c <= '6') {
		return glyphs[c - '1'];
	}
	if (c == 'A') {
		return glyphs[6];
	}
	if (c == 'B') {
		return glyphs[7];
	}
	return blank;
}

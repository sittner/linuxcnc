#ifndef _KEYPAD_H
#define _KEYPAD_H

#define KEY_SCAN_DIVIDER  50

#define KEY_COL_PORT      PORTD
#define KEY_COL_DDR       DDRD
#define KEY_COL_MASK      (_BV(4) | _BV(5) | _BV(6))
#define KEY_COL_FIRST_PIN 4
#define KEY_COL_LAST_PIN  6

#define KEY_ROW_INP       PIND
#define KEY_ROW_MASK      (_BV(0) | _BV(1) | _BV(2) | _BV(3))
#define KEY_ROW_FIRST_PIN 0
#define KEY_ROW_LAST_PIN  3

#define KEY_AUX0_PORT     PORTC
#define KEY_AUX0_INP      PINC
#define KEY_AUX0_PIN      6
#define KEY_AUX0_KEYNUM   12

#define KEY_AUX1_PORT     PORTC
#define KEY_AUX1_INP      PINC
#define KEY_AUX1_PIN      7
#define KEY_AUX1_KEYNUM   13

extern void key_init(void);
extern void key_timer(void);
extern uint16_t key_get(void);

#endif


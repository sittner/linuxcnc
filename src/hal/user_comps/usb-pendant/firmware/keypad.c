#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#include "keypad.h"

#define KEY_COL_SELECT(x) { KEY_COL_PORT = (KEY_COL_PORT | KEY_COL_MASK) & ~(x); }

#define KEY_ROW_SHIFT (KEY_ROW_LAST_PIN - KEY_ROW_FIRST_PIN + 1)

static volatile uint8_t key_scan_timer;
static volatile uint8_t key_scan_col;
static volatile uint16_t key_scan_buf;
static volatile uint16_t key_scan_res;

void key_init(void) {
  // initialize key scan outputs
  KEY_COL_DDR |= KEY_COL_MASK;
  KEY_COL_SELECT(_BV(KEY_COL_FIRST_PIN));

  // enable pullups for aux inputs
  KEY_AUX0_PORT |= _BV(KEY_AUX0_PIN);
  KEY_AUX1_PORT |= _BV(KEY_AUX1_PIN);

  // init vars
  key_scan_timer = 0;
  key_scan_col = _BV(KEY_COL_FIRST_PIN);
  key_scan_buf = 0;
  key_scan_res = 0;
}

void key_timer(void) {
  // check scan divider
  key_scan_timer++;
  if (key_scan_timer < KEY_SCAN_DIVIDER) {
    return;
  }
  key_scan_timer = 0;

  // read inputs
  key_scan_buf = (key_scan_buf << KEY_ROW_SHIFT) | ((~KEY_ROW_INP & KEY_ROW_MASK) >> KEY_ROW_FIRST_PIN);

  // check for new cycle
  if (key_scan_col == _BV(KEY_COL_LAST_PIN)) {
    // read aux keys
    if (!(KEY_AUX0_INP & _BV(KEY_AUX0_PIN))) {
      key_scan_buf |= _BV(KEY_AUX0_KEYNUM);
    }
    if (!(KEY_AUX1_INP & _BV(KEY_AUX1_PIN))) {
      key_scan_buf |= _BV(KEY_AUX1_KEYNUM);
    }

    // update result
    key_scan_res = key_scan_buf;
    key_scan_buf = 0;

    // return to first column
    key_scan_col = _BV(KEY_COL_FIRST_PIN);

  } else {
    // next column
    key_scan_col <<= 1;
  }

  // update col output
  KEY_COL_SELECT(key_scan_col);
}

uint16_t key_get(void) {
  uint8_t sreg = SREG;
  cli();
  uint16_t val = key_scan_res;
  SREG = sreg;
  return val;
}


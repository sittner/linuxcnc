#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>

#include "encoder.h"

const int8_t enc_table[] PROGMEM = { 0, 0, -1, 0, 0, 0, 0, +1, +1, 0, 0, 0, 0, -1, 0, 0 };

static volatile uint8_t enc_state;
static volatile enc_data_t enc_data;
static volatile bool enc_changed;

void enc_init(void) {
  enc_state = 0;
  enc_timer(0);

  memset((void *)&enc_data, 0, sizeof(enc_data));
  enc_changed = false;
}

void enc_timer(uint16_t app_time) {
  enc_state = (enc_state << 2) & 0x0f;

  if (ENC_A_INP & _BV(ENC_A_PIN)) {
    enc_state |= _BV(0);
  }
  if (ENC_B_INP & _BV(ENC_B_PIN)) {
    enc_state |= _BV(1);
  }

  int8_t step = pgm_read_byte(&enc_table[enc_state]);
  enc_data.pos += step;
  if (step != 0) {
    enc_data.time = app_time;
    enc_changed = true;
  }
}

bool enc_get(enc_data_t *data) {
  uint8_t sreg = SREG;
  cli();

  *data = enc_data;
  bool changed = enc_changed;
  enc_changed = false;

  SREG = sreg;

  return changed;
}


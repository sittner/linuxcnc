#ifndef _ENCODER_H
#define _ENCODER_H

#include <stdint.h>
#include <stdbool.h>

#define ENC_A_INP PINC
#define ENC_A_PIN 4

#define ENC_B_INP PINC
#define ENC_B_PIN 5

typedef struct __attribute__ ((__packed__)) {
  uint16_t time;
  int16_t pos;
} enc_data_t;

extern void enc_init(void);
extern void enc_timer(uint16_t app_time);
extern bool enc_get(enc_data_t *data);

#endif


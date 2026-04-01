#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/pgmspace.h>

#include "keypad.h"
#include "encoder.h"
#include "usbhid.h"

#define SEND_FORE_TIMEOUT 5000

static volatile uint16_t app_time = 0; 
static volatile uint16_t send_force_timer = 0; 

static uint16_t last_buttons = 0; 

SIGNAL(TIMER0_COMPA_vect)
{
  enc_timer(app_time);
  key_timer();

  app_time++;
  if (send_force_timer > 0) {
    send_force_timer--;
  }
};

bool usbhid_create_report_callback(USBHID_Report_Data_In_t* data) {

  bool send = enc_get(&data->enc);

  uint16_t buttons = key_get();
  if (buttons != last_buttons) {
    last_buttons = buttons;
    send = true;
  }
  data->buttons = buttons;

  uint8_t sreg = SREG;
  cli();

  data->app_time = app_time;

  if (send_force_timer == 0) {
    send = true;
  }
  if (send) {
    send_force_timer = SEND_FORE_TIMEOUT;
  }

  SREG = sreg;

  return send;
}

int main(void) {
  // Disable watchdog if enabled by bootloader/fuses
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

  // Disable clock division
  clock_prescale_set(clock_div_1);

  // initialize USB
  usbhid_init();

  // initialize keyboard
  key_init();

  // initialize encoder
  enc_init();

  // setup pullups for unused pins
  PORTB |= _BV(0) | _BV(1) | _BV(2) | _BV(3) | _BV(4) | _BV(5) | _BV(6) | _BV(7);
  PORTC |= _BV(2);
  
  // init timer 0 (10kHz @ F_OSC = 16MHz)
  TCCR0A |= _BV(WGM01);
  TCCR0B |= _BV(CS01);
  OCR0A |= 200;
  TIMSK0 |= _BV(OCIE0A);

  // start irqs
  sei();

  while (1) {
    // call usb task
    usbhid_task();
  }
}


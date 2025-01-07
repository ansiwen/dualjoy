/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025, Sven Anderson (https://github.com/ansiwen)
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "bsp/board_api.h"
#include "tusb.h"

#include "dualjoy.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum {
  BLINK_OFF = 0,
  BLINK_NOT_MOUNTED = 250 * 1000,
  BLINK_SUSPENDED = 2500 * 1000,
  DEBOUNCE_TIMEOUT_US = 20 * 1000,
  EVENT_FLASH_US = 30 * 1000,
  BLINK_FAST_US = 50 * 1000,
  MAX_DELAY_US = BLINK_SUSPENDED, // must be set to the largest wait interval
};

#define ARRAY_SIZE(_arr) ( sizeof(_arr) / sizeof(_arr[0]) )

static uint32_t blink_interval_us = BLINK_NOT_MOUNTED;
static uint32_t blink_timeout_us = 0;
static bool led_state = false;


// ----------------------------- JOYSTICK BEGIN ----------------------------

//DB9-connector:
//C64/Sega Mastersystem: 1 = up, 2 = down, 3 = left, 4 = right, 6 = btn1, 8 = gnd, 9 = btn2
//MSX: 1 = up, 2 = down, 3 = left, 4 = right, 6 = btn1, 7 = btn2, 8 = gnd

enum gpio {
  J1_UP = 5,
  J1_DOWN = 4,
  J1_LEFT = 3,
  J1_RIGHT = 2,
  J1_BTN = 27,

  J2_UP = 9,
  J2_DOWN = 8,
  J2_LEFT = 7,
  J2_RIGHT = 6,
  J2_BTN = 26,
};

#define J1_MASK (1<<J1_UP | 1<<J1_DOWN | 1<<J1_LEFT | 1<<J1_RIGHT | 1<<J1_BTN)
#define J2_MASK (1<<J2_UP | 1<<J2_DOWN | 1<<J2_LEFT | 1<<J2_RIGHT | 1<<J2_BTN)

#define PIN_MASK (J1_MASK | J2_MASK)

enum pin {
  UP = 0,
  DOWN,
  LEFT,
  RIGHT,
  BTN,
  PIN_NUM,
  TOTAL_PIN_NUM = PIN_NUM * 2,
};

static const uint8_t inputGPIOs[TOTAL_PIN_NUM] = {
  J1_UP, J1_DOWN, J1_LEFT, J1_RIGHT, J1_BTN,
  J2_UP, J2_DOWN, J2_LEFT, J2_RIGHT, J2_BTN
};

static const uint32_t inputMasks[TOTAL_PIN_NUM] = {
  1 << J1_UP, 1 << J1_DOWN, 1 << J1_LEFT, 1 << J1_RIGHT, 1 << J1_BTN,
  1 << J2_UP, 1 << J2_DOWN, 1 << J2_LEFT, 1 << J2_RIGHT, 1 << J2_BTN
};

static const uint8_t gpio2pin[32] = {
  [J1_UP] = UP,
  [J1_DOWN] = DOWN,
  [J1_LEFT] = LEFT,
  [J1_RIGHT] = RIGHT,
  [J1_BTN] = BTN,
  [J2_UP] = PIN_NUM+UP,
  [J2_DOWN] = PIN_NUM+DOWN,
  [J2_LEFT] = PIN_NUM+LEFT,
  [J2_RIGHT] = PIN_NUM+RIGHT,
  [J2_BTN] = PIN_NUM+BTN,
};

static uint32_t pin_states = 0;
static uint32_t pin_timeouts[TOTAL_PIN_NUM] = { 0 };

static inline uint8_t states2direction(const uint32_t mask[PIN_NUM]) {
  if (pin_states & mask[UP]) {
    if (pin_states & mask[RIGHT])
      return 2;  // NE
    else if (pin_states & mask[LEFT])
      return 8;  // NW
    else
      return 1;  // N
  }
  else if (pin_states & mask[DOWN]) {
    if (pin_states & mask[RIGHT])
      return 4;  // SE
    else if (pin_states & mask[LEFT])
      return 6;  // SW
    else
      return 5;  // S
  }
  else if (pin_states & mask[RIGHT])
    return 3;  // E
  else if (pin_states & mask[LEFT])
    return 7;  // W
  else
    return 0;  // Center
}

static inline bool reached(const uint32_t t) {
  // overflow safe time comparison
  return t == 0 || t-time_us_32() > MAX_DELAY_US;
}

static inline uint32_t time_after_us(uint32_t us) {
  if (us > MAX_DELAY_US) us = MAX_DELAY_US;
  return (time_us_32() + us) | 1; // make sure it's never 0 after overflow
}

static inline void led_flash() {
  led_state = !led_state;
  board_led_write(led_state);
  blink_interval_us = BLINK_OFF;
  blink_timeout_us = time_after_us(EVENT_FLASH_US);
}

static inline void led_set_blink_mode(const uint32_t interval) {
  blink_timeout_us = 0;
  if (interval == BLINK_OFF) {
    led_state = false;
    board_led_write(false);
  }
  blink_interval_us = interval;
}

static inline void led_blink_fast_until(const uint32_t timeout) {
  blink_timeout_us = timeout;
  blink_interval_us = BLINK_FAST_US;
}


typedef struct {
  uint8_t direction;
  uint8_t buttons;
} report;

#define REPORT_EQUAL(a, b) (a.direction == b.direction && a.buttons == b.buttons)
#define REPORT_COPY(a, b) do { a.direction = b.direction; a.buttons = b.buttons; } while (0)

static inline void send_states() {
  static report last_r1 = {0, 0};
  static report last_r2 = {0, 0};
  static report sent_r1 = {0, 0};
  static report sent_r2 = {0, 0};
  static uint32_t last_states = 0;

  const uint32_t changes = last_states ^ pin_states;

  if (changes) {
    if (changes & J1_MASK) {
      last_r1.direction = states2direction(&inputMasks[0]);
      last_r1.buttons = (pin_states & inputMasks[BTN]) ? 1 : 0;
    }
    if (changes & J2_MASK) {
      last_r2.direction = states2direction(&inputMasks[PIN_NUM]);
      last_r2.buttons = (pin_states & inputMasks[PIN_NUM+BTN]) ? 1 : 0;
    }
    last_states = pin_states;
  }

  if (!REPORT_EQUAL(sent_r1, last_r1)) {
    trace("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX J1: %d %x\n", last_r1.direction, last_r1.buttons);
    if (tud_hid_n_report(0, JOYSTICK_REPORT_ID, &last_r1, sizeof(report))) {
      led_flash();
      REPORT_COPY(sent_r1, last_r1);
    } else {
      trace("###################################### failed to send report\n");
    }
  }

  if (!REPORT_EQUAL(sent_r2, last_r2)) {
    trace("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX J2: %d %x\n", last_r2.direction, last_r2.buttons);
    if (tud_hid_n_report(1, JOYSTICK2_REPORT_ID, &last_r2, sizeof(report))) {
      led_flash();
      REPORT_COPY(sent_r2, last_r2);
    } else {
      trace("###################################### failed to send report\n");
    }
  }
}

static inline uint8_t fast_log2_of_pow2(const uint32_t x) {
    static const uint32_t deBruijnSequence = 0x077CB531U;
    static const uint8_t lookupTable[32] = {
        0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
    };
    // Multiply by de Bruijn sequence
    return lookupTable[((x * deBruijnSequence) >> 27)];
}

static inline void update_states_task() {
  const uint32_t pins = (~gpio_get_all()) & PIN_MASK;
  uint32_t changes = pins ^ pin_states;

  // beware, here comes some serious over-engineering
  while (changes) {
    trace("%s pins: %.32b pin_states: %.32b changes: %.32b\n", __func__, pins, pin_states, changes);
    const uint32_t mask = changes & -changes; // isolate least significant changed bit
    changes &= ~mask; // remove that bit from changes
    const uint8_t i = fast_log2_of_pow2(mask); // calculate bit position
    if (reached(pin_timeouts[gpio2pin[i]])) {
      trace("%s changing pin_state %d to %d\n", __func__, i, !(pin_states & mask));
      pin_states ^= mask;
      pin_timeouts[gpio2pin[i]] = time_after_us(DEBOUNCE_TIMEOUT_US);
    } else {
        trace("%s skipping pin_state %d because recent change\n", __func__, i);
    }
  }

  send_states();
}

static inline void setup_gpios() {
  //set all DB9-connector input signal pins as inputs with pullups
  for (uint8_t i = 0; i < sizeof(inputGPIOs); i++) {
    gpio_init(inputGPIOs[i]);
    gpio_set_dir(inputGPIOs[i], GPIO_IN);
    gpio_pull_up(inputGPIOs[i]);
    gpio_set_drive_strength(inputGPIOs[i], GPIO_DRIVE_STRENGTH_2MA);
  }
}

// ------------------------------JOYSTICK END ------------------------------


//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
static inline void led_blinking_task()
{
  static uint32_t next_us = 0;

  if (blink_timeout_us && reached(blink_timeout_us)) {
    led_set_blink_mode(BLINK_OFF);
    return;
  }

  // blink is disabled
  if (blink_interval_us == BLINK_OFF) return;

  // Blink every interval ms
  if (!reached(next_us)) return; // not enough time

  next_us = time_after_us(blink_interval_us);

  // TOGGLE
  led_state = !led_state;
  board_led_write(led_state);
}

//--------------------------------------------------------------------+
// USB Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  trace("%s called\n", __func__);
  led_blink_fast_until(time_after_us(1000 * 1000));
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  trace("%s called\n", __func__);
  led_set_blink_mode(BLINK_NOT_MOUNTED);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool)
{
  trace("%s called\n", __func__);
  led_set_blink_mode(BLINK_SUSPENDED);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  trace("%s called\n", __func__);
  if (tud_mounted()) {
    led_blink_fast_until(time_after_us(500 * 1000));
  } else {
   led_set_blink_mode(BLINK_NOT_MOUNTED);
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  trace("%s instance:%d\n", __func__, instance);
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  trace("%s called\n", __func__);
  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  trace("%s called\n", __func__);
}


/*------------- MAIN -------------*/

int main(void)
{
  stdio_init_all();
  trace("DualJoy starting...\n");

  board_init();

  sleep_ms(10);

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);

  sleep_ms(10);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

  sleep_ms(10);

  setup_gpios();

  sleep_ms(10);

  while (!tud_mounted()) {
    tud_task(); // tinyusb device task
    led_blinking_task();
  }

  while (1) {
    tud_task(); // tinyusb device task
    led_blinking_task();
    update_states_task();
    sleep_ms(1); // ~= 1000Hz sampling
    if (tud_suspended()) {
      sleep_ms(100);
    }
  }
}

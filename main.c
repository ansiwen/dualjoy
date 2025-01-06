/*
 * The MIT License (MIT)
 *
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

#include "usb_descriptors.h"

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
};

#define ARRAY_SIZE(_arr) ( sizeof(_arr) / sizeof(_arr[0]) )

static uint32_t blink_interval_us = BLINK_NOT_MOUNTED;
static uint32_t blink_timeout_us = 0;
static bool led_state = false;


// ----------------------------- JOYSTICK BEGIN ----------------------------

//DB9-connector:
//C64/Sega Mastersystem: 1 = up, 2 = down, 3 = left, 4 = right, 6 = btn1, 8 = gnd, 9 = btn2
//MSX: 1 = up, 2 = down, 3 = left, 4 = right, 6 = btn1, 7 = btn2, 8 = gnd

enum {
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

enum {
  UP = 0,
  DOWN,
  LEFT,
  RIGHT,
  BTN,
  STATE_NUM
};

const uint8_t inputPins[] =  {
  J1_UP, J1_DOWN, J1_LEFT, J1_RIGHT, J1_BTN,
  J2_UP, J2_DOWN, J2_LEFT, J2_RIGHT, J2_BTN
};

const uint32_t inputMasks[ARRAY_SIZE(inputPins)] = {
  1 << J1_UP, 1 << J1_DOWN, 1 << J1_LEFT, 1 << J1_RIGHT, 1 << J1_BTN,
  1 << J2_UP, 1 << J2_DOWN, 1 << J2_LEFT, 1 << J2_RIGHT, 1 << J2_BTN
};

typedef struct {
  uint32_t timeout;
  bool val;
} pin_state;

static pin_state states[ARRAY_SIZE(inputPins)] = { 0};

static inline uint8_t states2direction(pin_state s[STATE_NUM]) {
  if (s[UP].val) {
    if (s[RIGHT].val)
      return 2;  // NE
    else if (s[LEFT].val)
      return 8;  // NW
    else
      return 1;  // N
  }
  else if (s[DOWN].val) {
    if (s[RIGHT].val)
      return 4;  // SE
    else if (s[LEFT].val)
      return 6;  // SW
    else
      return 5;  // S
  }
  else if (s[RIGHT].val)
    return 3;  // E
  else if (s[LEFT].val)
    return 7;  // W
  else
    return 0;  // Center
}

static inline bool reached(uint32_t t) {
  // overflow safe time comparison
  return (int32_t)(t-time_us_32())<0;
}

static inline uint32_t time_after_us(uint32_t us) {
  return (time_us_32() + us) | 1; // make sure it's never 0 after overflow
}

static inline void led_flash() {
  led_state = !led_state;
  board_led_write(led_state);
  blink_timeout_us = time_after_us(EVENT_FLASH_US);
}

static inline void led_blink_fast_until(uint32_t timeout) {
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
  static report last1;
  static report last2;

  report report;
  report.direction = states2direction(&states[0]);
  report.buttons = states[BTN].val ? 1 : 0;

  if (!REPORT_EQUAL(last1, report)) {
    printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX J1: %d %x\n", report.direction, report.buttons);
    if (tud_hid_n_report(0, JOYSTICK_REPORT_ID, &report, sizeof(report))) {
      led_flash();
      REPORT_COPY(last1, report);
    } else {
      printf("###################################### failed to send report\n");
    }
  }

  report.direction = states2direction(&states[STATE_NUM]);
  report.buttons = states[STATE_NUM+BTN].val ? 1 : 0;

  if (!REPORT_EQUAL(last2, report)) {
    printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX J2: %d %x\n", report.direction, report.buttons);
    if (tud_hid_n_report(1, JOYSTICK2_REPORT_ID, &report, sizeof(report))) {
      led_flash();
      REPORT_COPY(last2, report);
    } else {
      printf("###################################### failed to send report\n");
    }
  }
}

static inline void set_state(pin_state *s, bool value) {
  if (s->val != value) {
    if (reached(s->timeout)) {
      printf("%s changing pin_state %d from %d to %d\n", __func__, s-&states[0], s->val, value);
      s->val = value;
      s->timeout = time_after_us(DEBOUNCE_TIMEOUT_US);
    } else {
        printf("%s skipping %d because recent change\n", __func__, s-&states[0]);
    }
  }
}

static inline void update_states() {
  uint32_t gpios = gpio_get_all();

  // here comes some over-engineering
  const uint32_t *mask_p = &inputMasks[0];
  pin_state *state_p = &states[0];
  while (mask_p != &inputMasks[ARRAY_SIZE(inputMasks)]) {
    set_state(state_p++, (gpios & *(mask_p++)) == 0);
  }

  send_states();
}

void setup_joysticks() {
  //set all DB9-connector input signal pins as inputs with pullups
  for (uint8_t i = 0; i < sizeof(inputPins); i++) {
    gpio_init(inputPins[i]);
    gpio_set_dir(inputPins[i], GPIO_IN);
    gpio_pull_up(inputPins[i]);
    gpio_set_drive_strength(inputPins[i], GPIO_DRIVE_STRENGTH_2MA);
  }
}

// ------------------------------JOYSTICK END ------------------------------


//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
static inline void led_blinking_task(void)
{
  static uint32_t next_us = 0;

  if (blink_timeout_us && reached(blink_timeout_us)) {
    blink_interval_us = BLINK_OFF;
    blink_timeout_us = 0;
    led_state = false;
    board_led_write(false);
    return;
  }

  // blink is disabled
  if (!blink_interval_us) return;

  // Blink every interval ms
  if (!reached(next_us)) return; // not enough time
  next_us = time_after_us(blink_interval_us);

  // TOGGLE
  led_state = !led_state;
  board_led_write(led_state);
}

/*------------- MAIN -------------*/
int main(void)
{
  stdio_init_all();
  printf("DualJoy starting...\n");

  board_init();

  sleep_ms(10);

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);

  sleep_ms(10);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

  sleep_ms(10);

  setup_joysticks();

  sleep_ms(10);

  while (!tud_mounted())
  {
    tud_task(); // tinyusb device task
    led_blinking_task();
  }

  while (1)
  {
    tud_task(); // tinyusb device task
    led_blinking_task();
    update_states();
    sleep_us(100);
    if (blink_interval_us == BLINK_SUSPENDED) {
      sleep_ms(100);
    }
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  printf("%s called\n", __func__);
  blink_interval_us = BLINK_OFF;
  led_blink_fast_until(time_after_us(1000 * 1000));
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  printf("%s called\n", __func__);
  blink_interval_us = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  printf("%s called\n", __func__);
  (void) remote_wakeup_en;
  blink_interval_us = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  printf("%s called\n", __func__);
  if (tud_mounted()) {
    led_blink_fast_until(time_after_us(500 * 1000));
  } else {
    blink_interval_us = BLINK_NOT_MOUNTED;
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  printf("%s instance:%d\n", __func__, instance);
  (void) len;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  printf("%s called\n", __func__);
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  printf("%s called\n", __func__);
  (void) instance;
}


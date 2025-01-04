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
#include "hardware/clocks.h"
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
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;


// ----------------------------- JOYSTICK BEGIN ----------------------------

#define JOYSTICK_REPORT_ID  0x04
#define JOYSTICK2_REPORT_ID 0x05

#define JOYSTICK_STATE_SIZE 3

//DB9-connector:
//C64/Sega Mastersystem: 1 = up, 2 = down, 3 = left, 4 = right, 6 = btn1, 8 = gnd, 9 = btn2
//MSX: 1 = up, 2 = down, 3 = left, 4 = right, 6 = btn1, 7 = btn2, 8 = gnd

#define J1_UP 5
#define J1_DOWN 4
#define J1_LEFT 3
#define J1_RIGHT 2
#define J1_BTN 27

#define J2_UP 9
#define J2_DOWN 8
#define J2_LEFT 7
#define J2_RIGHT 6
#define J2_BTN 26

#define J_MIN 129 // == -127
#define J_MID 0
#define J_MAX 127

#define J1_MASK (1 << J1_UP | 1 << J1_DOWN | 1 << J1_LEFT | 1 << J1_RIGHT | 1 << J1_BTN)
#define J2_MASK (1 << J2_UP | 1 << J2_DOWN | 1 << J2_LEFT | 1 << J2_RIGHT | 1 << J2_BTN)

#define GPIO_MASK (J1_MASK | J2_MASK)

#define GET_BIT(data, bit) ((data >> bit) & 1)
// #define SET_BIT(data, bit) (data |= (1 << bit))
// #define CLR_BIT(data, bit) (data &= ~(1 << bit))
// #define TOG_BIT(data, bit) (data ^= (1 << bit))

#define STATE_EQUAL(a, b, c) (a[c] == b[c] && a[c + 1] == b[c + 1] && a[c + 2] == b[c + 2])
#define STATE_COPY(a, b, c) do { a[c] = b[c]; a[c + 1] = b[c + 1]; a[c + 2] = b[c + 2]; } while(0)

const uint8_t inputPins[] =  {
  J1_UP, J1_DOWN, J1_LEFT, J1_RIGHT, J1_BTN,
  J2_UP, J2_DOWN, J2_LEFT, J2_RIGHT, J2_BTN
};

static uint8_t state[JOYSTICK_STATE_SIZE * 2] = { 0 };
static alarm_id_t alarm[JOYSTICK_STATE_SIZE * 2] = { 0 };

inline static void send_states() {
  static uint8_t last[JOYSTICK_STATE_SIZE * 2] = {255, 255, 255, 255, 255, 255};
  if (!STATE_EQUAL(last, state, 0)) {
    printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX J1: %d %d %d\n", state[0], state[1], state[2]);
    if (!tud_hid_n_report(0, JOYSTICK_REPORT_ID, &state[0], JOYSTICK_STATE_SIZE)) {
      printf("###################################### failed to send report\n");
    } else {
      STATE_COPY(last, state, 0);
    }
    // tud_hid_n_gamepad_report(0, JOYSTICK_REPORT_ID, (int8_t)(state[1]-127), (int8_t)(state[2]-127), 0, 0, 0, 0, 0, (uint32_t)state[0]);
  }
  if (!STATE_EQUAL(last, state, JOYSTICK_STATE_SIZE)) {
    printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX J2: %d %d %d\n", state[3], state[4], state[5]);
    if (!tud_hid_n_report(1, JOYSTICK2_REPORT_ID, &state[3], JOYSTICK_STATE_SIZE)) {
      printf("###################################### failed to send report\n");
    } else {
      STATE_COPY(last, state, JOYSTICK_STATE_SIZE);
    }
    // tud_hid_n_gamepad_report(1, JOYSTICK2_REPORT_ID, (int8_t)(state[4]-127), (int8_t)(state[5]-127), 0, 0, 0, 0, 0, (uint32_t)state[3]);
  }
}

static int64_t alarm_callback(alarm_id_t, void *);

inline static void set_state(uintptr_t i, uint8_t value) {
  if (state[i] != value) {
    if (!alarm[i]) {
      printf("%s changing state %d from %d to %d\n", __func__, i, state[i], value);
      state[i] = value;
      alarm[i] = add_alarm_in_ms(40, &alarm_callback, (void *)i, false);
    } else {
        printf("%s skipping %d because alarm is set\n", __func__, i);
    }
  }
}

static void update_states(uint, uint32_t) {
  uint32_t gpios = gpio_get_all();

  printf("%s gpios: %b\n", __func__, gpios & GPIO_MASK);

  set_state(0, !GET_BIT(gpios, J1_BTN));
  if (!GET_BIT(gpios, J1_LEFT)) set_state(1, J_MIN); /* left */
  else if (!GET_BIT(gpios, J1_RIGHT)) set_state(1, J_MAX); /* right */
  else set_state(1, J_MID);
  if (!GET_BIT(gpios, J1_UP)) set_state(2, J_MIN); /* up */
  else if (!GET_BIT(gpios, J1_DOWN)) set_state(2, J_MAX); /* down */
  else set_state(2, J_MID);

  set_state(JOYSTICK_STATE_SIZE + 0, !GET_BIT(gpios, J2_BTN));
  if (!GET_BIT(gpios, J2_LEFT)) set_state(JOYSTICK_STATE_SIZE + 1, J_MIN); /* left */
  else if (!GET_BIT(gpios, J2_RIGHT)) set_state(JOYSTICK_STATE_SIZE + 1, J_MAX); /* right */
  else set_state(JOYSTICK_STATE_SIZE + 1, J_MID);
  if (!GET_BIT(gpios, J2_UP)) set_state(JOYSTICK_STATE_SIZE + 2, J_MIN); /* up */
  else if (!GET_BIT(gpios, J2_DOWN)) set_state(JOYSTICK_STATE_SIZE + 2, J_MAX); /* down */
  else set_state(JOYSTICK_STATE_SIZE + 2, J_MID);

  send_states();
}

static int64_t alarm_callback(alarm_id_t id, void *user_data) {
  uintptr_t i = (uintptr_t)user_data;
  printf("%s id:%d user_data:%d\n", __func__, id, (uintptr_t)user_data);
  alarm[i] = 0;
  update_states(0, 0);
  return 0;
}

void setup_joysticks() {
  //set all DB9-connector input signal pins as inputs with pullups
  for (uint8_t i = 0; i < sizeof(inputPins); i++) {
    gpio_set_input_enabled(inputPins[i], true);
    gpio_set_pulls(inputPins[i], true, false); // PULLUP
  }
  gpio_set_irq_callback(update_states);
  for (uint8_t i = 0; i < sizeof(inputPins); i++) {
    gpio_set_irq_enabled(inputPins[i], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
  }
  irq_set_enabled(IO_IRQ_BANK0, true);
}

// ------------------------------JOYSTICK END ------------------------------


void led_blinking_task(void);
void hid_task(void);

/*------------- MAIN -------------*/
int main(void)
{

  set_sys_clock_khz(120000, true);

  stdio_init_all();
  printf("starting...\n");

  board_init();

  sleep_ms(100);

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

  setup_joysticks();

  while (!tud_hid_n_ready(0) || !tud_hid_n_ready(1))
  {
    tud_task(); // tinyusb device task
    led_blinking_task();
//    sleep_ms(1);
  }
  send_states();
  while(1) {
    tud_task();
    led_blinking_task();
//    sleep_ms(1);
    sleep_ms(100);
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  printf("%s called\n", __func__);
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  printf("%s called\n", __func__);
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  printf("%s called\n", __func__);
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  printf("%s called\n", __func__);
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
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

  // if (report_type == HID_REPORT_TYPE_OUTPUT)
  // {
  //   // Set keyboard LED e.g Capslock, Numlock etc...
  //   if (report_id == REPORT_ID_KEYBOARD)
  //   {
  //     // bufsize should be (at least) 1
  //     if ( bufsize < 1 ) return;

  //     uint8_t const kbd_leds = buffer[0];

  //     if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
  //     {
  //       // Capslock On: disable blink, turn led on
  //       blink_interval_ms = 0;
  //       board_led_write(true);
  //     }else
  //     {
  //       // Caplocks Off: back to normal blink
  //       board_led_write(false);
  //       blink_interval_ms = BLINK_MOUNTED;
  //     }
  //   }
  // }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}

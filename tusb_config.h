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

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_HID               2
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// HID buffer size Should be sufficient to hold ID (if any) + Data
#define CFG_TUD_HID_EP_BUFSIZE    16

// Add these new definitions for the additional interface
#define CFG_TUD_HID_EP_COUNT      2    // Number of HID endpoints
#define CFG_TUD_HID_INSTANCE_COUNT 2   // Number of HID instances


#ifdef LIB_PICO_STDIO_USB

//--- CDC stuff ---//

#undef CFG_TUD_CDC
#undef CFG_TUD_VENDOR
#define CFG_TUD_CDC             (1)
#define CFG_TUD_CDC_RX_BUFSIZE  (256)
#define CFG_TUD_CDC_TX_BUFSIZE  (256)

#include "pico/stdio_usb.h"

// We use a vendor specific interface but with our own driver
// Vendor driver only used for Microsoft OS 2.0 descriptor
#if !PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_MS_OS_20_DESCRIPTOR
#define CFG_TUD_VENDOR            (0)
#else
#define CFG_TUD_VENDOR            (1)
#define CFG_TUD_VENDOR_RX_BUFSIZE  (256)
#define CFG_TUD_VENDOR_TX_BUFSIZE  (256)
#endif

#endif // LIB_PICO_STDIO_USB

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */

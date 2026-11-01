// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 han-um
//
// Debug logging over USB CDC.
//
// Routes stdio (printf) into an interrupt-safe ring buffer, then drains it to
// the composite device's CDC-ACM IN endpoint from the USB task context. This
// lets firmware logs be read over the same USB cable as the audio device,
// with no UART adapter — useful when reporting issues.
//

#ifndef DEBUG_CDC_H
#define DEBUG_CDC_H
#include <stdint.h>
#include <stdbool.h>
bool debug_cdc_take_battery_request(uint32_t *nonce);
bool debug_cdc_take_anc_request(uint32_t *nonce, uint32_t *session, unsigned *mode);

// Register the CDC stdio driver so printf() output is captured. Call once,
// after stdio_init_all() and tinyusb_main().
void debug_cdc_init(void);

// Drain buffered stdout to the CDC IN endpoint. Call from the USB task
// (same context as tud_task), after tud_task().
void debug_cdc_task(void);

// Retain the latest 32 sparse connection events independently of stdout.
// tag must have static lifetime. Arguments are event-specific identifiers.
void debug_cdc_event(const char *tag, uint32_t arg0, uint32_t arg1);

#endif // DEBUG_CDC_H

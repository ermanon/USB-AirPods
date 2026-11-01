// SPDX-License-Identifier: GPL-3.0-only
#ifndef CASE_WAKE_RETRY_H
#define CASE_WAKE_RETRY_H
#include <stdbool.h>
#include <stdint.h>

#define CASE_WAKE_SETTLE_MS 2000u
#define CASE_WAKE_PAGE_UNITS 9600u // 6 seconds, controller units of 0.625 ms
#define NORMAL_PAGE_UNITS 0x6000u // SDK default, about 15 seconds

typedef struct { bool active; uint32_t started_ms; } case_wake_retry_t;
static inline bool case_wake_fast(const case_wake_retry_t *s, uint32_t now) {
    return s->active && now - s->started_ms < 30000u;
}
static inline uint32_t case_wake_retry_delay(const case_wake_retry_t *s, uint32_t now,
                                            uint32_t normal_delay) {
    // Preserve initial settle and release delays; shorten only failure backoff.
    return case_wake_fast(s, now) && normal_delay >= 5000u ? 1000u : normal_delay;
}
static inline uint16_t case_wake_page_units(const case_wake_retry_t *s, uint32_t now) {
    return case_wake_fast(s, now) ? CASE_WAKE_PAGE_UNITS : NORMAL_PAGE_UNITS;
}
#endif

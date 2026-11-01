#ifndef USBPODS_AU_TIMELINE_H
#define USBPODS_AU_TIMELINE_H
#include <stdbool.h>
#include <stdint.h>

// Learn cadence from consecutive source timestamps; do not assume that the
// proprietary timestamp is in samples, milliseconds, or any particular unit.
typedef struct { uint32_t previous, step; unsigned confidence; bool seen; } au_timeline_t;
typedef struct { unsigned lost; bool discontinuity, drop; } au_timeline_result_t;
static inline au_timeline_result_t au_timeline_observe(au_timeline_t *t, uint32_t ts) {
    au_timeline_result_t result = {0};
    if (!t->seen) { t->seen = true; t->previous = ts; return result; }
    uint32_t delta = ts - t->previous;
    if (t->confidence >= 32) {
        if (!delta || delta >= 0x80000000u) { result.drop = true; return result; }
        if (delta != t->step) {
            if (delta % t->step == 0 && delta / t->step <= 5)
                result.lost = delta / t->step - 1;
            else {
                result.discontinuity = true;
                t->confidence = 0;
            }
        }
    }
    if (t->confidence < 32) {
        if (delta && delta < 0x80000000u) {
            if (delta == t->step) t->confidence++;
            else { t->step = delta; t->confidence = 1; }
        } else t->confidence = 0;
    }
    t->previous = ts;
    return result;
}
#endif

// SPDX-License-Identifier: GPL-3.0-only
#ifndef AACP_ANC_H
#define AACP_ANC_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef struct { unsigned mode, mask; uint32_t session; } aacp_anc_state;
static inline void aacp_anc_reset(aacp_anc_state *s) {
    s->mode = s->mask = 0;
    ++s->session;
}
static inline bool aacp_anc_parse(aacp_anc_state *s, const uint8_t *p, size_t n) {
    static const uint8_t header[] = {4,0,4,0,9,0,13};
    if (n != 11 || memcmp(p, header, 7) || p[7] < 1 || p[7] > 4 ||
        p[8] || p[9] || p[10]) return false;
    s->mode = p[7];
    // Only observed modes are offered: no model-name capability guesses.
    s->mask |= 1u << (p[7] - 1);
    return true;
}
static inline bool aacp_anc_allowed(const aacp_anc_state *s, uint32_t session, unsigned mode) {
    return session == s->session && mode >= 1 && mode <= 4 &&
           (s->mask & (1u << (mode - 1))) != 0;
}
#endif

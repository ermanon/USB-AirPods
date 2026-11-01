// SPDX-License-Identifier: GPL-3.0-only
#ifndef AACP_BATTERY_H
#define AACP_BATTERY_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Read-only decoding of notifications already enabled by the AACP handshake.
// Components: 0x04 left, 0x02 right, 0x08 case. No Bluetooth TX here.
typedef struct {
    int level; // -1: unavailable
    unsigned status; // 0 unknown, 1 charging, 2 discharging, 4 disconnected
    uint32_t updated_ms;
    bool seen;
} aacp_battery_value;

typedef struct {
    bool connected;
    aacp_battery_value values[3]; // left, right, case
} aacp_battery_state;

static inline void aacp_battery_reset(aacp_battery_state *s, bool connected) {
    memset(s, 0, sizeof(*s));
    s->connected = connected;
    for (int i = 0; i < 3; ++i) s->values[i].level = -1;
}

static inline bool aacp_battery_parse(aacp_battery_state *s,
                                     const uint8_t *p, size_t n, uint32_t now) {
    static const uint8_t header[] = {4, 0, 4, 0, 4, 0};
    if (!s->connected || n < 7 || memcmp(p, header, 6) != 0 ||
        p[6] == 0 || n != 7u + 5u * p[6]) return false;
    // Validate the complete notification before modifying any cached values.
    unsigned mask = 0;
    for (unsigned j = 0; j < p[6]; ++j) {
        const uint8_t *v = p + 7 + j * 5;
        if (v[1] != 1 || v[4] != 1) return false;
        unsigned bit = v[0] == 4 ? 1 : v[0] == 2 ? 2 : v[0] == 8 ? 4 : 0;
        if (mask & bit) return false;
        mask |= bit;
    }
    for (unsigned j = 0; j < p[6]; ++j) {
        const uint8_t *v = p + 7 + j * 5;
        int i = v[0] == 4 ? 0 : v[0] == 2 ? 1 : v[0] == 8 ? 2 : -1;
        if (i < 0) continue;
        aacp_battery_value *b = &s->values[i];
        b->status = v[3] == 1 || v[3] == 2 || v[3] == 4 ? v[3] : 0;
        b->level = v[2] <= 100 && (b->status == 1 || b->status == 2) ? v[2] : -1;
        b->updated_ms = now;
        b->seen = true;
    }
    return mask != 0;
}
#endif

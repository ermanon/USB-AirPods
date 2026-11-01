// SPDX-License-Identifier: GPL-3.0-only
#ifndef BATTERY_QUERY_H
#define BATTERY_QUERY_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Exact, bounded query. The nonce lets the host reject old buffered replies.
static inline bool battery_query_parse(const char *line, uint32_t *nonce) {
    if (strlen(line) != 24 || memcmp(line, "USBPODS BATTERY ", 16) != 0) return false;
    uint32_t value = 0;
    for (int i = 16; i < 24; ++i) {
        char c = line[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else return false;
        value = (value << 4) | d;
    }
    *nonce = value;
    return true;
}
// ANC request: fixed width, session-bound, no arbitrary Bluetooth payload.
static inline bool anc_query_parse(const char *line, uint32_t *nonce, uint32_t *session, unsigned *mode) {
    if (strlen(line) != 31 || memcmp(line, "USBPODS ANC ", 12) || line[20] != ' ' ||
        line[29] != ' ' || line[30] < '1' || line[30] > '4') return false;
    char query[] = "USBPODS BATTERY 00000000";
    memcpy(query + 16, line + 12, 8);
    if (!battery_query_parse(query, nonce)) return false;
    memcpy(query + 16, line + 21, 8);
    if (!battery_query_parse(query, session)) return false;
    *mode = (unsigned)(line[30] - '0');
    return true;
}
#endif

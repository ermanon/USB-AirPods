// SPDX-License-Identifier: GPL-3.0-only
// AACP/proximity formats: librepods-org/librepods, docs/AAP Definitions.md
// and linux/ble/blemanager.cpp. AACP status is primary/secondary, not L/R.
#ifndef AIRPODS_CASE_POLICY_H
#define AIRPODS_CASE_POLICY_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    bool both_in_case, in_ear, lid_known, lid_closed;
    uint16_t model;
} airpods_proximity_t;

static inline bool airpods_ear_packet(const uint8_t *p, size_t n, bool *both) {
    if (n != 8 || p[0] != 4 || p[1] || p[2] != 4 || p[3] ||
        p[4] != 6 || p[5] || p[6] > 2 || p[7] > 2) return false;
    *both = p[6] == 2 && p[7] == 2;
    return true;
}

// Validate the complete key list before accepting a key. Never log key bytes.
static inline bool airpods_irk_packet(const uint8_t *p, size_t n, uint8_t key[16]) {
    if (n < 7 || p[0] != 4 || p[1] || p[2] != 4 || p[3] || p[4] != 0x31 || p[5]) return false;
    size_t off = 7, found = 0;
    for (unsigned i = 0; i < p[6]; i++) {
        if (n - off < 4) return false;
        unsigned length = (unsigned)p[off + 2] | ((unsigned)p[off + 3] << 8);
        uint16_t type = (uint16_t)p[off] | ((uint16_t)p[off + 1] << 8);
        off += 4;
        if (length > n - off) return false;
        if (type == 1 && length == 16) found = off;
        off += length;
    }
    if (off != n || !found) return false;
    uint8_t nonzero = 0;
    for (unsigned i = 0; i < 16; i++) nonzero |= p[found + i];
    if (!nonzero) return false;
    for (unsigned i = 0; i < 16; i++) key[i] = p[found + i];
    return true;
}

// Apple manufacturer value, excluding company ID 4c 00.
static inline bool airpods_proximity_parse(const uint8_t *p, size_t n, airpods_proximity_t *out) {
    if (n != 27 || p[0] != 7 || p[1] != 25 || p[2] != 1) return false;
    uint16_t model = ((uint16_t)p[3] << 8) | p[4];
    // Earbud models only; never apply this policy to AirPods Max/other Apple devices.
    if (model != 0x1920 && model != 0x1b20 && model != 0x1420 && model != 0x2420 &&
        model != 0x0e20 && model != 0x1320 && model != 0x0f20 && model != 0x0220) return false;
    uint8_t status = p[5];
    *out = (airpods_proximity_t){
        .both_in_case = (status & 4) != 0,
        .in_ear = (status & 0x0a) != 0,
        .lid_known = (status & 0x40) != 0,
        .lid_closed = (p[8] & 8) != 0,
        .model = model
    };
    return true;
}

typedef struct {
    bool parked, close_pending;
    uint32_t close_since, last_report;
    unsigned closed_reports;
} airpods_case_policy_t;

static inline void airpods_case_observe(airpods_case_policy_t *s,
                                       const airpods_proximity_t *p, uint32_t now) {
    // The accessory can initiate BR/EDR as soon as the lid opens. Allow it
    // before wear, rather than disconnecting it and paging a waking device.
    if ((p->in_ear && !p->both_in_case) ||
        (p->lid_known && !p->lid_closed)) {
        s->parked = false;
        s->close_pending = false;
        s->closed_reports = 0;
        return;
    }
    if (p->both_in_case && !p->in_ear && p->lid_known && p->lid_closed) {
        if (!s->close_pending || now - s->last_report > 2000) {
            s->close_pending = true;
            s->close_since = now;
            s->closed_reports = 0;
        }
        s->last_report = now;
        if (s->closed_reports < 2) s->closed_reports++;
    } else if (!p->both_in_case || p->in_ear || (p->lid_known && !p->lid_closed)) {
        s->close_pending = false;
        s->closed_reports = 0;
    }
    // Both pods can advertise using the same IRK. The other pod's unknown
    // lid does not contradict a verified closed lid, nor refresh its age.
}

static inline bool airpods_case_poll(airpods_case_policy_t *s, uint32_t now) {
    // Two verified reports and a short debounce; stale/unknown packets cannot park.
    if (!s->parked && s->close_pending && s->closed_reports >= 2 &&
        now - s->close_since >= 700 && now - s->last_report <= 2000) {
        s->parked = true;
        return true;
    }
    return false;
}
#endif

#ifndef USBPODS_AUDIO_RECOVERY_H
#define USBPODS_AUDIO_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

// Pure transition policy: no stack calls, allocation, or timers. The owner
// executes actions under the Bluetooth lock and checks every command result.
typedef enum {
    AUDIO_RECOVERY_IDLE, AUDIO_RECOVERY_SUSPEND,
    AUDIO_RECOVERY_START, AUDIO_RECOVERY_VERIFY, AUDIO_RECOVERY_DISCONNECT
} audio_recovery_state_t;
typedef enum {
    AUDIO_RECOVERY_NONE, AUDIO_RECOVERY_SEND_START,
    AUDIO_RECOVERY_RESUME, AUDIO_RECOVERY_RECONNECT, AUDIO_RECOVERY_HEALTHY
} audio_recovery_action_t;
typedef struct {
    audio_recovery_state_t state;
    uint16_t cid;
    uint8_t seid;
    uint32_t entered_ms, last_tx_ms;
    unsigned tx_count;
} audio_recovery_t;

static inline void audio_recovery_clear(audio_recovery_t *r) {
    *r = (audio_recovery_t){0};
}
static inline void audio_recovery_enter(audio_recovery_t *r,
        audio_recovery_state_t state, uint16_t cid, uint8_t seid, uint32_t now) {
    *r = (audio_recovery_t){.state = state, .cid = cid, .seid = seid,
                           .entered_ms = now, .last_tx_ms = now};
}
static inline bool audio_recovery_matches(const audio_recovery_t *r,
        uint16_t cid, uint8_t seid) {
    return r->state != AUDIO_RECOVERY_IDLE && r->cid == cid && r->seid == seid;
}
static inline audio_recovery_action_t audio_recovery_accept(audio_recovery_t *r,
        uint16_t cid, uint8_t seid, bool start, uint32_t now) {
    if (!audio_recovery_matches(r, cid, seid)) return AUDIO_RECOVERY_NONE;
    if (!start && r->state == AUDIO_RECOVERY_SUSPEND) {
        audio_recovery_enter(r, AUDIO_RECOVERY_START, cid, seid, now);
        return AUDIO_RECOVERY_SEND_START;
    }
    if (start && r->state == AUDIO_RECOVERY_START) {
        audio_recovery_enter(r, AUDIO_RECOVERY_VERIFY, cid, seid, now);
        return AUDIO_RECOVERY_RESUME;
    }
    return AUDIO_RECOVERY_NONE;
}
static inline void audio_recovery_tx(audio_recovery_t *r, uint32_t now) {
    if (r->state == AUDIO_RECOVERY_VERIFY) {
        r->last_tx_ms = now;
        r->tx_count++;
    }
}
static inline audio_recovery_action_t audio_recovery_poll(audio_recovery_t *r,
        uint32_t now) {
    uint32_t age = now - r->entered_ms; // unsigned subtraction survives wrap
    if (r->state == AUDIO_RECOVERY_VERIFY && age >= 500 &&
        r->tx_count >= 10 && now - r->last_tx_ms < 150) {
        audio_recovery_clear(r);
        return AUDIO_RECOVERY_HEALTHY;
    }
    if (r->state == AUDIO_RECOVERY_SUSPEND || r->state == AUDIO_RECOVERY_START ||
        r->state == AUDIO_RECOVERY_VERIFY) {
        if (age >= 3000) return AUDIO_RECOVERY_RECONNECT;
    }
    return AUDIO_RECOVERY_NONE;
}
#endif

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 han-um
// Derived from librepods (https://github.com/librepods-org/librepods), GPL-3.0.
//
// AACP (Apple Accessory Protocol) channel — the AirPods hi-res mic transport.
//
// Opens an outgoing L2CAP channel on PSM 0x1001 over the existing ACL link,
// runs the fixed init sequence (handshake / SET_FEATURE_FLAGS /
// REQUEST_NOTIFICATIONS), then drives the microphone stream: a START opcode
// makes the AirPods push type-0x58 SDUs carrying AAC-ELD access units, which
// are demuxed here and handed to the decoder (aacp_mic_dec.c).
//
// All byte sequences and the 0x58 demux layout are ports of librepods
// (linux-rust branch, GPL-3.0) — see that project's aacp_audio.rs.
//
// LOGGING RULE: these handlers run in the BTstack run loop, where a burst of
// blocking log output has caused A2DP underruns. No per-packet logging on the
// audio path; telemetry is aggregated and printed at most once per second.
//

#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_aacp.h"
#include "aacp_mic_dec.h"
#include "au_timeline.h"
#include "airpods_case.h"
#include "../tinyusb/debug_cdc.h"

#define AACP_PSM              0x1001
// Requested local MTU. BTstack clamps this to l2cap_max_mtu() =
// HCI_ACL_PAYLOAD_SIZE - 4 (currently 1691), which is what actually gets
// advertised. Measured audio SDUs top out around 354 bytes, well under that;
// if larger SDUs ever get dropped, raise HCI_ACL_PAYLOAD_SIZE in
// btstack_config.h rather than this constant.
#define AACP_LOCAL_MTU        4096
#define AACP_CONNECT_DELAY_MS 1500   // let A2DP settle before opening the channel
#define AACP_RETRY_DELAY_MS   3000   // delay between connect retries
#define AACP_MAX_ATTEMPTS     3      // initial attempt + retries per A2DP session

// Mic lifecycle: the ONLY trigger is the USB alt setting — main.c calls
// aacp_mic_start()/aacp_mic_stop() when the host opens/closes the recording
// stream. If the host opens the mic before the AACP channel is up (or the
// channel drops mid-capture), the start is remembered and fired as soon as
// the first control packet confirms the channel.

// --- Fixed init byte sequences (verbatim from librepods) ---

// 1) Handshake: 16 bytes sent raw (the 04 00 04 00 data header is part of it).
static const uint8_t aacp_handshake[] = {
    0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 2) SET_FEATURE_FLAGS (opcode 0x004D): 04 00 04 00 | 4D 00 | FF 00 00 00 00 00 00 00
static const uint8_t aacp_set_feature_flags[] = {
    0x04, 0x00, 0x04, 0x00, 0x4D, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 3) REQUEST_NOTIFICATIONS (opcode 0x000F): 04 00 04 00 | 0F 00 | FF FF FF FF
static const uint8_t aacp_request_notifications[] = {
    0x04, 0x00, 0x04, 0x00, 0x0F, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF
};

// Proximity key request used by LibrePods/CAPod; retain only IRK, never log keys.
static const uint8_t aacp_request_identity[] = { 4, 0, 4, 0, 0x30, 0, 5, 0 };

// --- Mic stream control byte sequences (verbatim from librepods) ---

static const uint8_t aacp_mic_start_bytes[] = {
    0x04, 0x00, 0x04, 0x00, 0x58, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x01, 0x82, 0x00, 0x00, 0x00,
    0x04, 0x96, 0x00
};

static const uint8_t aacp_mic_stop_bytes[] = {
    0x04, 0x00, 0x04, 0x00, 0x58, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x03, 0x01
};

// --- TX queue (ordered; one packet sent per CAN_SEND_NOW event) ---
// Entries reference static const byte arrays only, so no payload copies.
typedef struct {
    const uint8_t *data;
    uint16_t       len;
} aacp_pkt_t;

#define AACP_TX_QUEUE_LEN 8
static aacp_pkt_t aacp_tx_queue[AACP_TX_QUEUE_LEN];
static uint8_t    aacp_tx_head = 0;   // next slot to send
static uint8_t    aacp_tx_count = 0;  // pending packets

static uint16_t   aacp_cid       = 0;
static bool       aacp_connected = false;
static bd_addr_t  aacp_addr;
#include "aacp_battery.h"
#include "aacp_anc.h"
static aacp_anc_state anc_state;
static aacp_battery_state battery_state;

void aacp_battery_report(uint32_t nonce) {
    uint32_t now = btstack_run_loop_get_time_ms();
    char line[192];
    int used = snprintf(line, sizeof(line), "[USBPODS-BATTERY/1] %08lX %u",
                        (unsigned long)nonce, battery_state.connected ? 1u : 0u);
    for (int i = 0; i < 3; ++i) {
        const aacp_battery_value *v = &battery_state.values[i];
        used += snprintf(line + used, sizeof(line) - (size_t)used, " %d %u %lu",
                         v->seen ? v->level : -1, v->status,
                         (unsigned long)(v->seen ? now - v->updated_ms : UINT32_MAX));
    }
    printf("%s\n", line);
    printf("[USBPODS-ANC/1] %08lX %u %u %u %08lX\n", (unsigned long)nonce,
           battery_state.connected ? 1u : 0u, anc_state.mode, anc_state.mask,
           (unsigned long)anc_state.session);
    // This is the address of the AirPods selected by the existing pairing
    // logic. It lets the Windows companion keep a user-selected label for
    // each paired set without sending any additional Bluetooth command.
    printf("[USBPODS-DEVICE/1] %08lX %u %s\n", (unsigned long)nonce,
           battery_state.connected ? 1u : 0u, bd_addr_to_str(aacp_addr));
}

static uint8_t    aacp_attempts  = 0;      // connect attempts this A2DP session
static bool       aacp_got_control = false; // first valid AACP control packet seen
static bool       aacp_session_active = false; // A2DP permits connect/retry

static btstack_timer_source_t aacp_connect_timer;
static btstack_timer_source_t aacp_tx_timer;
static unsigned aacp_tx_retries;
static uint32_t aacp_tx_started_ms;

// --- Mic stream state ---

// type-0x58 audio SDU layout: 22-byte header, then N x [ts:u32 LE][len:u8][au].
#define TYPE58_HEADER_LEN 22

static bool aacp_mic_running = false;   // START sent, STOP not yet sent
static bool aacp_mic_want    = false;   // host wants the mic (USB alt 1),
                                        // start deferred until channel is up
static btstack_timer_source_t aacp_mic_stats_timer;    // 1 Hz report
static au_timeline_t mic_timeline;
static uint32_t mic_timestamp_gaps, mic_timestamp_drops;
static unsigned mic_restart_attempts;
static uint32_t mic_report_ms;

// RX statistics — cumulative since START, plus per-interval rates.
static struct {
    uint32_t sdu_total;     // audio SDUs seen
    uint32_t au_total;      // AUs demuxed
    uint32_t sdu_interval;  // audio SDUs this report interval
    uint32_t au_interval;   // AUs this report interval
    uint32_t au_bytes_interval;
    uint16_t au_min, au_max;        // AU size bounds (cumulative)
    uint16_t sdu_max;               // largest audio SDU seen (cumulative)
    uint16_t demux_short;           // SDUs whose trailer didn't parse cleanly
    uint32_t last_sdu_ms;           // for stall observation in the report
} mic_stats;

static void aacp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void aacp_schedule_retry(void);
static void aacp_mic_teardown(void);
static void aacp_tx_poll(btstack_timer_source_t *timer);

// ------------------------------------------------------------------

void aacp_init(void) {
    aacp_battery_reset(&battery_state, false);
    aacp_anc_reset(&anc_state);
    aacp_cid         = 0;
    aacp_connected   = false;
    aacp_tx_head     = 0;
    aacp_tx_count    = 0;
    aacp_mic_running = false;
}

static void aacp_do_connect(btstack_timer_source_t *ts) {
    UNUSED(ts);
    if (!aacp_session_active || aacp_cid != 0) return;
    aacp_attempts++;
    printf("[AACP] connecting to %s PSM 0x%04x (attempt %u/%u, local mtu %u) ...\n",
           bd_addr_to_str(aacp_addr), AACP_PSM, aacp_attempts, AACP_MAX_ATTEMPTS,
           btstack_min(AACP_LOCAL_MTU, l2cap_max_mtu()));
    uint8_t status = l2cap_create_channel(aacp_packet_handler, aacp_addr, AACP_PSM,
                                          AACP_LOCAL_MTU, &aacp_cid);
    if (status != ERROR_CODE_SUCCESS) {
        printf("[AACP] l2cap_create_channel failed: 0x%02x\n", status);
        aacp_cid = 0;
        aacp_schedule_retry();
    }
}

// Re-arm the connect timer for a retry, if attempts remain.
static void aacp_schedule_retry(void) {
    if (!aacp_session_active) return;
    if (aacp_attempts >= AACP_MAX_ATTEMPTS) {
        printf("[AACP] giving up after %u attempts. If this persists: re-pair "
               "(BOOTSEL long press) so the AirPods pick up our DID record.\n", aacp_attempts);
        return;
    }
    printf("[AACP] retrying in %d ms\n", AACP_RETRY_DELAY_MS);
    btstack_run_loop_remove_timer(&aacp_connect_timer);
    btstack_run_loop_set_timer_handler(&aacp_connect_timer, aacp_do_connect);
    btstack_run_loop_set_timer(&aacp_connect_timer, AACP_RETRY_DELAY_MS);
    btstack_run_loop_add_timer(&aacp_connect_timer);
}

void aacp_connect(bd_addr_t addr) {
    if (aacp_cid != 0) {
        printf("[AACP] connect ignored, channel already active (cid 0x%04x)\n", aacp_cid);
        return;
    }
    memcpy(aacp_addr, addr, sizeof(bd_addr_t));
    aacp_session_active = true;
    aacp_attempts    = 0;
    aacp_got_control = false;
    printf("[AACP] scheduling connect in %d ms\n", AACP_CONNECT_DELAY_MS);
    btstack_run_loop_remove_timer(&aacp_connect_timer);
    btstack_run_loop_set_timer_handler(&aacp_connect_timer, aacp_do_connect);
    btstack_run_loop_set_timer(&aacp_connect_timer, AACP_CONNECT_DELAY_MS);
    btstack_run_loop_add_timer(&aacp_connect_timer);
}

static void aacp_mic_teardown(void);   // fwd

void aacp_disconnect(void) {
    aacp_battery_reset(&battery_state, false);
    aacp_anc_reset(&anc_state);
    aacp_session_active = false;
    aacp_got_control = false;
    btstack_run_loop_remove_timer(&aacp_connect_timer);
    btstack_run_loop_remove_timer(&aacp_tx_timer);
    aacp_mic_teardown();
    if (aacp_cid != 0) {
        l2cap_disconnect(aacp_cid);
    }
    aacp_cid       = 0;
    aacp_connected = false;
    aacp_tx_head   = 0;
    aacp_tx_count  = 0;
}

bool aacp_is_connected(void) {
    return aacp_connected;
}

// ------------------------------------------------------------------
// TX queue

static bool aacp_tx_enqueue(const uint8_t *data, uint16_t len) {
    if (aacp_tx_count >= AACP_TX_QUEUE_LEN) {
        printf("[AACP] TX queue full, dropping %u-byte packet\n", len);
        return false;
    }
    if (!aacp_tx_count) {
        aacp_tx_started_ms = btstack_run_loop_get_time_ms();
        aacp_tx_retries = 0;
        btstack_run_loop_set_timer_handler(&aacp_tx_timer, aacp_tx_poll);
        btstack_run_loop_set_timer(&aacp_tx_timer, 100);
        btstack_run_loop_add_timer(&aacp_tx_timer);
    }
    uint8_t slot = (uint8_t)((aacp_tx_head + aacp_tx_count) % AACP_TX_QUEUE_LEN);
    aacp_tx_queue[slot].data = data;
    aacp_tx_queue[slot].len  = len;
    aacp_tx_count++;
    if (aacp_cid != 0) l2cap_request_can_send_now_event(aacp_cid);
    return true;
}

void aacp_anc_set(uint32_t nonce, uint32_t session, unsigned mode) {
    static const uint8_t modes[4][11] = {
        {4,0,4,0,9,0,13,1,0,0,0}, {4,0,4,0,9,0,13,2,0,0,0},
        {4,0,4,0,9,0,13,3,0,0,0}, {4,0,4,0,9,0,13,4,0,0,0}
    };
    unsigned result = 1; // unavailable / stale session / unverified mode
    if (aacp_connected && aacp_got_control && aacp_anc_allowed(&anc_state, session, mode)) {
        // Leave two queue slots available for existing microphone controls.
        result = aacp_tx_count < AACP_TX_QUEUE_LEN - 2 &&
                 aacp_tx_enqueue(modes[mode - 1], 11) ? 0 : 2;
    }
    // Queued is not confirmed. Only an RX notification updates anc_state.mode.
    printf("[USBPODS-ANC-RESULT/1] %08lX %u\n", (unsigned long)nonce, result);
}

static void aacp_tx_poll(btstack_timer_source_t *timer) {
    if (!aacp_tx_count || !aacp_cid) return;
    if (btstack_run_loop_get_time_ms() - aacp_tx_started_ms >= 4000 || aacp_tx_retries >= 3) {
        debug_cdc_event("AACP_TX_TIMEOUT", aacp_cid, aacp_tx_count);
        aacp_tx_head = aacp_tx_count = 0;
        aacp_mic_teardown();
        l2cap_disconnect(aacp_cid);
        return;
    }
    l2cap_request_can_send_now_event(aacp_cid);
    btstack_run_loop_set_timer(timer, 100);
    btstack_run_loop_add_timer(timer);
}

static void aacp_handle_can_send_now(void) {
    if (aacp_tx_count == 0) return;
    aacp_pkt_t *p = &aacp_tx_queue[aacp_tx_head];
    uint8_t status = l2cap_send(aacp_cid, (uint8_t *) p->data, p->len);
    if (status != ERROR_CODE_SUCCESS) {
        aacp_tx_retries++;
        debug_cdc_event("AACP_TX_ERROR", aacp_cid, status);
        // Retry from the 100ms timer, not a tight CAN_SEND_NOW loop.
        return;
    }
    printf("[AACP] sent %u bytes (%u queued)\n", p->len, (unsigned)(aacp_tx_count - 1));
    aacp_tx_head = (uint8_t)((aacp_tx_head + 1) % AACP_TX_QUEUE_LEN);
    aacp_tx_count--;
    aacp_tx_started_ms = btstack_run_loop_get_time_ms();
    aacp_tx_retries = 0;
    if (!aacp_tx_count) btstack_run_loop_remove_timer(&aacp_tx_timer);
    if (aacp_tx_count > 0) l2cap_request_can_send_now_event(aacp_cid);
}

static void aacp_queue_init_sequence(void) {
    aacp_tx_enqueue(aacp_handshake,             sizeof(aacp_handshake));
    aacp_tx_enqueue(aacp_set_feature_flags,     sizeof(aacp_set_feature_flags));
    aacp_tx_enqueue(aacp_request_notifications, sizeof(aacp_request_notifications));
    aacp_tx_enqueue(aacp_request_identity, sizeof(aacp_request_identity));
}

// ------------------------------------------------------------------
// Mic stream

// 1 Hz statistics report. Two short lines per second — safe log volume.
// Doubles as the stream stall watchdog: no audio SDU for >2000ms while
// capture is active → STOP + START to restart the stream.
static void aacp_mic_stats_report(btstack_timer_source_t *ts) {
    uint32_t now = btstack_run_loop_get_time_ms();
    // Covers BOTH stall cases: stream died mid-capture AND "START ignored"
    // (no SDU ever arrived — happens when START lands too soon after the
    // AACP handshake, e.g. host opened the mic before/during BT connect;
    // last_sdu_ms is initialized to the START time so the retry kicks in
    // 2s later either way).
    bool stalled = (now - mic_stats.last_sdu_ms) > 2000;
    bool decoder_faulted = aacp_mic_dec_faulted();
    if (aacp_mic_dec_healthy()) mic_restart_attempts = 0;
    uint32_t avg = mic_stats.au_interval ? (mic_stats.au_bytes_interval / mic_stats.au_interval) : 0;
    printf("[MIC] sdu/s=%lu au/s=%lu au min/avg/max=%u/%lu/%u sdu_max=%u total_au=%lu%s%s\n",
           (unsigned long) mic_stats.sdu_interval,
           (unsigned long) mic_stats.au_interval,
           mic_stats.au_min == 0xFFFF ? 0 : mic_stats.au_min,
           (unsigned long) avg,
           mic_stats.au_max,
           mic_stats.sdu_max,
           (unsigned long) mic_stats.au_total,
           mic_stats.demux_short ? " DEMUX_SHORT!" : "",
           stalled ? " STALLED>2s" : "");
    aacp_mic_dec_report();
    printf("[MICTS] ms=%lu interval=%lu step=%lu confidence=%u gaps=%lu reordered=%lu txq=%u\n",
           (unsigned long)now, (unsigned long)(now - mic_report_ms),
           (unsigned long)mic_timeline.step, mic_timeline.confidence,
           (unsigned long)mic_timestamp_gaps, (unsigned long)mic_timestamp_drops, aacp_tx_count);
    mic_report_ms = now;
    mic_stats.sdu_interval = 0;
    mic_stats.au_interval = 0;
    mic_stats.au_bytes_interval = 0;

    // A blocked link cannot drain STOP/START. Keep one recovery batch in
    // flight; repeated batches otherwise fill the queue with stale commands.
    if ((stalled || decoder_faulted) && aacp_cid != 0 && aacp_connected && aacp_tx_count == 0) {
        debug_cdc_event("MIC_RECOVERY", aacp_cid, decoder_faulted);
        if (mic_restart_attempts >= 2) {
            debug_cdc_event("MIC_RECOVERY_CHANNEL_CLOSE", aacp_cid, mic_restart_attempts);
            l2cap_disconnect(aacp_cid);
            return; // CLOSED tears down and schedules a new AACP handshake
        }
        mic_restart_attempts++;
        printf("[MIC] stall watchdog: restarting stream (STOP + START)\n");
        aacp_mic_pcm_reset();
        mic_timeline = (au_timeline_t){0};
        aacp_tx_enqueue(aacp_mic_stop_bytes,  sizeof(aacp_mic_stop_bytes));
        aacp_tx_enqueue(aacp_mic_start_bytes, sizeof(aacp_mic_start_bytes));
        mic_stats.last_sdu_ms = now;   // give the restart a fresh 2s window
    }

    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

void aacp_mic_start(void) {
    aacp_mic_want = true;
    if (aacp_cid == 0 || !aacp_connected || !aacp_got_control) {
        printf("[MIC] start deferred — AACP channel not open yet\n");
        return;
    }
    if (aacp_mic_running) return;
    if (!aacp_tx_enqueue(aacp_mic_start_bytes, sizeof(aacp_mic_start_bytes))) return;
    aacp_mic_running = true;
    mic_restart_attempts = 0;
    mic_timeline = (au_timeline_t){0};
    mic_timestamp_gaps = mic_timestamp_drops = 0;

    memset(&mic_stats, 0, sizeof(mic_stats));
    mic_stats.au_min = 0xFFFF;
    mic_stats.last_sdu_ms = btstack_run_loop_get_time_ms();

    // The decoder was opened at boot (thread context — opening it here, in
    // the BTstack callback, hung the system). Stats-only if boot init failed.
    if (!aacp_mic_dec_ready()) {
        printf("[MIC] WARNING: decoder unavailable, continuing stats-only\n");
    }
    aacp_mic_dec_reset_stats();
    aacp_mic_pcm_reset();   // fresh USB PCM session

    printf("[MIC] sending START (hi-res mic, AAC-ELD mono 64kHz expected)\n");

    btstack_run_loop_remove_timer(&aacp_mic_stats_timer);
    btstack_run_loop_set_timer_handler(&aacp_mic_stats_timer, aacp_mic_stats_report);
    btstack_run_loop_set_timer(&aacp_mic_stats_timer, 1000);
    btstack_run_loop_add_timer(&aacp_mic_stats_timer);
}

void aacp_mic_stop(void) {
    aacp_mic_want = false;
    if (!aacp_mic_running) return;
    printf("[MIC] sending STOP (total: %lu SDUs, %lu AUs)\n",
           (unsigned long) mic_stats.sdu_total, (unsigned long) mic_stats.au_total);
    if (aacp_cid != 0 && aacp_connected) {
        aacp_tx_enqueue(aacp_mic_stop_bytes, sizeof(aacp_mic_stop_bytes));
    }
    aacp_mic_teardown();
}

bool aacp_mic_active(void) {
    return aacp_mic_running;
}

// Stop timers / clear state without sending anything (channel may be gone).
static void aacp_mic_teardown(void) {
    aacp_mic_running = false;
    aacp_mic_pcm_reset();
    btstack_run_loop_remove_timer(&aacp_mic_stats_timer);
}

// --- type-0x58 audio SDU handling (port of librepods aacp_audio.rs) ---

// Predicate for 0x58 *audio* frames (subtype 0x0001).
static bool aacp_is_audio_sdu(const uint8_t *sdu, uint16_t size) {
    return size >= 8
        && sdu[0] == 0x04
        && sdu[2] == 0x04
        && little_endian_read_16(sdu, 4) == 0x0058
        && little_endian_read_16(sdu, 6) == 0x0001;
}

// Walk the sub-frames of one 0x58 audio SDU: 22-byte header, then
// N x [timestamp:u32 LE][au_len:u8][AU bytes]. Each AU goes to the AAC-ELD
// decoder; size statistics feed the 1 Hz report.
static void aacp_handle_audio_sdu(const uint8_t *sdu, uint16_t size) {
    if (!aacp_mic_running || size < TYPE58_HEADER_LEN) return;
    mic_stats.sdu_total++;
    mic_stats.sdu_interval++;
    if (size > mic_stats.sdu_max) mic_stats.sdu_max = size;

    uint16_t off = TYPE58_HEADER_LEN;
    while ((uint32_t) off + 5 <= size) {
        uint8_t au_len = sdu[off + 4];
        uint32_t timestamp = little_endian_read_32(sdu, off);
        uint16_t start = off + 5;
        uint32_t end   = (uint32_t) start + au_len;
        if (end > size) {          // truncated trailer — count and stop
            mic_stats.demux_short++;
            break;
        }
        if (!au_len) { mic_stats.demux_short++; break; }
        au_timeline_result_t timing = au_timeline_observe(&mic_timeline, timestamp);
        mic_timestamp_gaps += timing.lost;
        if (timing.drop) { mic_timestamp_drops++; off = (uint16_t)end; continue; }
        if (aacp_mic_dec_enqueue(&sdu[start], au_len, timestamp, timing.lost, timing.discontinuity)) {
            mic_stats.last_sdu_ms = btstack_run_loop_get_time_ms();
        }
        mic_stats.au_total++;
        mic_stats.au_interval++;
        mic_stats.au_bytes_interval += au_len;
        if (au_len < mic_stats.au_min) mic_stats.au_min = au_len;
        if (au_len > mic_stats.au_max) mic_stats.au_max = au_len;
        off = (uint16_t) end;
    }
}

// ------------------------------------------------------------------

static void aacp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case L2CAP_EVENT_CHANNEL_OPENED: {
                    uint8_t  status = l2cap_event_channel_opened_get_status(packet);
                    uint16_t cid    = l2cap_event_channel_opened_get_local_cid(packet);
                    if (!aacp_session_active || cid != aacp_cid) {
                        if (status == ERROR_CODE_SUCCESS) l2cap_disconnect(cid);
                        return;
                    }
                    if (status != ERROR_CODE_SUCCESS) {
                        printf("[AACP] channel open FAILED: status 0x%02x\n", status);
                        aacp_cid       = 0;
                        aacp_connected = false;
                        aacp_schedule_retry();
                        return;
                    }
                    aacp_cid       = cid;
                    aacp_connected = true;
                    aacp_battery_reset(&battery_state, true);
                    aacp_anc_reset(&anc_state);
                    aacp_got_control = false;
                    printf("[AACP] channel OPEN cid 0x%04x remote_mtu %u — starting handshake\n",
                           cid, l2cap_event_channel_opened_get_remote_mtu(packet));
                    aacp_queue_init_sequence();
                    break;
                }
                case L2CAP_EVENT_CAN_SEND_NOW:
                    if (l2cap_event_can_send_now_get_local_cid(packet) != aacp_cid) break;
                    aacp_handle_can_send_now();
                    break;
                case L2CAP_EVENT_CHANNEL_CLOSED:
                    if (l2cap_event_channel_closed_get_local_cid(packet) != aacp_cid) break;
                    aacp_battery_reset(&battery_state, false);
                    aacp_anc_reset(&anc_state);
                    printf("[AACP] channel CLOSED\n");
                    aacp_cid       = 0;
                    aacp_connected = false;
                    aacp_got_control = false;
                    aacp_tx_head   = 0;
                    aacp_tx_count  = 0;
                    btstack_run_loop_remove_timer(&aacp_tx_timer);
                    aacp_mic_teardown();
                    aacp_schedule_retry();
                    break;
                default:
                    break;
            }
            break;

        case L2CAP_DATA_PACKET:
            if (channel != aacp_cid || !aacp_connected) break;
            // Audio SDUs first (hot path once the mic runs — stats only, no logs).
            if (aacp_is_audio_sdu(packet, size)) {
                aacp_handle_audio_sdu(packet, size);
                break;
            }

            // Control packets: 04 00 04 00 data header + u16le opcode.
            // Not logged per packet — the AirPods send occasional multi-
            // hundred-byte info bursts that must not stall the run loop.
            if (size >= 6 && packet[0] == 0x04 && packet[1] == 0x00 &&
                packet[2] == 0x04 && packet[3] == 0x00) {
                airpods_case_control(packet, size);
                aacp_anc_parse(&anc_state, packet, size);
                aacp_battery_parse(&battery_state, packet, size, btstack_run_loop_get_time_ms());
                if (!aacp_got_control) {
                    aacp_got_control = true;
                    aacp_attempts = 0; // a recovered channel gets a fresh retry budget
                    printf("[AACP] control channel confirmed (first packet)\n");
                    // Host already holds the mic open (USB alt 1)? Fire the
                    // deferred START now that the channel is confirmed.
                    if (aacp_mic_want && !aacp_mic_running) {
                        printf("[MIC] firing deferred START\n");
                        aacp_mic_start();
                    }
                }
            }
            break;

        default:
            break;
    }
}

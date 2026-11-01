// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 han-um
//
// Debug logging over USB CDC — see debug_cdc.h.
//
// printf() may be called from both thread context (BTstack run loop on Core 0)
// and IRQ context (the 500us USB timer that drives tud_task and its callbacks).
// To keep the TinyUSB CDC FIFO single-writer, all printf output first lands in
// an interrupt-safe ring buffer (producers briefly disable IRQs), and only the
// USB task context (debug_cdc_task) ever touches tud_cdc_*.
//

#include <stdint.h>
#include <stdio.h>

#include "pico/stdio/driver.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "tusb.h"

#include "debug_cdc.h"

#include "battery_query.h"

static volatile bool battery_query_pending;
static volatile uint32_t battery_query_nonce;
static volatile bool anc_pending;
static volatile uint32_t anc_nonce, anc_session;
static volatile unsigned anc_mode;
static char query_line[32];
static unsigned query_length;
static bool query_overflow;

bool debug_cdc_take_anc_request(uint32_t *nonce, uint32_t *session, unsigned *mode) {
    uint32_t save = save_and_disable_interrupts();
    bool pending = anc_pending;
    if (pending) {
        *nonce = anc_nonce; *session = anc_session; *mode = anc_mode;
        anc_pending = false;
    }
    restore_interrupts(save);
    return pending;
}

bool debug_cdc_take_battery_request(uint32_t *nonce) {
    uint32_t save = save_and_disable_interrupts();
    bool pending = battery_query_pending;
    if (pending) {
        *nonce = battery_query_nonce;
        battery_query_pending = false;
    }
    restore_interrupts(save);
    return pending;
}

static void debug_cdc_read_queries(void) {
    char chunk[32];
    uint32_t count = tud_cdc_read(chunk, sizeof(chunk));
    for (uint32_t i = 0; i < count; ++i) {
        char c = chunk[i];
        if (c == '\n') {
            uint32_t nonce;
            uint32_t session;
            unsigned mode;
            query_line[query_length] = 0;
            if (!query_overflow && battery_query_parse(query_line, &nonce)) {
                battery_query_nonce = nonce;
                battery_query_pending = true;
            }
            if (!query_overflow && !anc_pending && anc_query_parse(query_line, &nonce, &session, &mode)) {
                anc_nonce = nonce; anc_session = session; anc_mode = mode;
                anc_pending = true;
            }
            query_length = 0;
            query_overflow = false;
        } else if (c != '\r') {
            if (c == 0 || query_length == sizeof(query_line) - 1) query_overflow = true;
            else if (!query_overflow) query_line[query_length++] = c;
        }
    }
}


#define DBG_BUF_SIZE 8192u                 // must be a power of two
#define DBG_BUF_MASK (DBG_BUF_SIZE - 1u)

static uint8_t           dbg_buf[DBG_BUF_SIZE];
static volatile uint32_t dbg_head = 0;     // producer index
static volatile uint32_t dbg_tail = 0;     // consumer index (USB task only)
static volatile uint32_t dbg_dropped = 0;  // bytes discarded while the ring was full

#define EVENT_HISTORY_LEN 32u
typedef struct {
    const char *tag;
    uint32_t ms, arg0, arg1;
} debug_event_t;
static debug_event_t event_history[EVENT_HISTORY_LEN];
static debug_event_t event_snapshot[EVENT_HISTORY_LEN];
static uint32_t event_total;
static unsigned snapshot_count, snapshot_next;
static bool terminal_was_connected;

void debug_cdc_event(const char *tag, uint32_t arg0, uint32_t arg1) {
    debug_event_t event = {tag, to_ms_since_boot(get_absolute_time()), arg0, arg1};
    uint32_t flags = save_and_disable_interrupts();
    event_history[event_total % EVENT_HISTORY_LEN] = event;
    event_total++;
    restore_interrupts(flags);
    printf("[EVENT] ms=%lu %s a=%lu b=%lu\n", (unsigned long)event.ms,
           tag, (unsigned long)arg0, (unsigned long)arg1);
}

// Producer side. Runs with IRQs disabled so thread- and IRQ-context printf
// calls can't interleave, and so the USB-task consumer can't preempt a push.
static void dbg_push_locked(uint8_t c) {
    uint32_t next = (dbg_head + 1u) & DBG_BUF_MASK;
    if (next == dbg_tail) {                // full: drop (debug output is best-effort)
        dbg_dropped++;                     // ...but make the gap self-evident later
        return;
    }
    dbg_buf[dbg_head] = c;
    dbg_head = next;
}

static void cdc_out_chars(const char *buf, int len) {
    uint32_t save = save_and_disable_interrupts();
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') dbg_push_locked('\r');  // make output terminal-friendly
        dbg_push_locked((uint8_t) c);
    }
    restore_interrupts(save);
}

static stdio_driver_t debug_cdc_driver = {
    .out_chars = cdc_out_chars,
    .out_flush = NULL,
    .in_chars  = NULL,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = false,   // we insert CR ourselves above
#endif
};

void debug_cdc_init(void) {
    stdio_set_driver_enabled(&debug_cdc_driver, true);
}

void debug_cdc_task(void) {
    // Drain only while a terminal is actually attached (DTR asserted). Until
    // then, output accumulates in the 8KB ring, so boot/pairing logs are still
    // delivered when the user opens the terminal late.
    if (!tud_cdc_connected()) {
        terminal_was_connected = false;
        query_length = 0; query_overflow = false;
        battery_query_pending = false; anc_pending = false;
        return;
    }
    debug_cdc_read_queries(); // bounded to 32 bytes per USB tick
    if (!terminal_was_connected) {
        terminal_was_connected = true;
        uint32_t flags = save_and_disable_interrupts();
        snapshot_count = event_total < EVENT_HISTORY_LEN ? event_total : EVENT_HISTORY_LEN;
        snapshot_next = 0;
        uint32_t first = event_total - snapshot_count;
        for (unsigned i = 0; i < snapshot_count; i++)
            event_snapshot[i] = event_history[(first + i) % EVENT_HISTORY_LEN];
        restore_interrupts(flags);
    }

    // Replay one retained event per tick, only once old stdout has drained.
    // The snapshot preserves ordering if new events arrive during replay.
    if (snapshot_next < snapshot_count &&
        ((dbg_head - dbg_tail) & DBG_BUF_MASK) < DBG_BUF_SIZE / 2u) {
        debug_event_t *event = &event_snapshot[snapshot_next++];
        printf("[HISTORY] ms=%lu %s a=%lu b=%lu\n", (unsigned long)event->ms,
               event->tag, (unsigned long)event->arg0, (unsigned long)event->arg1);
    }

    // If bytes were discarded while the ring was full, report the gap once the
    // ring has room again, so a log gap is never silent. (We run in the USB
    // timer IRQ; producers push with IRQs disabled, so guard the same way.)
    if (dbg_dropped != 0 && ((dbg_head - dbg_tail) & DBG_BUF_MASK) < DBG_BUF_SIZE / 2u) {
        char msg[48];
        int n = snprintf(msg, sizeof(msg), "\r\n[log dropped %lu bytes]\r\n",
                         (unsigned long) dbg_dropped);
        uint32_t save = save_and_disable_interrupts();
        dbg_dropped = 0;
        for (int i = 0; i < n; i++) dbg_push_locked((uint8_t) msg[i]);
        restore_interrupts(save);
    }

    if (dbg_tail == dbg_head) return;

    // Bounded work per call: at most ONE 64-byte chunk per tick. This runs in
    // the 500us USB timer IRQ; an unbounded drain loop under heavy printf
    // traffic could hog the IRQ.
    uint32_t avail = tud_cdc_write_available();
    if (avail == 0) return;

    uint8_t  tmp[64];
    uint32_t n = 0;
    while (n < avail && n < sizeof(tmp) && dbg_tail != dbg_head) {
        tmp[n++] = dbg_buf[dbg_tail];
        dbg_tail = (dbg_tail + 1u) & DBG_BUF_MASK;
    }
    if (n == 0) return;
    tud_cdc_write(tmp, n);
    tud_cdc_write_flush();
}

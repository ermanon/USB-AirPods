// SPDX-License-Identifier: GPL-3.0-only
// Identity and proximity interpretation follows LibrePods linux/ble sources.
// IRK stays in RAM for the selected peer; no key material goes to CDC or files.
#include <string.h>
#include "airpods_case.h"
#include "airpods_case_policy.h"
#include "btstack_crypto.h"
#include "btstack_avdtp_source.h"
#include "../tinyusb/debug_cdc.h"

static bd_addr_t case_peer;
static uint8_t case_irk[16];
static bool peer_valid, key_valid, connected, both_in_case, scanning;
static uint32_t warmup_ms;
static bool warmup;
static airpods_case_policy_t policy;
static btstack_timer_source_t poll_timer;
static btstack_packet_callback_registration_t event_registration;
static bool ready;

static void update_scan(void) {
    bool wanted = ready && key_valid && (!connected || both_in_case || warmup || policy.parked);
    if (wanted == scanning) return;
    scanning = wanted;
    if (wanted) {
        // Passive scanning: no BLE connection and no scan requests to other devices.
        gap_set_scan_parameters(0, 160, 16); // 100 ms period / 10 ms window
        gap_start_scan();
    } else gap_stop_scan();
}

void airpods_case_reset(void) {
    peer_valid = key_valid = connected = both_in_case = warmup = false;
    memset(case_irk, 0, sizeof(case_irk));
    memset(case_peer, 0, sizeof(case_peer));
    policy = (airpods_case_policy_t){0};
    update_scan();
}

bool airpods_case_parked(void) { return policy.parked; }

void airpods_case_select_peer(const bd_addr_t peer) {
    if (!peer_valid || memcmp(peer, case_peer, sizeof(case_peer))) {
        airpods_case_reset();
        memcpy(case_peer, peer, sizeof(case_peer));
        peer_valid = true;
    }
}

void airpods_case_session(const bd_addr_t peer, bool is_connected) {
    airpods_case_select_peer(peer);
    connected = is_connected;
    if (!connected) both_in_case = false;
    update_scan();
}

void airpods_case_control(const uint8_t *packet, uint16_t size) {
    bool in_case;
    if (airpods_ear_packet(packet, size, &in_case)) {
        debug_cdc_event("EAR_STATE", packet[6], packet[7]);
        both_in_case = in_case;
        update_scan();
    }
    uint8_t key[16];
    if (peer_valid && airpods_irk_packet(packet, size, key)) {
        memcpy(case_irk, key, sizeof(key));
        memset(key, 0, sizeof(key));
        key_valid = true;
        warmup = true;
        warmup_ms = btstack_run_loop_get_time_ms();
        debug_cdc_event("CASE_ID_READY", 1, 0);
        update_scan();
    }
}

static bool case_address_matches(const bd_addr_t address) {
    // BTstack bd_addr_t is canonical display order. IRK from AACP is LE.
    if (!key_valid || (address[0] & 0xc0) != 0x40) return false;
    uint8_t key[16], input[16] = {0}, output[16];
    for (unsigned i = 0; i < 16; i++) key[i] = case_irk[15 - i];
    memcpy(input + 13, address, 3);
    btstack_aes128_calc(key, input, output);
    bool match = !memcmp(output + 13, address + 3, 3);
    memset(key, 0, sizeof(key));
    return match;
}

static void case_event(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    if (type != HCI_EVENT_PACKET || size < 2) return;
    if (packet[0] == BTSTACK_EVENT_STATE) {
        ready = btstack_event_state_get_state(packet) == HCI_STATE_WORKING;
        update_scan();
        return;
    }
    if (packet[0] != GAP_EVENT_ADVERTISING_REPORT || size < 12 || !scanning) return;
    uint8_t length = gap_event_advertising_report_get_data_length(packet);
    if ((unsigned)length + 12 > size || gap_event_advertising_report_get_address_type(packet) != BD_ADDR_TYPE_LE_RANDOM) return;
    const uint8_t *data = gap_event_advertising_report_get_data(packet);
    airpods_proximity_t proximity;
    bool found = false;
    for (unsigned off = 0; off < length;) {
        unsigned n = data[off++];
        if (!n) break;
        if (n > length - off) return;
        if (n >= 3 && data[off] == 0xff && data[off + 1] == 0x4c && data[off + 2] == 0 &&
            airpods_proximity_parse(data + off + 3, n - 3, &proximity)) found = true;
        off += n;
    }
    if (!found) return;
    bd_addr_t address;
    gap_event_advertising_report_get_address(packet, address);
    if (!case_address_matches(address)) return; // ignore other people's AirPods
    static unsigned previous = 0xffff;
    unsigned state = proximity.both_in_case | (proximity.in_ear << 1) |
        (proximity.lid_known << 2) | (proximity.lid_closed << 3);
    if (state != previous) {
        debug_cdc_event("CASE_PROXIMITY", state, proximity.model);
        previous = state;
    }
    bool was_parked = policy.parked;
    airpods_case_observe(&policy, &proximity, btstack_run_loop_get_time_ms());
    if (was_parked && !policy.parked) {
        // Keep listening while an open case still contains both buds, so
        // reclosing without wearing can park even before AACP comes back.
        both_in_case = proximity.both_in_case;
        debug_cdc_event("CASE_WAKE", state, 0);
        bt_audio_case_state_changed(false);
        update_scan();
    }
}

static void case_poll(btstack_timer_source_t *timer) {
    uint32_t now = btstack_run_loop_get_time_ms();
    if (warmup && now - warmup_ms >= 5000) { warmup = false; update_scan(); }
    if (airpods_case_poll(&policy, now)) {
        debug_cdc_event("CASE_SLEEP", 1, 0);
        bt_audio_case_state_changed(true);
        update_scan();
    }
    btstack_run_loop_set_timer(timer, 100);
    btstack_run_loop_add_timer(timer);
}

void airpods_case_init(void) {
    event_registration.callback = case_event;
    hci_add_event_handler(&event_registration);
    btstack_run_loop_set_timer_handler(&poll_timer, case_poll);
    btstack_run_loop_set_timer(&poll_timer, 100);
    btstack_run_loop_add_timer(&poll_timer);
}

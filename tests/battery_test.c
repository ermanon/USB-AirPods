#include <assert.h>
#include <stdio.h>
#include "../src/btstack/aacp_battery.h"
#include "../src/tinyusb/battery_query.h"
#include "../src/btstack/aacp_anc.h"

int main(void) {
    aacp_anc_state anc = {0};
    aacp_anc_reset(&anc);
    uint8_t noise[] = {4,0,4,0,9,0,13,2,0,0,0};
    for (size_t n = 0; n < sizeof(noise); n++) assert(!aacp_anc_parse(&anc, noise, n));
    assert(!anc.mode && !anc.mask);
    assert(aacp_anc_parse(&anc, noise, sizeof(noise)) && anc.mode == 2 && anc.mask == 2);
    assert(aacp_anc_allowed(&anc, anc.session, 2));
    assert(!aacp_anc_allowed(&anc, anc.session, 4));
    assert(!aacp_anc_allowed(&anc, anc.session + 1, 2));
    noise[7] = 4;
    assert(aacp_anc_parse(&anc, noise, sizeof(noise)) && anc.mask == 10);
    noise[7] = 5;
    assert(!aacp_anc_parse(&anc, noise, sizeof(noise)) && anc.mode == 4);
    noise[7] = 2; noise[6] = 14;
    assert(!aacp_anc_parse(&anc, noise, sizeof(noise)));
    aacp_anc_reset(&anc);
    assert(anc.mode == 0 && anc.mask == 0);
    uint32_t command_nonce, session;
    unsigned mode;
    assert(anc_query_parse("USBPODS ANC 12345678 ABCDEF01 4", &command_nonce, &session, &mode));
    assert(command_nonce == 0x12345678 && session == 0xABCDEF01 && mode == 4);
    assert(!anc_query_parse("USBPODS ANC 12345678 ABCDEF01 5", &command_nonce, &session, &mode));
    assert(!anc_query_parse("USBPODS ANC 12345678 ABCDEFG1 4", &command_nonce, &session, &mode));
    assert(!anc_query_parse("USBPODS ANC 12345678 ABCDEF01 4x", &command_nonce, &session, &mode));
    aacp_battery_state s, saved;
    uint8_t packet[] = {4,0,4,0,4,0,3, 2,1,100,2,1, 4,1,99,1,1, 8,1,17,2,1};
    aacp_battery_reset(&s, false);
    assert(!aacp_battery_parse(&s, packet, sizeof(packet), 100));
    aacp_battery_reset(&s, true);
    assert(aacp_battery_parse(&s, packet, sizeof(packet), 100));
    assert(s.values[0].level == 99 && s.values[0].status == 1);
    assert(s.values[1].level == 100 && s.values[1].status == 2);
    assert(s.values[2].level == 17 && s.values[2].updated_ms == 100);
    saved = s;
    for (size_t n = 0; n < sizeof(packet); ++n) {
        assert(!aacp_battery_parse(&s, packet, n, 200));
        assert(memcmp(&s, &saved, sizeof(s)) == 0);
    }
    packet[21] = 0;
    assert(!aacp_battery_parse(&s, packet, sizeof(packet), 200));
    assert(memcmp(&s, &saved, sizeof(s)) == 0);
    packet[21] = 1;
    packet[12] = 2; // Duplicate right component: reject entire packet.
    assert(!aacp_battery_parse(&s, packet, sizeof(packet), 200));
    packet[12] = 4;
    packet[9] = 255;
    assert(aacp_battery_parse(&s, packet, sizeof(packet), 200) && s.values[1].level == -1);
    packet[9] = 0;
    assert(aacp_battery_parse(&s, packet, sizeof(packet), 300) && s.values[1].level == 0);
    packet[10] = 4;
    assert(aacp_battery_parse(&s, packet, sizeof(packet), 400) && s.values[1].level == -1);
    uint8_t single[] = {4,0,4,0,4,0,1, 8,1,65,1,1};
    assert(aacp_battery_parse(&s, single, sizeof(single), 500));
    assert(s.values[0].updated_ms == 400 && s.values[2].updated_ms == 500);
    assert(s.values[2].level == 65 && s.values[2].status == 1);
    aacp_battery_reset(&s, false);
    assert(!s.connected && s.values[0].level == -1 && !s.values[2].seen);
    uint32_t nonce = 0;
    assert(battery_query_parse("USBPODS BATTERY 1234abcd", &nonce) && nonce == 0x1234abcd);
    assert(battery_query_parse("USBPODS BATTERY FFFFFFFF", &nonce) && nonce == UINT32_MAX);
    assert(!battery_query_parse("USBPODS BATTERY FFFFFFF", &nonce));
    assert(!battery_query_parse("USBPODS BATTERY FFFFFFFFG", &nonce));
    assert(!battery_query_parse("USBPODS BATTERY 1234567G", &nonce));
    assert(!battery_query_parse("noise", &nonce));
    puts("PASS: firmware battery parsing, truncation, partial updates, disconnect, USB nonce queries");
    return 0;
}

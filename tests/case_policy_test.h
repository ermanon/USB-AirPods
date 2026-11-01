#include "../src/btstack/airpods_case_policy.h"
#include "../src/btstack/case_wake_retry.h"

static void run_case_tests(void) {
    case_wake_retry_t retry = {0};
    CHECK(case_wake_page_units(&retry, 0) == NORMAL_PAGE_UNITS);
    CHECK(case_wake_retry_delay(&retry, 0, 5000) == 5000);
    retry = (case_wake_retry_t){true, 100};
    CHECK(case_wake_page_units(&retry, 100) == CASE_WAKE_PAGE_UNITS);
    CHECK(case_wake_retry_delay(&retry, 100, 5000) == 1000);
    CHECK(case_wake_retry_delay(&retry, 100, CASE_WAKE_SETTLE_MS) == CASE_WAKE_SETTLE_MS);
    CHECK(case_wake_retry_delay(&retry, 100, 100) == 100);
    CHECK(case_wake_fast(&retry, 30099));
    CHECK(!case_wake_fast(&retry, 30100));
    CHECK(case_wake_page_units(&retry, 30100) == NORMAL_PAGE_UNITS);
    CHECK(case_wake_retry_delay(&retry, 30100, 5000) == 5000);
    retry.started_ms = 0xffffff00u;
    CHECK(case_wake_fast(&retry, 0x742fu));
    CHECK(!case_wake_fast(&retry, 0x7430u));
    retry.active = false;
    CHECK(case_wake_page_units(&retry, 0) == NORMAL_PAGE_UNITS);
    bool both = false;
    uint8_t ear[] = {4, 0, 4, 0, 6, 0, 2, 2};
    CHECK(airpods_ear_packet(ear, 8, &both) && both);
    for (unsigned a = 0; a < 256; a++) {
        ear[6] = a;
        for (unsigned b = 0; b < 3; b++) {
            ear[7] = b;
            bool valid = airpods_ear_packet(ear, 8, &both);
            CHECK(valid == (a <= 2));
            if (valid) CHECK(both == (a == 2 && b == 2));
        }
    }
    ear[6] = ear[7] = 2;
    for (unsigned n = 0; n < 8; n++) CHECK(!airpods_ear_packet(ear, n, &both));
    ear[0] = 0;
    CHECK(!airpods_ear_packet(ear, 8, &both));

    uint8_t keys[47] = {4, 0, 4, 0, 0x31, 0, 2, 1, 0, 16, 0};
    for (unsigned i = 0; i < 16; i++) keys[11 + i] = i + 1;
    keys[27] = 4; keys[29] = 16;
    uint8_t key[16] = {0};
    CHECK(airpods_irk_packet(keys, sizeof(keys), key));
    CHECK(key[0] == 1 && key[15] == 16);
    for (unsigned n = 0; n < sizeof(keys); n++) CHECK(!airpods_irk_packet(keys, n, key));
    keys[10] = 1;
    CHECK(!airpods_irk_packet(keys, sizeof(keys), key));
    keys[10] = 0; keys[6] = 255;
    CHECK(!airpods_irk_packet(keys, sizeof(keys), key));
    keys[6] = 2; keys[8] = 1;
    CHECK(!airpods_irk_packet(keys, sizeof(keys), key));
    keys[8] = 0;
    for (unsigned i = 0; i < 16; i++) keys[11 + i] = 0;
    CHECK(!airpods_irk_packet(keys, sizeof(keys), key));

    uint8_t advertisement[27] = {7, 25, 1, 0x19, 0x20, 0x55, 0, 0, 8};
    airpods_proximity_t p;
    CHECK(airpods_proximity_parse(advertisement, 27, &p));
    CHECK(p.both_in_case && !p.in_ear && p.lid_known && p.lid_closed);
    for (unsigned n = 0; n < 27; n++) CHECK(!airpods_proximity_parse(advertisement, n, &p));
    advertisement[2] = 0;
    CHECK(!airpods_proximity_parse(advertisement, 27, &p));
    advertisement[2] = 1; advertisement[3] = 0x0a;
    CHECK(!airpods_proximity_parse(advertisement, 27, &p)); // AirPods Max
    advertisement[3] = 0x19;
    CHECK(airpods_proximity_parse(advertisement, 27, &p));

    airpods_case_policy_t s = {0};
    airpods_case_observe(&s, &p, 100);
    CHECK(!airpods_case_poll(&s, 800)); // one report insufficient
    airpods_case_observe(&s, &p, 400);
    CHECK(!airpods_case_poll(&s, 799));
    CHECK(airpods_case_poll(&s, 800) && s.parked);
    CHECK(!airpods_case_poll(&s, 900)); // one close action
    p.lid_closed = false; p.lid_known = false;
    airpods_case_observe(&s, &p, 950);
    CHECK(s.parked); // unknown lid must not wake, even with open bit
    p.lid_known = true;
    airpods_case_observe(&s, &p, 1000);
    CHECK(!s.parked && !s.close_pending); // allow accessory's lid-open connection
    // Closing again before wear must park normally, not stay in preconnect.
    p.lid_closed = true;
    airpods_case_observe(&s, &p, 1010);
    airpods_case_observe(&s, &p, 1110);
    CHECK(airpods_case_poll(&s, 1710) && s.parked);
    p.both_in_case = false; p.in_ear = false; p.lid_known = false;
    airpods_case_observe(&s, &p, 1800);
    CHECK(s.parked); // outside case alone is not worn
    p.in_ear = true;
    airpods_case_observe(&s, &p, 1900);
    CHECK(!s.parked); // one worn earbud is enough

    p = (airpods_proximity_t){ .both_in_case=true, .lid_known=true, .lid_closed=true };
    s = (airpods_case_policy_t){0};
    airpods_case_observe(&s, &p, 0xffffff00u);
    airpods_case_observe(&s, &p, 0xffffff80u);
    CHECK(airpods_case_poll(&s, 0x1bcu)); // 700 ms wrap
    s = (airpods_case_policy_t){0};
    airpods_case_observe(&s, &p, 100);
    airpods_case_observe(&s, &p, 200);
    CHECK(!airpods_case_poll(&s, 2201)); // no stale close
    airpods_case_observe(&s, &p, 2300);
    CHECK(s.closed_reports == 1);
    p.lid_known = false;
    airpods_case_observe(&s, &p, 2400);
    CHECK(s.close_pending && !airpods_case_poll(&s, 9999)); // unknown does not refresh age
    s = (airpods_case_policy_t){0};
    airpods_case_observe(&s, &p, 3000);
    airpods_case_observe(&s, &p, 3100);
    CHECK(!s.close_pending && !airpods_case_poll(&s, 4000)); // unknown cannot initiate
    p.lid_known = true;
    airpods_case_observe(&s, &p, 4100);
    p.lid_known = false;
    airpods_case_observe(&s, &p, 4300);
    p.lid_known = true;
    airpods_case_observe(&s, &p, 4500);
    p.lid_known = false;
    airpods_case_observe(&s, &p, 4700);
    CHECK(airpods_case_poll(&s, 4800)); // alternating pods seen on AirPods 4 ANC
    s = (airpods_case_policy_t){0};
    p.lid_known = true;
    airpods_case_observe(&s, &p, 5000);
    p.lid_closed = false;
    airpods_case_observe(&s, &p, 5100);
    CHECK(!s.close_pending && !airpods_case_poll(&s, 6000));
    p.lid_closed = true;
    p.lid_known = true; p.both_in_case = false;
    airpods_case_observe(&s, &p, 10000);
    airpods_case_observe(&s, &p, 11000);
    CHECK(!airpods_case_poll(&s, 11500)); // single bud in case
    p.both_in_case = true;
    airpods_case_observe(&s, &p, 12000);
    p.in_ear = true;
    airpods_case_observe(&s, &p, 12100);
    CHECK(!s.close_pending); // contradictory status cannot park
}

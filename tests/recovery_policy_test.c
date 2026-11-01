#include "../src/btstack/audio_recovery.h"
#include "../src/btstack/au_timeline.h"
#include "../src/btstack/audio_packet.h"

static unsigned checks, failures, first_failure;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; if (!first_failure) first_failure = __LINE__; } } while (0)

// Freestanding ARM harness; Unicorn reads the result block after return.
void *memset(void *p, int value, unsigned size) {
    unsigned char *dst = p;
    while (size--) *dst++ = value;
    return p;
}
void *memcpy(void *dst, const void *src, unsigned size) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (size--) *d++ = *s++;
    return dst;
}

#include "case_policy_test.h"
void run_case_identity_tests(void);
void run_negotiation_tests(void);

void run_tests(void) {
    run_case_tests();
    audio_recovery_t r = {0};
    CHECK(audio_recovery_accept(&r, 1, 2, false, 0) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_poll(&r, 4000) == AUDIO_RECOVERY_NONE);
    audio_recovery_enter(&r, AUDIO_RECOVERY_SUSPEND, 1, 2, 100);
    CHECK(audio_recovery_accept(&r, 9, 2, false, 110) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_accept(&r, 1, 9, false, 110) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_accept(&r, 1, 2, true, 110) == AUDIO_RECOVERY_NONE);
    CHECK(r.entered_ms == 100 && r.state == AUDIO_RECOVERY_SUSPEND);
    CHECK(audio_recovery_poll(&r, 3099) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_poll(&r, 3100) == AUDIO_RECOVERY_RECONNECT);
    audio_recovery_enter(&r, AUDIO_RECOVERY_SUSPEND, 1, 2, 100);
    CHECK(audio_recovery_accept(&r, 1, 2, false, 500) == AUDIO_RECOVERY_SEND_START);
    CHECK(r.state == AUDIO_RECOVERY_START && r.entered_ms == 500);
    CHECK(audio_recovery_poll(&r, 3499) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_poll(&r, 3500) == AUDIO_RECOVERY_RECONNECT);
    CHECK(audio_recovery_accept(&r, 1, 2, true, 600) == AUDIO_RECOVERY_RESUME);
    CHECK(r.state == AUDIO_RECOVERY_VERIFY);
    CHECK(audio_recovery_accept(&r, 1, 2, true, 650) == AUDIO_RECOVERY_NONE);
    CHECK(r.entered_ms == 600);
    for (unsigned i = 0; i < 10; i++) audio_recovery_tx(&r, 1000 + i * 10);
    CHECK(audio_recovery_poll(&r, 1099) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_poll(&r, 1100) == AUDIO_RECOVERY_HEALTHY);
    CHECK(r.state == AUDIO_RECOVERY_IDLE);
    audio_recovery_enter(&r, AUDIO_RECOVERY_VERIFY, 1, 2, 0);
    for (unsigned i = 0; i < 10; i++) audio_recovery_tx(&r, 100);
    CHECK(audio_recovery_poll(&r, 500) == AUDIO_RECOVERY_NONE); // stale TX
    CHECK(audio_recovery_poll(&r, 3000) == AUDIO_RECOVERY_RECONNECT);
    audio_recovery_enter(&r, AUDIO_RECOVERY_VERIFY, 1, 2, 0);
    audio_recovery_tx(&r, 500);
    CHECK(audio_recovery_poll(&r, 500) == AUDIO_RECOVERY_NONE); // too little flow
    audio_recovery_enter(&r, AUDIO_RECOVERY_DISCONNECT, 1, 2, 0);
    CHECK(audio_recovery_accept(&r, 1, 2, true, 10) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_poll(&r, 9000) == AUDIO_RECOVERY_NONE);
    audio_recovery_enter(&r, AUDIO_RECOVERY_START, 1, 2, 0xffffff00u);
    CHECK(audio_recovery_poll(&r, 0x00000900u) == AUDIO_RECOVERY_NONE);
    CHECK(audio_recovery_poll(&r, 0x00000ab8u) == AUDIO_RECOVERY_RECONNECT);
    audio_recovery_clear(&r);
    CHECK(!audio_recovery_matches(&r, 1, 2));

    au_timeline_t timeline = {0};
    au_timeline_result_t t = au_timeline_observe(&timeline, 1000);
    CHECK(!t.drop && !t.lost && !t.discontinuity);
    // Never infer losses until the proprietary time field has a stable cadence.
    for (unsigned i = 1; i <= 32; i++) {
        t = au_timeline_observe(&timeline, 1000 + i * 480);
        CHECK(!t.drop && !t.lost && !t.discontinuity);
    }
    CHECK(timeline.confidence == 32 && timeline.step == 480);
    t = au_timeline_observe(&timeline, 1000 + 34 * 480);
    CHECK(t.lost == 1 && !t.drop && !t.discontinuity);
    t = au_timeline_observe(&timeline, 1000 + 34 * 480);
    CHECK(t.drop);
    t = au_timeline_observe(&timeline, 1000 + 33 * 480);
    CHECK(t.drop && timeline.previous == 1000 + 34 * 480);
    t = au_timeline_observe(&timeline, 1000 + 39 * 480);
    CHECK(t.lost == 4 && !t.discontinuity);
    t = au_timeline_observe(&timeline, 1000 + 45 * 480);
    CHECK(t.discontinuity && t.lost == 0);
    timeline = (au_timeline_t){0};
    for (unsigned i = 0; i <= 32; i++)
        au_timeline_observe(&timeline, 0xfffff000u + i * 480);
    t = au_timeline_observe(&timeline, 0xfffff000u + 34 * 480);
    CHECK(t.lost == 1 && !t.drop); // source clock wrap
    timeline = (au_timeline_t){0};
    for (unsigned i = 0; i < 100; i++) {
        t = au_timeline_observe(&timeline, i / 4);
        CHECK(!t.drop && !t.lost && !t.discontinuity); // repeated/unknown timestamps
    }
    CHECK(timeline.confidence < 32);
    timeline = (au_timeline_t){0};
    au_timeline_observe(&timeline, 0);
    t = au_timeline_observe(&timeline, 10000);
    CHECK(!t.lost && !t.discontinuity);

    CHECK(eld_packet_fits(0, 982, 986));
    CHECK(!eld_packet_fits(0, 983, 986));
    CHECK(eld_packet_fits(600, 382, 986));
    CHECK(!eld_packet_fits(600, 383, 986));
    CHECK(!eld_packet_fits(0, 1, 3));
    CHECK(!eld_packet_fits(0xffffffffu, 1, 986));
    CHECK(audio_sample_timestamp(100, 200, false) == 100);
    CHECK(audio_sample_timestamp(100, 200, true) == 200);
    CHECK(audio_sample_timestamp(400, 200, true) == 400);
    CHECK(audio_sample_timestamp(0x10, 0xfffffff0u, true) == 0x10);
    CHECK(audio_sample_timestamp(0xfffffff0u, 0x10, true) == 0x10);
    uint8_t header[4];
    eld_size_header(header, 0x0fff, 982);
    CHECK(header[0] == 0xbf && header[1] == 0xff && header[2] == 0x13 && header[3] == 0xd6);
    eld_size_header(header, 0x1000, 1);
    CHECK(header[0] == 0xb0 && header[1] == 0 && header[2] == 0x10 && header[3] == 1);
#ifdef WITH_QUEUE_TESTS
    run_queue_tests();
    run_case_identity_tests();
    run_negotiation_tests();
#endif
    volatile uint32_t *result = (volatile uint32_t *)0x20000000;
    result[0] = checks;
    result[1] = failures;
    result[2] = first_failure;
}

void run_queue_tests(void);
#include "recovery_policy_test.c"
#include "../src/btstack/btstack_avdtp_source.h"

typedef unsigned btstack_timer_source_t;
typedef struct { uint8_t data[8]; unsigned head, count; } queue_t;
static bool irq_enabled = true, pending_irq;
static unsigned inject_at, queue_ops;
static uint32_t fake_now;
static int16_t test_pcm[2048];

static void producer_irq(void) {
    bool previous = irq_enabled;
    irq_enabled = false;
    audio_slot_push_samples(test_pcm, 480);
    irq_enabled = previous;
}
static void queue_preempt(void) {
    queue_ops++;
    if (queue_ops != inject_at) return;
    if (irq_enabled) producer_irq();
    else pending_irq = true;
}
static uint32_t save_and_disable_interrupts(void) {
    uint32_t previous = irq_enabled;
    irq_enabled = false;
    return previous;
}
static void restore_interrupts(uint32_t previous) {
    irq_enabled = previous;
    if (irq_enabled && pending_irq) { pending_irq = false; producer_irq(); }
}
static void queue_init(queue_t *q, unsigned size, unsigned count) {
    (void)size; (void)count; *q = (queue_t){0};
}
static bool queue_try_remove(queue_t *q, uint8_t *idx) {
    queue_preempt();
    if (!q->count) return false;
    *idx = q->data[q->head]; q->head = (q->head + 1) % 8; q->count--;
    return true;
}
static bool queue_try_add(queue_t *q, const uint8_t *idx) {
    queue_preempt();
    if (q->count == 8) return false;
    q->data[(q->head + q->count) % 8] = *idx; q->count++;
    return true;
}
static uint32_t get_absolute_time(void) { return fake_now; }
static uint32_t to_ms_since_boot(uint32_t t) { return t; }
static int printf(const char *format, ...) { (void)format; return 0; }
static uint8_t cur_codec = 4;
// Production typedefs, globals and complete function bodies follow.

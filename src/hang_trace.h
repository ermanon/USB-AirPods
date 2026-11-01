#ifndef USBPODS_HANG_TRACE_H
#define USBPODS_HANG_TRACE_H
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#define HANG_TRACE_MAGIC 0x55425032u
enum { TRACE_MAIN=1, TRACE_USB=2, TRACE_VOLUME=3, TRACE_MIC_START=4,
       TRACE_MIC_DECODE=5, TRACE_MIC_REPORT=6, TRACE_DEC_INIT=7, TRACE_ENCODE=8,
       TRACE_DECODE_LOCK=9, TRACE_IDLE=10, TRACE_BUTTON=11, TRACE_DECODE_RELEASE=12 };
static inline uint32_t hang_trace_enter(uint32_t stage) {
    uint32_t flags = save_and_disable_interrupts();
    uint32_t previous = watchdog_hw->scratch[1];
    watchdog_hw->scratch[1] = stage;
    restore_interrupts(flags);
    return previous;
}
static inline void hang_trace_leave(uint32_t previous) {
    watchdog_hw->scratch[1] = previous;
}
#endif

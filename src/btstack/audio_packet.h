#ifndef USBPODS_AUDIO_PACKET_H
#define USBPODS_AUDIO_PACKET_H
#include <stdbool.h>
#include <stdint.h>
static inline bool eld_packet_fits(unsigned used, unsigned au, unsigned limit) {
    return limit >= 4 && au <= limit - 4 && used <= limit - 4 - au;
}
static inline uint32_t audio_sample_timestamp(uint32_t source, uint32_t next, bool valid) {
    return valid && (int32_t)(source - next) < 0 ? next : source;
}
static inline void eld_size_header(uint8_t *dst, uint16_t sequence, uint16_t bytes) {
    sequence &= 0x0fff;
    dst[0] = 0xb0 | (sequence >> 8);
    dst[1] = sequence & 0xff;
    dst[2] = 0x10 | ((bytes >> 8) & 0x0f);
    dst[3] = bytes & 0xff;
}
#endif

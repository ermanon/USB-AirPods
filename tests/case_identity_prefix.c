#include "rijndael.h"
typedef uint8_t bd_addr_t[6];
static bool key_valid;
static uint8_t case_irk[16];
static int memcmp(const void *a, const void *b, unsigned n) {
    const uint8_t *aa=a, *bb=b;
    while (n--) { if (*aa != *bb) return (int)*aa - *bb; aa++; bb++; }
    return 0;
}
static void btstack_aes128_calc(const uint8_t *key, const uint8_t *input, uint8_t *output) {
    // Same implementation as BTstack's production software AES wrapper.
    uint32_t rk[44];
    int rounds = rijndaelSetupEncrypt(rk, key, 128);
    rijndaelEncrypt(rk, rounds, input, output);
}

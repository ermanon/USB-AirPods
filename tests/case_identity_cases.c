void run_case_identity_tests(void) {
    // Bluetooth ah sample: key EC0234A357C8AD05341010A60A397D9B,
    // prand 708194 -> hash 0DFBAA. AACP supplies the key in LE order.
    uint8_t sample_key[] = {0x9b,0x7d,0x39,0x0a,0xa6,0x10,0x10,0x34,
                           0x05,0xad,0xc8,0x57,0xa3,0x34,0x02,0xec};
    bd_addr_t address = {0x70,0x81,0x94,0x0d,0xfb,0xaa};
    memcpy(case_irk, sample_key, 16);
    key_valid = false;
    CHECK(!case_address_matches(address));
    key_valid = true;
    CHECK(case_address_matches(address));
    for (unsigned i=0; i<6; i++) for (unsigned bit=0; bit<8; bit++) {
        address[i] ^= 1u << bit;
        CHECK(!case_address_matches(address));
        address[i] ^= 1u << bit;
    }
    for (unsigned i=0; i<16; i++) for (unsigned bit=0; bit<8; bit++) {
        case_irk[i] ^= 1u << bit;
        CHECK(!case_address_matches(address));
        case_irk[i] ^= 1u << bit;
    }
    CHECK(case_address_matches(address));
}

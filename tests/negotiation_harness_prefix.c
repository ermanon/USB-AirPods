static bool stream_configuration_received, finish_scan_avdtp_codec;
static unsigned codec_calls, negotiation_disconnects, negotiation_failures;
static int preferred_result, fallback_result;
static bool configure_during_command;
static int set_next_codec(uint8_t codec) {
    codec_calls++;
    if (configure_during_command) {
        stream_configuration_received = finish_scan_avdtp_codec = true;
    }
    return codec == 0 ? preferred_result : fallback_result;
}
static void audio_recovery_disconnect(void) { negotiation_disconnects++; }
static void debug_cdc_event(const char *name, unsigned a, unsigned b) {
    (void)name; (void)a; (void)b; negotiation_failures++;
}

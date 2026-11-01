static void reset_negotiation_test(void) {
    media_tracker.avdtp_cid = 1;
    is_streaming = stream_configuration_received = finish_scan_avdtp_codec = false;
    configure_during_command = false;
    codec_calls = negotiation_disconnects = negotiation_failures = 0;
    preferred_result = fallback_result = 0;
}
void run_negotiation_tests(void) {
    reset_negotiation_test();
    stream_configuration_received = finish_scan_avdtp_codec = true;
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 0 && negotiation_disconnects == 0);
    CHECK(finish_scan_avdtp_codec); // remote config/open before discovery done
    reset_negotiation_test();
    media_tracker.avdtp_cid = 0;
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 0);
    reset_negotiation_test();
    is_streaming = true;
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 0);
    reset_negotiation_test();
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 1 && finish_scan_avdtp_codec);
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 1); // duplicate discovery completion cannot reconfigure
    reset_negotiation_test();
    preferred_result = 12;
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 2 && finish_scan_avdtp_codec && !negotiation_disconnects);
    reset_negotiation_test();
    preferred_result = fallback_result = 12;
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 2 && negotiation_disconnects == 1);
    CHECK(negotiation_failures == 1 && !finish_scan_avdtp_codec);
    reset_negotiation_test();
    configure_during_command = true; preferred_result = 12;
    set_next_capablity_and_start_stream();
    CHECK(codec_calls == 1 && !negotiation_disconnects && finish_scan_avdtp_codec);
}

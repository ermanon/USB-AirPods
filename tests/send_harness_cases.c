static void run_send_tests(void) {
    avdtp_connection_t connection = {.avdtp_cid = 7};
    avdtp_stream_endpoint_t endpoint = {.connection = &connection, .l2cap_media_cid = 20};
    uint8_t payload[4] = {0};
    media_tracker.avdtp_cid = 7;
    is_streaming = true;
    CHECK(audio_payload_send(payload, 4) == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    CHECK(stack_send_calls == 0 && audio_tx_packets == 0);
    sc.local_stream_endpoint = &endpoint;
    endpoint.l2cap_media_cid = 0;
    CHECK(audio_payload_send(payload, 4) == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    CHECK(stack_send_calls == 0 && audio_tx_packets == 0);
    endpoint.l2cap_media_cid = 20;
    connection.avdtp_cid = 8;
    CHECK(audio_payload_send(payload, 4) == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    CHECK(stack_send_calls == 0);
    connection.avdtp_cid = 7;
    is_streaming = false;
    CHECK(audio_payload_send(payload, 4) == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    is_streaming = true;
    CHECK(audio_payload_send(payload, 0) == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    CHECK(stack_send_calls == 0);
    stack_send_status = 7;
    CHECK(audio_payload_send(payload, 4) == 7);
    CHECK(audio_send_error_pending && audio_tx_packets == 0 && audio_last_tx_status == 7);
    CHECK(audio_tx_errors == 6 && audio_packet_drops == 6);
    stack_send_status = 0;
    for (unsigned codec = 1; codec <= 4; codec++) {
        cur_codec = codec;
        fake_now = 100;
        audio_recovery_enter(&recovery, AUDIO_RECOVERY_VERIFY, 7, 2, fake_now);
        for (unsigned i = 0; i < 10; i++) {
            fake_now = 500 + i * 10;
            CHECK(audio_payload_send(payload, 4) == 0);
        }
        CHECK(audio_recovery_poll(&recovery, 600) == AUDIO_RECOVERY_HEALTHY);
    }
    CHECK(audio_tx_packets == 40);
}

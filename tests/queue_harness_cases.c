static void check_ownership(void) {
    unsigned owners[8] = {0};
    for (unsigned i = 0; i < free_queue.count; i++)
        owners[free_queue.data[(free_queue.head + i) % 8]]++;
    for (unsigned i = 0; i < ready_queue.count; i++)
        owners[ready_queue.data[(ready_queue.head + i) % 8]]++;
    if (filling_slot_valid) owners[filling_slot_idx]++;
    for (unsigned i = 0; i < active_slot_count; i++) CHECK(owners[i] == 1);
    CHECK(irq_enabled);
}
void run_queue_tests(void) {
    inject_at = 0;
    audio_slot_queue_init();
    for (unsigned point = 1; point <= 25; point++) {
        inject_at = 0;
        audio_slot_queue_configure_with_count(480, 6);
        producer_irq(); producer_irq();
        queue_ops = 0;
        inject_at = point;
        audio_slot_queue_configure_with_count(480, 6);
        inject_at = 0;
        check_ownership();
        CHECK(slot_frame_int16 == 960);
    }
    audio_slot_queue_configure_with_count(480, 6);
    uint32_t start_clock = pcm_source_clock;
    for (unsigned i = 0; i < 7; i++) producer_irq();
    CHECK(ready_queue.count == 6);
    CHECK(audio_pcm_dropped >= 480);
    CHECK(pcm_source_clock == start_clock + 7 * 480);
    check_ownership();
    audio_slot_queue_configure_with_count(480, 6);
    producer_irq();
    fake_now += 81;
    uint8_t slot;
    CHECK(audio_slot_pop(&slot));
    CHECK(audio_pcm_stale == 480);
    CHECK(audio_pcm_silence == 480);
    for (unsigned i = 0; i < 960; i++) CHECK(slot_pool[slot].data[i] == 0);
    queue_try_add(&free_queue, &slot);
    check_ownership();
    unsigned old_count = free_queue.count;
    audio_slot_queue_configure_with_count(0, 6);
    CHECK(free_queue.count == old_count && irq_enabled);

    a2dp_media_sending_context_t packet = {
        .codec_storage_count = 680, .codec_ready_to_send = 1,
        .codec_num_frames = 2, .send_wait_ms = 100,
        .packet_created_ms = 80, .rtp_timestamp = 123456
    };
    audio_packet_reset(&packet);
    CHECK(packet.codec_storage_count == 0 && packet.codec_num_frames == 0);
    CHECK(packet.codec_ready_to_send == 0 && packet.send_wait_ms == 0);
    CHECK(packet.packet_created_ms == 0 && packet.rtp_timestamp == 123456);
    run_send_tests();
}

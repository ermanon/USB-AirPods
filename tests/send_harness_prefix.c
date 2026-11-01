typedef struct { uint16_t avdtp_cid; } avdtp_connection_t;
typedef struct { avdtp_connection_t *connection; uint16_t l2cap_media_cid; } avdtp_stream_endpoint_t;
static struct { avdtp_stream_endpoint_t *local_stream_endpoint; } sc;
static a2dp_media_sending_context_t media_tracker;
static audio_recovery_t recovery;
static bool is_streaming, audio_send_error_pending;
static uint8_t audio_last_tx_status;
static uint32_t audio_tx_packets, audio_tx_errors, audio_packet_drops;
static unsigned stack_send_calls;
static uint8_t stack_send_status;
#define ERROR_CODE_SUCCESS 0
#define ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER 2
static uint32_t btstack_run_loop_get_time_ms(void) { return fake_now; }
static uint8_t a2dp_source_stream_send_media_payload_rtp(uint16_t cid,
        uint8_t seid, uint8_t marker, uint32_t ts, uint8_t *payload, uint16_t size) {
    (void)cid; (void)seid; (void)marker; (void)ts; (void)payload; (void)size;
    stack_send_calls++;
    // Model the SDK's false-success return when media CID is missing.
    if (!sc.local_stream_endpoint || !sc.local_stream_endpoint->l2cap_media_cid) return 0;
    return stack_send_status;
}

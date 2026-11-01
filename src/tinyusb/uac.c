/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jerzy Kasenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

 #include <stdio.h>
 #include <string.h>
 
 #include "bsp/board_api.h"
 #include "tusb.h"
 #include "usb_descriptors.h"
 #include "debug_cdc.h"

 #include "../btstack/btstack_avdtp_source.h"
 #include "../btstack/aacp_mic_dec.h"
 #include "pico/flash.h"
 #include "hardware/sync.h"


 
 //--------------------------------------------------------------------+
 // MACRO CONSTANT TYPEDEF PROTOTYPES
 //--------------------------------------------------------------------+
 
 // List of supported sample rates
 const uint32_t sample_rates[] = {48000};

 uint32_t current_sample_rate  = 48000;

 bool need_change_bt_volume = false;
 static volatile bool volume_notify_pending = false;
 static void audio_notification_task(void);
 static volatile uint32_t usb_output_samples;
 static volatile uint32_t usb_output_peak;
 static volatile uint32_t usb_output_channel_peak[2];
 
 #define N_SAMPLE_RATES  TU_ARRAY_SIZE(sample_rates)
 
 /* Blink pattern
  * - 25 ms   : streaming data
  * - 250 ms  : device not mounted
  * - 1000 ms : device mounted
  * - 2500 ms : device is suspended
  */
 enum
 {
   BLINK_STREAMING = 25,
   BLINK_NOT_MOUNTED = 250,
   BLINK_MOUNTED = 1000,
   BLINK_SUSPENDED = 2500,
 };
 
 enum
 {
   VOLUME_CTRL_0_DB = 0,
   VOLUME_CTRL_10_DB = 2560,
   VOLUME_CTRL_20_DB = 5120,
   VOLUME_CTRL_30_DB = 7680,
   VOLUME_CTRL_40_DB = 10240,
   VOLUME_CTRL_50_DB = 12800,
   VOLUME_CTRL_60_DB = 15360,
   VOLUME_CTRL_70_DB = 17920,
   VOLUME_CTRL_80_DB = 20480,
   VOLUME_CTRL_90_DB = 23040,
   VOLUME_CTRL_100_DB = 25600,
   VOLUME_CTRL_SILENCE = 0x8000,
 };

 #define BT_VOL_MAX   127
 #define USB_ATT_MAX  25600
 
 static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
 
 // Audio controls
 // Current states
 int8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];       // +1 for master channel 0
 int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];    // +1 for master channel 0
 

 // Buffer for microphone data
 //int32_t mic_buf[CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ / 4];

 // Buffer for speaker data
 int32_t spk_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 4];
 // Speaker data size received in the last frame
 int spk_data_size;
 // Resolution per format
 const uint8_t resolutions_per_format[CFG_TUD_AUDIO_FUNC_1_N_FORMATS] = {CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX,
                                                                         CFG_TUD_AUDIO_FUNC_1_FORMAT_2_RESOLUTION_RX};
 // Current resolution, update on format change
 uint8_t current_resolution;
 
 //void led_blinking_task(void);
 void audio_task(void);
 void audio_control_task(void);
 
 /*------------- MAIN -------------*/
 void tinyusb_main(void)
 {


  flash_safe_execute_core_init();

  //board_init();
 
   // init device stack on configured roothub port
   tusb_rhport_init_t dev_init = {
     .role = TUSB_ROLE_DEVICE,
     .speed = TUSB_SPEED_AUTO
   };
   tusb_init(BOARD_TUD_RHPORT, &dev_init);


 }


 void tinyusb_task(void){
    tud_task(); // TinyUSB device task
    debug_cdc_task(); // drain buffered stdout to the CDC console (same context as tud_task)
    audio_task();
    audio_notification_task();
 }
 

void tinyusb_control_task(void){
  audio_control_task();
}

 //--------------------------------------------------------------------+
 // Device callbacks
 //--------------------------------------------------------------------+
 
 // Invoked when device is mounted
 void tud_mount_cb(void)
 {
   //blink_interval_ms = BLINK_MOUNTED;
 }
 
 // Invoked when device is unmounted
 void tud_umount_cb(void)
 {
   //blink_interval_ms = BLINK_NOT_MOUNTED;
 }
 
 // Invoked when usb bus is suspended
 // remote_wakeup_en : if host allow us  to perform remote wakeup
 // Within 7ms, device must draw an average of current less than 2.5 mA from bus
 void tud_suspend_cb(bool remote_wakeup_en)
 {
   (void)remote_wakeup_en;
   printf("tud_suspend_cb\n");
 }
 
 // Invoked when usb bus is resumed
 void tud_resume_cb(void)
 {
  printf("tud_resume_cb\n");
 }
 
 // Helper for the mic clock (fixed 64 kHz — see USB_MIC_SAMPLE_RATE)
 static bool tud_audio_mic_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
 {
   TU_ASSERT(request->bEntityID == UAC2_ENTITY_MIC_CLOCK);

   if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
   {
     if (request->bRequest == AUDIO_CS_REQ_CUR)
     {
       audio_control_cur_4_t curf = { (int32_t) tu_htole32(USB_MIC_SAMPLE_RATE) };
       return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
     }
     else if (request->bRequest == AUDIO_CS_REQ_RANGE)
     {
       audio_control_range_4_n_t(1) rangef =
       {
         .wNumSubRanges = tu_htole16(1),
         .subrange[0] = { .bMin = (int32_t) USB_MIC_SAMPLE_RATE,
                          .bMax = (int32_t) USB_MIC_SAMPLE_RATE, .bRes = 0 }
       };
       return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
     }
   }
   else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
            request->bRequest == AUDIO_CS_REQ_CUR)
   {
     audio_control_cur_1_t cur_valid = { .bCur = 1 };
     return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
   }
   return false;
 }

 // Helper for clock get requests
 static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
 {
   TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
 
   if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
   {
     if (request->bRequest == AUDIO_CS_REQ_CUR)
     {
       TU_LOG1("Clock get current freq %" PRIu32 "\r\n", current_sample_rate);
 
       audio_control_cur_4_t curf = { (int32_t) tu_htole32(current_sample_rate) };
       return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
     }
     else if (request->bRequest == AUDIO_CS_REQ_RANGE)
     {
       audio_control_range_4_n_t(N_SAMPLE_RATES) rangef =
       {
         .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)
       };
       TU_LOG1("Clock get %d freq ranges\r\n", N_SAMPLE_RATES);
       for(uint8_t i = 0; i < N_SAMPLE_RATES; i++)
       {
         rangef.subrange[i].bMin = (int32_t) sample_rates[i];
         rangef.subrange[i].bMax = (int32_t) sample_rates[i];
         rangef.subrange[i].bRes = 0;
         TU_LOG1("Range %d (%d, %d, %d)\r\n", i, (int)rangef.subrange[i].bMin, (int)rangef.subrange[i].bMax, (int)rangef.subrange[i].bRes);
       }
 
       return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
     }
   }
   else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
            request->bRequest == AUDIO_CS_REQ_CUR)
   {
     audio_control_cur_1_t cur_valid = { .bCur = 1 };
     TU_LOG1("Clock get is valid %u\r\n", cur_valid.bCur);
     return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
   }
   TU_LOG1("Clock get request not supported, entity = %u, selector = %u, request = %u\r\n",
           request->bEntityID, request->bControlSelector, request->bRequest);
   return false;
 }
 
 // Helper for clock set requests
 static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
 {
   (void)rhport;
 
   TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
   TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);
 
   if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
   {
     TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
 
     current_sample_rate = (uint32_t) ((audio_control_cur_4_t const *)buf)->bCur;
 
     TU_LOG1("Clock set current freq: %" PRIu32 "\r\n", current_sample_rate);
 
     return true;
   }
   else
   {
     TU_LOG1("Clock set request not supported, entity = %u, selector = %u, request = %u\r\n",
             request->bEntityID, request->bControlSelector, request->bRequest);
     return false;
   }
 }
 
 // Helper for feature unit get requests
 static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request)
 {
   TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);
   TU_VERIFY(request->bChannelNumber == 0); // only master controls advertised
 
   if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR)
   {
     audio_control_cur_1_t mute1 = { .bCur = mute[request->bChannelNumber] };
     TU_LOG1("Get channel %u mute %d\r\n", request->bChannelNumber, mute1.bCur);
     return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
   }
   else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
   {
     if (request->bRequest == AUDIO_CS_REQ_RANGE)
     {
       audio_control_range_2_n_t(1) range_vol = {
         .wNumSubRanges = tu_htole16(1),
         .subrange[0] = { .bMin = tu_htole16(-VOLUME_CTRL_50_DB), tu_htole16(VOLUME_CTRL_0_DB), tu_htole16(256) }
       };
       TU_LOG1("Get channel %u volume range (%d, %d, %u) dB\r\n", request->bChannelNumber,
               range_vol.subrange[0].bMin / 256, range_vol.subrange[0].bMax / 256, range_vol.subrange[0].bRes / 256);
       return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
     }
     else if (request->bRequest == AUDIO_CS_REQ_CUR)
     {
       audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(volume[request->bChannelNumber]) };
       TU_LOG1("Get channel %u volume %d dB\r\n", request->bChannelNumber, cur_vol.bCur / 256);
       return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
     }
   }
   TU_LOG1("Feature unit get request not supported, entity = %u, selector = %u, request = %u\r\n",
           request->bEntityID, request->bControlSelector, request->bRequest);
 
   return false;
 }
 
 // Helper for feature unit set requests
 static bool tud_audio_feature_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
 {
   (void)rhport;
 
   TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);
   TU_VERIFY(request->bChannelNumber == 0);
   TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);
 
   if (request->bControlSelector == AUDIO_FU_CTRL_MUTE)
   {
     TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));
 
     TU_VERIFY(buf[0] <= 1);
     mute[request->bChannelNumber] = buf[0];

     if(request->bChannelNumber == 0){
      need_change_bt_volume = true;
     }
 
     TU_LOG1("Set channel %d Mute: %d\r\n", request->bChannelNumber, mute[request->bChannelNumber]);
 
     return true;
   }
   else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
   {
     TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
 
     int16_t requested_volume = (int16_t)tu_le16toh(((audio_control_cur_2_t const *)buf)->bCur);
     TU_VERIFY(requested_volume >= -VOLUME_CTRL_50_DB && requested_volume <= VOLUME_CTRL_0_DB);
     volume[request->bChannelNumber] = requested_volume;
 
     TU_LOG1("Set channel %d volume: %d dB\r\n", request->bChannelNumber, volume[request->bChannelNumber]/256);

     if(request->bChannelNumber == 0){
      need_change_bt_volume = true;
     }
 
     return true;
   }
   else
   {
     TU_LOG1("Feature unit set request not supported, entity = %u, selector = %u, request = %u\r\n",
             request->bEntityID, request->bControlSelector, request->bRequest);
     return false;
   }
 }
 
 //--------------------------------------------------------------------+
 // Application Callback API Implementations
 //--------------------------------------------------------------------+
 
 // Invoked when audio class specific get request received for an entity
 bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
 {
   audio_control_request_t const *request = (audio_control_request_t const *)p_request;
 
   if (request->bEntityID == UAC2_ENTITY_CLOCK)
     return tud_audio_clock_get_request(rhport, request);
   if (request->bEntityID == UAC2_ENTITY_MIC_CLOCK)
     return tud_audio_mic_clock_get_request(rhport, request);
   if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
     return tud_audio_feature_unit_get_request(rhport, request);
   else
   {
     TU_LOG1("Get request not handled, entity = %d, selector = %d, request = %d\r\n",
             request->bEntityID, request->bControlSelector, request->bRequest);
   }
   return false;
 }
 
 // Invoked when audio class specific set request received for an entity
 bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
 {
   audio_control_request_t const *request = (audio_control_request_t const *)p_request;
 
   if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
     return tud_audio_feature_unit_set_request(rhport, request, buf);
   if (request->bEntityID == UAC2_ENTITY_CLOCK)
     return tud_audio_clock_set_request(rhport, request, buf);
   if (request->bEntityID == UAC2_ENTITY_MIC_CLOCK)
   {
     // Fixed 64 kHz clock — accept a redundant SET of the only valid rate.
     (void)buf;
     return (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
             request->bRequest == AUDIO_CS_REQ_CUR);
   }
   TU_LOG1("Set request not handled, entity = %d, selector = %d, request = %d\r\n",
           request->bEntityID, request->bControlSelector, request->bRequest);
 
   return false;
 }
 
 // --- Mic lifecycle requests (the USB alt setting is the only trigger) ---
 // These callbacks run in USB (timer IRQ) context; BTstack must not be called
 // from here (same rule as the buttons in main.c). Latch a request; the main
 // loop executes aacp_mic_start/stop with the async-context lock held.
 static volatile bool mic_request_pending = false;
 static volatile bool mic_requested_enabled = false;

 // USB callbacks run on this core's timer IRQ. Consume the latest state
 // atomically so rapid alt-setting changes cannot reorder START and STOP.
 bool usb_mic_take_request(bool *enabled) {
   uint32_t flags = save_and_disable_interrupts();
   bool pending = mic_request_pending;
   *enabled = mic_requested_enabled;
   mic_request_pending = false;
   restore_interrupts(flags);
   return pending;
 }

 static void usb_mic_request(bool enabled) {
   mic_requested_enabled = enabled;
   mic_request_pending = true;
 }

 bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request)
 {
   (void)rhport;

   uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
   uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

   if (ITF_NUM_AUDIO_STREAMING_SPK == itf && alt == 0)
       blink_interval_ms = BLINK_MOUNTED;

   if (ITF_NUM_AUDIO_STREAMING_MIC == itf && alt == 0)
   {
       printf("[USB] mic interface closed (alt 0)\n");
       usb_mic_request(false);
   }

   return true;
 }

 bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
 {
   (void)rhport;
   uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
   uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

   TU_LOG2("Set interface %d alt %d\r\n", itf, alt);
   if (ITF_NUM_AUDIO_STREAMING_SPK == itf && alt != 0)
       blink_interval_ms = BLINK_STREAMING;

   // Host opened/closed the recording stream (e.g. an app starts capturing).
   if (ITF_NUM_AUDIO_STREAMING_MIC == itf)
   {
       printf("[USB] mic interface alt %d\n", alt);
       usb_mic_request(alt != 0);
       return true;
   }

   // Clear buffer when streaming format is changed
   spk_data_size = 0;
   if(alt != 0)
   {
     current_resolution = resolutions_per_format[alt-1];
   }

   return true;
 }

 uint16_t usb_stop_delay = 0;
 bool is_usb_audio_running = false;

 bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
 {
   (void)rhport;
   (void)func_id;
   (void)ep_out;
   (void)cur_alt_setting;
 
   spk_data_size = tud_audio_read(spk_buf, n_bytes_received);

   if (spk_data_size)
   {
    usb_stop_delay = 0;
    set_usb_streaming(true);
    if (current_resolution == 16)
    {
      int16_t *src = (int16_t *)spk_buf;
      uint16_t sample_count = spk_data_size / 4; // should be 44-45
      usb_output_samples += sample_count;
      for (unsigned i = 0; i < sample_count * 2; i++) {
          int32_t sample = src[i];
          uint32_t magnitude = sample < 0 ? -sample : sample;
          if (magnitude > usb_output_peak) usb_output_peak = magnitude;
          if (magnitude > usb_output_channel_peak[i & 1])
              usb_output_channel_peak[i & 1] = magnitude;
      }

      audio_slot_push_samples(src, sample_count);

      is_usb_audio_running = true;
      spk_data_size = 0;
    }
   } 

   return true;
 }
 
 // Feed the mic EP IN: called by the audio driver before each IN transfer is
 // loaded. Pull 1ms of samples (64 @ 64 kHz) from the decoder's PCM ring and
 // zero-fill any shortfall so the host always gets a full frame.
 bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
 {
   (void)rhport;
   (void)itf;
   (void)ep_in;
   (void)cur_alt_setting;

   int16_t buf[USB_MIC_SAMPLE_RATE / 1000];   // 64 samples / ms, mono
   uint32_t got = aacp_mic_pcm_read(buf, TU_ARRAY_SIZE(buf));
   for (uint32_t i = got; i < TU_ARRAY_SIZE(buf); i++) buf[i] = 0;

   tud_audio_write(buf, sizeof(buf));
   return true;
 }
 
 //--------------------------------------------------------------------+
 // AUDIO Task
 //--------------------------------------------------------------------+


 void audio_task(void)
 {
  if (is_usb_audio_running){
    usb_stop_delay = 0;
  }else{
    usb_stop_delay++;
    if (usb_stop_delay > 100){
      set_usb_streaming(false);
    }
  }
  is_usb_audio_running = false;
 }
 

void audio_control_task(void)
 {
   static uint32_t last_report;
   uint32_t now = to_ms_since_boot(get_absolute_time());
   if (now - last_report >= 1000) {
       last_report = now;
       uint32_t saved = save_and_disable_interrupts();
       uint32_t samples = usb_output_samples, peak = usb_output_peak;
       uint32_t left = usb_output_channel_peak[0], right = usb_output_channel_peak[1];
       usb_output_samples = usb_output_peak = 0;
       usb_output_channel_peak[0] = usb_output_channel_peak[1] = 0;
       int host_mute = mute[0], host_volume = volume[0];
       restore_interrupts(saved);
       printf("[USBOUT] samples=%lu peak=%lu L=%lu R=%lu mute=%d vol256db=%d remote_pause=%u\n",
              (unsigned long)samples, (unsigned long)peak,
              (unsigned long)left, (unsigned long)right, host_mute, host_volume, get_bt_mute());
       bt_audio_report();
   }
   if (*get_is_bt_sink_volume_changed_ptr())
   {

    uint8_t bt_level = get_bt_volume();


    // 2) invert & scale into 0…25600
    //    (BT_VOL_MAX - bt_level) maps 127→0, 0→127
    //    multiply then divide with rounding
    uint16_t usb_level = (bt_level > BT_VOL_MAX)
                       ? USB_ATT_MAX
                       : (uint16_t)(( (uint32_t)(BT_VOL_MAX - bt_level)
                                     * USB_ATT_MAX
                                     + (BT_VOL_MAX/2) )
                                   / BT_VOL_MAX);

    // 3) store into USB-Audio’s volume (attenuation) field
    // AVRCP echoes zero when we mute. Keep the host's saved level so
    // unmute restores it, and do not overwrite a pending USB request.
    uint32_t volume_flags = save_and_disable_interrupts();
    if (!mute[0] && !need_change_bt_volume)
        volume[0] = -1 * usb_level / 2;
    restore_interrupts(volume_flags);

     volume_notify_pending = true;
     *get_is_bt_sink_volume_changed_ptr() = false;
   }

   // Snapshot and consume the USB request before calling BTstack. A USB
   // callback arriving during the call leaves a new request for next tick.
   uint32_t flags = save_and_disable_interrupts();
   bool change_volume = need_change_bt_volume;
   int16_t requested_level = volume[0];
   bool requested_mute = mute[0] != 0;
   need_change_bt_volume = false;
   restore_interrupts(flags);
   if (change_volume){

    //printf("vol is %d\n", volume[0]);
    
    // SET_CUR is a state assignment, including repeated MUTE=1 requests.
    // Volume changes while muted update the saved level, not the output.
    set_bt_volume_usb((requested_mute || get_bt_mute()) ? -12800 : requested_level);
    // The host already knows its SET_CUR value. Only unsolicited changes
    // from the earbuds need an interrupt notification back to Windows.
    //*get_is_bt_sink_volume_changed_ptr() = false;
   }

  }
 static void audio_notification_task(void) {
   // TinyUSB's Pico OS layer uses blocking mutexes. A thread must not be
   // preempted by tud_task while holding one. Keep ALL endpoint writes in
   // the USB timer context, alongside tud_task and the audio callbacks.
   if (!volume_notify_pending || !tud_mounted()) return;
   const audio_interrupt_data_t data = {
     .bInfo = 0,
     .bAttribute = AUDIO_CS_REQ_CUR,
     .wValue_cn_or_mcn = 0,
     .wValue_cs = AUDIO_FU_CTRL_VOLUME,
     .wIndex_ep_or_int = ITF_NUM_AUDIO_CONTROL,
     .wIndex_entity_id = UAC2_ENTITY_SPK_FEATURE_UNIT,
   };
   if (tud_audio_int_write(&data)) volume_notify_pending = false;
 }
 

#include "ES9018AudioSink.h"

#include "driver/i2s.h"

ES9018AudioSink::ES9018AudioSink() {
  i2s_config_t i2s_config = {

      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),  // Only TX
      .sample_rate = 44100,
      .bits_per_sample = (i2s_bits_per_sample_t)16,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  //2-channels
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB,
      .intr_alloc_flags = 0,  //Default interrupt priority
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = true,
      .tx_desc_auto_clear = true,  //Auto clear tx descriptor on underflow
      .fixed_mclk = 384 * 44100};

  i2s_pin_config_t pin_config = {
      .bck_io_num = 27,
      .ws_io_num = 32,
      .data_out_num = 25,
      .data_in_num = -1  //Not used
  };
  i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)0, &pin_config);

  startI2sFeed();
}

ES9018AudioSink::~ES9018AudioSink() {}

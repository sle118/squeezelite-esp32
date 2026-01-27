#include "InternalAudioSink.h"
#include "driver/i2s.h"

InternalAudioSink::InternalAudioSink() {
  softwareVolumeControl = true;
  usign = true;
#ifdef I2S_MODE_DAC_BUILT_IN

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX |
                           I2S_MODE_DAC_BUILT_IN),  // Only TX
      .sample_rate = (i2s_bits_per_sample_t)44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,  //ESP_INTR_FLAG_LEVEL1
      .dma_buf_count = 6,
      .dma_buf_len = 512,
      .use_apll = true,
      .tx_desc_auto_clear = true,  //Auto clear tx descriptor on underflow
      .fixed_mclk = -1};

  //install and start i2s driver
  i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  //init DAC
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
#endif

  startI2sFeed();
}

InternalAudioSink::~InternalAudioSink() {}

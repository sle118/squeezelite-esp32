#include "ES8311AudioSink.h"
extern "C" {
#include "es8311.h"
}
ES8311AudioSink::ES8311AudioSink() {
  this->softwareVolumeControl = false;
  esp_err_t ret_val = ESP_OK;
  Es8311Config cfg = {
      .esMode = ES_MODE_SLAVE,
      .i2c_port_num = I2C_NUM_0,
      .i2c_cfg =
          {
              .mode = I2C_MODE_MASTER,
              .sda_io_num = 1,
              .scl_io_num = 2,
              .sda_pullup_en = GPIO_PULLUP_ENABLE,
              .scl_pullup_en = GPIO_PULLUP_ENABLE,
          },
      .dacOutput = (DacOutput)(DAC_OUTPUT_LOUT1 | DAC_OUTPUT_LOUT2 |
                               DAC_OUTPUT_ROUT1 | DAC_OUTPUT_ROUT2),
      .adcInput = ADC_INPUT_LINPUT1_RINPUT1,
  };
  cfg.i2c_cfg.master.clk_speed = 100000;
  Es8311Init(&cfg);
  Es8311SetBitsPerSample(ES_MODULE_DAC, BIT_LENGTH_16BITS);
  Es8311ConfigFmt(ES_MODULE_DAC, ES_I2S_NORMAL);
  Es8311SetVoiceVolume(60);
  Es8311Start(ES_MODULE_DAC);
  ES8311WriteReg(ES8311_CLK_MANAGER_REG01, 0xbf);
  ES8311WriteReg(ES8311_CLK_MANAGER_REG02, 0x18);

  //     .codec_mode = AUDIO_HAL_CODEC_MODE_DECODE,
  //     .i2s_iface = {
  //         .mode = AUDIO_HAL_MODE_SLAVE,
  //         .fmt = AUDIO_HAL_I2S_NORMAL,
  //         .samples = AUDIO_HAL_44K_SAMPLES,
  //         .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
  //     },
  // };

  // ret_val |= es8311_codec_init(&cfg);
  // ret_val |= es8311_set_bits_per_sample(cfg.i2s_iface.bits);
  // ret_val |= es8311_config_fmt((es_i2s_fmt_t) cfg.i2s_iface.fmt);
  // ret_val |= es8311_codec_set_voice_volume(60);
  // ret_val |= es8311_codec_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);
  // ret_val |= es8311_codec_set_clk();

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),  // Only TX
      .sample_rate = 44100,
      .bits_per_sample = (i2s_bits_per_sample_t)16,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // 2-channels
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,  // Default interrupt priority
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = true,  // Auto clear tx descriptor on underflow
  };

  i2s_pin_config_t pin_config = {
      .mck_io_num = 42,
      .bck_io_num = 40,
      .ws_io_num = 41,
      .data_out_num = 39,
      .data_in_num = -1,
  };

  int err;

  err = i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    ESP_LOGE("OI", "i2s driver installation error: %d", err);
  }

  err = i2s_set_pin((i2s_port_t)0, &pin_config);
  if (err != ESP_OK) {
    ESP_LOGE("OI", "i2s set pin error: %d", err);
  }

  // PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
  // REG_SET_FIELD(PIN_CTRL, CLK_OUT1, 0);
  // ESP_LOGI("OI", "MCLK output on CLK_OUT1");

  startI2sFeed();
}

void ES8311AudioSink::volumeChanged(uint16_t volume) {
  Es8311SetVoiceVolume(volume);
}

void ES8311AudioSink::writeReg(uint8_t reg_add, uint8_t data) {}

void ES8311AudioSink::setSampleRate(uint32_t sampleRate) {
  std::cout << "ES8311AudioSink::setSampleRate(" << sampleRate << ")"
            << std::endl;
  // i2s set sample rate
  es8311_Codec_Startup(0, sampleRate);
}

ES8311AudioSink::~ES8311AudioSink() {}

#include "TAS5711AudioSink.h"

struct tas5711_cmd_s {
  uint8_t reg;
  uint8_t value;
};

static const struct tas5711_cmd_s tas5711_init_sequence[] = {
    {0x00, 0x6c},  // 0x6c - 256 x mclk
    {0x04, 0x03},  // 0x03 - 16 bit i2s
    {0x05, 0x00},  // system control 0x00 is audio playback
    {0x06, 0x00},  // disable mute
    {0x07, 0x50},  // volume register
    {0xff, 0xff}

};
i2c_ack_type_t ACK_CHECK_EN = (i2c_ack_type_t)0x1;

TAS5711AudioSink::TAS5711AudioSink() {
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
      .fixed_mclk = 256 * 44100};

  i2s_pin_config_t pin_config = {
      .bck_io_num = 5,
      .ws_io_num = 25,
      .data_out_num = 26,
      .data_in_num = -1  //Not used
  };
  i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)0, &pin_config);

  // configure i2c
  i2c_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = 21,
      .scl_io_num = 23,
      .sda_pullup_en = GPIO_PULLUP_DISABLE,
      .scl_pullup_en = GPIO_PULLUP_DISABLE,
  };

  i2c_config.master.clk_speed = 250000;

  i2c_param_config(i2c_port, &i2c_config);
  i2c_driver_install(i2c_port, I2C_MODE_MASTER, false, false, false);
  i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();

  uint8_t data, addr = (0x1b);

  i2c_master_start(i2c_cmd);
  i2c_master_write_byte(i2c_cmd, (addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
  i2c_master_write_byte(i2c_cmd, 00, ACK_CHECK_EN);

  i2c_master_start(i2c_cmd);
  i2c_master_write_byte(i2c_cmd, (addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
  i2c_master_read_byte(i2c_cmd, &data, ACK_CHECK_EN);

  i2c_master_stop(i2c_cmd);
  int ret = i2c_master_cmd_begin(i2c_port, i2c_cmd, 50 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(i2c_cmd);

  if (ret == ESP_OK) {
    ESP_LOGI("RR", "Detected TAS");
  } else {
    ESP_LOGI("RR", "Unable to detect dac");
  }

  writeReg(0x1b, 0x00);
  vTaskDelay(100 / portTICK_PERIOD_MS);

  for (int i = 0; tas5711_init_sequence[i].reg != 0xff; i++) {
    writeReg(tas5711_init_sequence[i].reg, tas5711_init_sequence[i].value);
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  startI2sFeed();
}

void TAS5711AudioSink::writeReg(uint8_t reg, uint8_t value) {
  i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();

  i2c_master_start(i2c_cmd);
  i2c_master_write_byte(i2c_cmd, (0x1b << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
  i2c_master_write_byte(i2c_cmd, reg, ACK_CHECK_EN);
  i2c_master_write_byte(i2c_cmd, value, ACK_CHECK_EN);

  i2c_master_stop(i2c_cmd);
  esp_err_t res =
      i2c_master_cmd_begin(i2c_port, i2c_cmd, 500 / portTICK_PERIOD_MS);

  if (res != ESP_OK) {
    ESP_LOGE("RR", "Unable to write to TAS5711");
  }
  i2c_cmd_link_delete(i2c_cmd);
}

TAS5711AudioSink::~TAS5711AudioSink() {}

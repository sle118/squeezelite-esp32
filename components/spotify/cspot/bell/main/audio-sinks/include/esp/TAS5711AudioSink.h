#ifndef TAS5711AUDIOSINK_H
#define TAS5711AUDIOSINK_H

#include <driver/i2c.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <iostream>
#include <vector>
#include "BufferedAudioSink.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "esp_log.h"

class TAS5711AudioSink : public BufferedAudioSink {
 public:
  TAS5711AudioSink();
  ~TAS5711AudioSink();

  void writeReg(uint8_t reg, uint8_t value);

 private:
  i2c_config_t i2c_config;
  i2c_port_t i2c_port = I2C_NUM_0;
};

#endif

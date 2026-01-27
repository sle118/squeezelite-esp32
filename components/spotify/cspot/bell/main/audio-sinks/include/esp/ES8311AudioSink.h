#ifndef ES8311AUDIOSINK_H
#define ES8311AUDIOSINK_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <iostream>
#include <vector>
#include "BufferedAudioSink.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "esp_log.h"

class ES8311AudioSink : public BufferedAudioSink {
 public:
  ES8311AudioSink();
  ~ES8311AudioSink();
  void writeReg(uint8_t reg_add, uint8_t data);
  void volumeChanged(uint16_t volume);
  void setSampleRate(uint32_t sampleRate);

 private:
};

#endif
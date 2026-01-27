#ifndef BUFFEREDAUDIOSINK_H
#define BUFFEREDAUDIOSINK_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <iostream>
#include <vector>
#include "AudioSink.h"
#include "esp_err.h"
#include "esp_log.h"

class BufferedAudioSink : public AudioSink {
 public:
  void feedPCMFrames(const uint8_t* buffer, size_t bytes) override;
  bool setParams(uint32_t sampleRate, uint8_t channelCount,
                 uint8_t bitDepth) override;

 protected:
  void startI2sFeed(size_t buf_size = 4096 * 8);
  void feedPCMFramesInternal(const void* pvItem, size_t xItemSize);

 private:
};

#endif
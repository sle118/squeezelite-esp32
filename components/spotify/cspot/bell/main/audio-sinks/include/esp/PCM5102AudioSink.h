#ifndef PCM5102AUDIOSINK_H
#define PCM5102AUDIOSINK_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <iostream>
#include <vector>
#include "BufferedAudioSink.h"
#include "esp_err.h"
#include "esp_log.h"

class PCM5102AudioSink : public BufferedAudioSink {
 public:
  PCM5102AudioSink();
  ~PCM5102AudioSink();

 private:
};

#endif
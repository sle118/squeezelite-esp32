#ifndef INTERNALAUDIOSINK_H
#define INTERNALAUDIOSINK_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <iostream>
#include <vector>
#include "BufferedAudioSink.h"
#include "esp_err.h"
#include "esp_log.h"

class InternalAudioSink : public BufferedAudioSink {
 public:
  InternalAudioSink();
  ~InternalAudioSink();

 private:
};

#endif

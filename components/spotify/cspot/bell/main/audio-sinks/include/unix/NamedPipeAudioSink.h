#pragma once

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint8_t
#include <fstream>   // for ofstream

#include "AudioSink.h"  // for AudioSink

class NamedPipeAudioSink : public AudioSink {
 public:
  NamedPipeAudioSink();
  ~NamedPipeAudioSink();
  void feedPCMFrames(const uint8_t* buffer, size_t bytes);

 private:
  std::ofstream namedPipeFile;
};

#pragma once

#include <cstddef>
#include <cstring>
#include <istream>
#include "CodecType.h"
#include "StreamInfo.h"

namespace bell {
class AudioContainer {
 protected:
  std::istream& istr;

 public:
  bell::SampleRate sampleRate;
  bell::BitWidth bitWidth;
  int channels;

  AudioContainer(std::istream& istr) : istr(istr) {}

  virtual std::byte* readSample(uint32_t& len) = 0;
  virtual void consumeBytes(uint32_t len) = 0;
  virtual void parseSetupData() = 0;
  virtual bell::AudioCodec getCodec() = 0;
};
}  // namespace bell

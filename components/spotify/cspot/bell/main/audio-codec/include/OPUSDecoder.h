#pragma once

#include <stdint.h>  // for uint8_t, uint32_t, int16_t

#include "BaseCodec.h"  // for BaseCodec

struct OpusDecoder;

namespace bell {
class OPUSDecoder : public BaseCodec {
 private:
  OpusDecoder* opus;
  int16_t* pcmData;

 public:
  OPUSDecoder();
  ~OPUSDecoder();
  bool setup(uint32_t sampleRate, uint8_t channelCount,
             uint8_t bitDepth) override;
  uint8_t* decode(uint8_t* inData, uint32_t& inLen, uint32_t& outLen) override;
};
}  // namespace bell

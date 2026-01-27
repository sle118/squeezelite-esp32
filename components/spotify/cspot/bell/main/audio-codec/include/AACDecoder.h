#pragma once

#include <stdint.h>  // for uint8_t, uint32_t, int16_t
#include <vector>

#include "BaseCodec.h"              // for BaseCodec
#include "pvmp4audiodecoder_api.h"  // for tPVMP4AudioDecoderExternal

namespace bell {
class AudioContainer;

class AACDecoder : public BaseCodec {
 private:
  tPVMP4AudioDecoderExternal* aacDecoder;
  std::vector<uint8_t> inputBuffer;
  std::vector<int16_t> outputBuffer;
  void* pMem;
  bool firstFrame = true;

  int getDecodedStreamType();

 public:
  AACDecoder();
  ~AACDecoder();
  bool setup(uint32_t sampleRate, uint8_t channelCount,
             uint8_t bitDepth) override;
  bool setup(AudioContainer* container) override;
  uint8_t* decode(uint8_t* inData, uint32_t& inLen, uint32_t& outLen) override;
};
}  // namespace bell

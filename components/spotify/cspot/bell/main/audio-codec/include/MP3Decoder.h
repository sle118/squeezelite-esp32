#pragma once

#include <stdint.h>  // for uint8_t, uint32_t, int16_t

#include "BaseCodec.h"  // for BaseCodec
#include "mp3dec.h"     // for HMP3Decoder, MP3FrameInfo

namespace bell {
class AudioContainer;

class MP3Decoder : public BaseCodec {
 private:
  HMP3Decoder mp3;
  int16_t* pcmData;
  MP3FrameInfo frame = {};

 public:
  MP3Decoder();
  ~MP3Decoder();
  bool setup(uint32_t sampleRate, uint8_t channelCount,
             uint8_t bitDepth) override;

  bool setup(AudioContainer* container) override;
  uint8_t* decode(uint8_t* inData, uint32_t& inLen, uint32_t& outLen) override;
};
}  // namespace bell

#pragma once

#include <stdint.h>  // for uint8_t, uint32_t, int16_t

#include "BaseCodec.h"     // for BaseCodec
#include "ivorbiscodec.h"  // for vorbis_comment, vorbis_dsp_state, vorbis_info
#include "ogg.h"           // for ogg_packet

namespace bell {
class AudioContainer;

class VorbisDecoder : public BaseCodec {
 private:
  vorbis_info* vi = nullptr;
  vorbis_comment* vc = nullptr;
  vorbis_dsp_state* vd = nullptr;
  ogg_packet op = {};
  int16_t* pcmData;

 public:
  VorbisDecoder();
  ~VorbisDecoder();
  bool setup(uint32_t sampleRate, uint8_t channelCount,
             uint8_t bitDepth) override;
  uint8_t* decode(uint8_t* inData, uint32_t& inLen, uint32_t& outLen) override;
  bool setup(AudioContainer* container) override;

 private:
  void setPacket(uint8_t* inData, uint32_t inLen) const;
};
}  // namespace bell

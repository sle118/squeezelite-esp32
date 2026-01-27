#include "MP3Decoder.h"

#include <stdlib.h>  // for free, malloc
#include <cstdio>

namespace bell {
class AudioContainer;
}  // namespace bell

using namespace bell;

MP3Decoder::MP3Decoder() {
  mp3 = MP3InitDecoder();
  pcmData =
      (int16_t*)malloc(MAX_NSAMP * MAX_NGRAN * MAX_NCHAN * sizeof(int16_t));
}

MP3Decoder::~MP3Decoder() {
  MP3FreeDecoder(mp3);
  free(pcmData);
}

bool MP3Decoder::setup(uint32_t sampleRate, uint8_t channelCount,
                       uint8_t bitDepth) {
  return true;
}

bool MP3Decoder::setup(AudioContainer* container) {
  return true;
}

uint8_t* MP3Decoder::decode(uint8_t* inData, uint32_t& inLen,
                            uint32_t& outLen) {
  if (!inData || inLen == 0)
    return nullptr;
  int status = MP3Decode(mp3, static_cast<unsigned char**>(&inData),
                         reinterpret_cast<int*>(&inLen),
                         static_cast<short*>(this->pcmData),
                         /* useSize */ 0);
  MP3GetLastFrameInfo(mp3, &frame);
  if (status != ERR_MP3_NONE) {
    lastErrno = status;
    inLen -= 2;
    outLen = 0;
    return nullptr;
  }
  if (sampleRate != frame.samprate) {
    this->sampleRate = frame.samprate;
  }

  if (channelCount != frame.nChans) {
    this->channelCount = frame.nChans;
  }
  outLen = frame.outputSamps * sizeof(int16_t);
  return (uint8_t*)pcmData;
}

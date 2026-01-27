#include "OPUSDecoder.h"

#include <stdlib.h>  // for free, malloc

#include "CodecType.h"  // for bell
#include "opus.h"       // for opus_decoder_destroy, opus_decode, opus_decod...

using namespace bell;

#define MAX_FRAME_SIZE 6 * 960
#define MAX_CHANNELS 2

// dummy structure, just to get access to channels
struct OpusDecoder {
  int dummy1;
  int dummy2;
  int channels;
};

OPUSDecoder::OPUSDecoder() {
  opus = nullptr;
  pcmData = (int16_t*)malloc(MAX_FRAME_SIZE * MAX_CHANNELS * sizeof(int16_t));
}

OPUSDecoder::~OPUSDecoder() {
  if (opus)
    opus_decoder_destroy(opus);
  free(pcmData);
}

bool OPUSDecoder::setup(uint32_t sampleRate, uint8_t channelCount,
                        uint8_t bitDepth) {
  if (opus)
    opus_decoder_destroy(opus);
  opus = opus_decoder_create((int32_t)sampleRate, channelCount, &lastErrno);
  return !lastErrno;
}

uint8_t* OPUSDecoder::decode(uint8_t* inData, uint32_t& inLen,
                             uint32_t& outLen) {
  if (!inData)
    return nullptr;
  outLen =
      opus_decode(opus, static_cast<unsigned char*>(inData),
                  static_cast<int32_t>(inLen), pcmData, MAX_FRAME_SIZE, false);
  outLen *= opus->channels * sizeof(int16_t);
  inLen = 0;
  return (uint8_t*)pcmData;
}

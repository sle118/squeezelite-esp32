#ifndef BELL_DISABLE_CODECS
#ifndef DECODER_GLOBALS_H
#define DECODER_GLOBALS_H

#define AAC_READBUF_SIZE (4 * AAC_MAINBUF_SIZE * AAC_MAX_NCHANS)
#define MP3_READBUF_SIZE (2 * 1024);

#include <stdio.h>  // for NULL

// #include "aacdec.h"  // for AACFreeDecoder, AACInitDecoder, HAACDecoder
#include "mp3dec.h"  // for MP3FreeDecoder, MP3InitDecoder, HMP3Decoder

namespace bell {
class DecodersInstance {
 public:
  DecodersInstance(){};
  ~DecodersInstance() {
    MP3FreeDecoder(mp3Decoder);
    // AACFreeDecoder(aacDecoder);
  };

  // HAACDecoder aacDecoder = NULL;
  HMP3Decoder mp3Decoder = NULL;

  void ensureAAC() {
    // if (aacDecoder == NULL) {
    // aacDecoder = AACInitDecoder();
    // }
  }

  void ensureMP3() {
    if (mp3Decoder == NULL) {
      mp3Decoder = MP3InitDecoder();
    }
  }
};

extern bell::DecodersInstance* decodersInstance;

void createDecoders();
}  // namespace bell

#endif
#endif

#include "DecoderGlobals.h"

bell::DecodersInstance* bell::decodersInstance;

void bell::createDecoders() {
  bell::decodersInstance = new bell::DecodersInstance();
}
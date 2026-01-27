#include "AudioContainers.h"

#include <string.h>      // for memcmp
#include <cstddef>       // for byte
#include "BellLogger.h"  // for BellLogger

#include "ADTSContainer.h"  // for AACContainer
#include "CodecType.h"      // for bell
#include "MP3Container.h"   // for MP3Container

namespace bell {
class AudioContainer;
}  // namespace bell

using namespace bell;

std::unique_ptr<bell::AudioContainer> AudioContainers::guessAudioContainer(
    std::istream& istr) {
  std::byte tmp[14];
  istr.read((char*)tmp, sizeof(tmp));

  if (memcmp(tmp, "\xFF\xF1", 2) == 0 || memcmp(tmp, "\xFF\xF9", 2) == 0) {
    // AAC found
    BELL_LOG(info, "AudioContainers",
             "Mime guesser found AAC in ADTS format, creating ADTSContainer");
    return std::make_unique<bell::ADTSContainer>(istr, tmp);
  } else if (memcmp(tmp, "\xFF\xFB", 2) == 0 ||
             memcmp(tmp, "\x49\x44\x33", 3) == 0) {
    // MP3 Found
    BELL_LOG(info, "AudioContainers",
             "Mime guesser found MP3 format, creating MP3Container");

    return std::make_unique<bell::MP3Container>(istr, tmp);
  }

  BELL_LOG(error, "AudioContainers",
           "Mime guesser found no supported format [%X, %X]", tmp[0], tmp[1]);
  return nullptr;
}

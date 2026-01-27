#include "AudioCodecs.h"

#include <map>          // for map, operator!=, map<>::iterator, map<>:...
#include <type_traits>  // for remove_extent_t

#include "AudioContainer.h"  // for AudioContainer

namespace bell {
class BaseCodec;
}  // namespace bell

using namespace bell;

#ifdef BELL_CODEC_AAC
#include "AACDecoder.h"  // for AACDecoder

static std::shared_ptr<AACDecoder> codecAac;
#endif

#ifdef BELL_CODEC_MP3
#include "MP3Decoder.h"  // for MP3Decoder

static std::shared_ptr<MP3Decoder> codecMp3;
#endif

#ifdef BELL_CODEC_VORBIS
#include "VorbisDecoder.h"  // for VorbisDecoder

static std::shared_ptr<VorbisDecoder> codecVorbis;
#endif

#ifdef BELL_CODEC_OPUS
#include "OPUSDecoder.h"  // for OPUSDecoder

static std::shared_ptr<OPUSDecoder> codecOpus;
#endif

std::map<AudioCodec, std::shared_ptr<BaseCodec>> customCodecs;

std::shared_ptr<BaseCodec> AudioCodecs::getCodec(AudioCodec type) {
  if (customCodecs.find(type) != customCodecs.end())
    return customCodecs[type];
  switch (type) {
#ifdef BELL_CODEC_AAC
    case AudioCodec::AAC:
      if (codecAac)
        return codecAac;
      codecAac = std::make_shared<AACDecoder>();
      return codecAac;
#endif
#ifdef BELL_CODEC_MP3
    case AudioCodec::MP3:
      if (codecMp3)
        return codecMp3;
      codecMp3 = std::make_shared<MP3Decoder>();
      return codecMp3;
#endif
#ifdef BELL_CODEC_VORBIS
    case AudioCodec::VORBIS:
      if (codecVorbis)
        return codecVorbis;
      codecVorbis = std::make_shared<VorbisDecoder>();
      return codecVorbis;
#endif
#ifdef BELL_CODEC_OPUS
    case AudioCodec::OPUS:
      if (codecOpus)
        return codecOpus;
      codecOpus = std::make_shared<OPUSDecoder>();
      return codecOpus;
#endif
    default:
      return nullptr;
  }
}

std::shared_ptr<BaseCodec> AudioCodecs::getCodec(AudioContainer* container) {
  auto codec = getCodec(container->getCodec());
  if (codec != nullptr) {
    codec->setup(container);
  }
  return codec;
}
void AudioCodecs::addCodec(AudioCodec type,
                           const std::shared_ptr<BaseCodec>& codec) {
  customCodecs[type] = codec;
}

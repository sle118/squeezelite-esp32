#pragma once

#include <memory>  // for shared_ptr

#include "AudioContainer.h"  // for AudioContainer
#include "BaseCodec.h"       // for BaseCodec
#include "CodecType.h"       // for AudioCodec

namespace bell {
class AudioCodecs {
 public:
  static std::shared_ptr<BaseCodec> getCodec(AudioCodec type);
  static std::shared_ptr<BaseCodec> getCodec(AudioContainer* container);
  static void addCodec(AudioCodec type,
                       const std::shared_ptr<BaseCodec>& codec);
};
}  // namespace bell

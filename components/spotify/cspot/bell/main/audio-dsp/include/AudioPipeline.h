#pragma once

#include <memory>  // for shared_ptr, unique_ptr
#include <mutex>   // for mutex
#include <vector>  // for vector

#include "StreamInfo.h"  // for StreamInfo

namespace bell {
class AudioTransform;
class Gain;

class AudioPipeline {
 private:
  std::shared_ptr<Gain> headroomGainTransform;

 public:
  AudioPipeline();
  ~AudioPipeline(){};

  std::mutex accessMutex;
  std::vector<std::shared_ptr<AudioTransform>> transforms;

  void recalculateHeadroom();
  void addTransform(std::shared_ptr<AudioTransform> transform);
  void volumeUpdated(int volume);
  std::unique_ptr<StreamInfo> process(std::unique_ptr<StreamInfo> data);
};
};  // namespace bell
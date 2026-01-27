#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include "StreamInfo.h"
#include "TransformConfig.h"

namespace bell {
class AudioTransform {
 protected:
  std::mutex accessMutex;

 public:
  virtual std::unique_ptr<StreamInfo> process(
      std::unique_ptr<StreamInfo> data) = 0;
  virtual void sampleRateChanged(uint32_t sampleRate){};
  virtual float calculateHeadroom() { return 0; };

  virtual void reconfigure(){};

  std::string filterType;
  std::unique_ptr<TransformConfig> config;

  AudioTransform() = default;
  virtual ~AudioTransform() = default;
};
};  // namespace bell
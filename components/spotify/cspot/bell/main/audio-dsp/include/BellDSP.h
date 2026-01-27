#pragma once

#include <stddef.h>    // for size_t
#include <stdint.h>    // for uint32_t, uint8_t
#include <functional>  // for function
#include <memory>      // for shared_ptr, unique_ptr
#include <mutex>       // for mutex
#include <vector>      // for vector

#include "StreamInfo.h"  // for BitWidth

namespace bell {
class AudioPipeline;
class CentralAudioBuffer;

#define MAX_INT16 32767

class BellDSP {
 public:
  BellDSP(std::shared_ptr<CentralAudioBuffer> centralAudioBuffer);
  ~BellDSP(){};

  class AudioEffect {
   public:
    AudioEffect() = default;
    ~AudioEffect() = default;
    size_t duration;
    virtual void apply(float* sampleData, size_t samples,
                       size_t relativePosition) = 0;
  };

  class FadeEffect : public AudioEffect {
   private:
    std::function<void()> onFinish;
    bool isFadeIn;

   public:
    FadeEffect(size_t duration, bool isFadeIn,
               std::function<void()> onFinish = nullptr);
    ~FadeEffect(){};

    void apply(float* sampleData, size_t samples, size_t relativePosition);
  };

  void applyPipeline(std::shared_ptr<AudioPipeline> pipeline);
  void queryInstantEffect(std::unique_ptr<AudioEffect> instantEffect);

  std::shared_ptr<AudioPipeline> getActivePipeline();

  size_t process(uint8_t* data, size_t bytes, int channels, uint32_t sampleRate,
                 BitWidth bitWidth);

 private:
  std::shared_ptr<AudioPipeline> activePipeline;
  std::shared_ptr<CentralAudioBuffer> buffer;
  std::mutex accessMutex;
  std::vector<float> dataLeft = std::vector<float>(1024);
  std::vector<float> dataRight = std::vector<float>(1024);

  std::unique_ptr<AudioEffect> underflowEffect = nullptr;
  std::unique_ptr<AudioEffect> startEffect = nullptr;
  std::unique_ptr<AudioEffect> instantEffect = nullptr;

  size_t samplesSinceInstantQueued;
};
};  // namespace bell

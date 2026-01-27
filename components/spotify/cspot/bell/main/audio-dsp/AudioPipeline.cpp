#include "AudioPipeline.h"

#include <type_traits>  // for remove_extent_t
#include <utility>      // for move

#include "AudioTransform.h"   // for AudioTransform
#include "BellLogger.h"       // for AbstractLogger, BELL_LOG
#include "TransformConfig.h"  // for TransformConfig

using namespace bell;

AudioPipeline::AudioPipeline(){
    // this->headroomGainTransform = std::make_shared<Gain>(Channels::LEFT_RIGHT);
    // this->transforms.push_back(this->headroomGainTransform);
};

void AudioPipeline::addTransform(std::shared_ptr<AudioTransform> transform) {
  transforms.push_back(transform);
  recalculateHeadroom();
}

void AudioPipeline::recalculateHeadroom() {
  float headroom = 0.0f;

  // Find largest headroom required by any transform down the chain, and apply it
  for (auto transform : transforms) {
    if (headroom < transform->calculateHeadroom()) {
      headroom = transform->calculateHeadroom();
    }
  }

  // headroomGainTransform->configure(-headroom);
}

void AudioPipeline::volumeUpdated(int volume) {
  BELL_LOG(debug, "AudioPipeline", "Requested");
  std::scoped_lock lock(this->accessMutex);
  for (auto transform : transforms) {
    transform->config->currentVolume = volume;
    transform->reconfigure();
  }
  BELL_LOG(debug, "AudioPipeline", "Volume applied, DSP reconfigured");
}

std::unique_ptr<StreamInfo> AudioPipeline::process(
    std::unique_ptr<StreamInfo> data) {
  std::scoped_lock lock(this->accessMutex);
  for (auto& transform : transforms) {
    data = transform->process(std::move(data));
  }

  return data;
}
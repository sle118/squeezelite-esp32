#include "BellDSP.h"

#include <type_traits>  // for remove_extent_t
#include <utility>      // for move

#include "AudioPipeline.h"       // for CentralAudioBuffer
#include "CentralAudioBuffer.h"  // for CentralAudioBuffer

using namespace bell;

BellDSP::FadeEffect::FadeEffect(size_t duration, bool isFadeIn,
                                std::function<void()> onFinish) {
  this->duration = duration;
  this->onFinish = onFinish;
  this->isFadeIn = isFadeIn;
}

void BellDSP::FadeEffect::apply(float* audioData, size_t samples,
                                size_t relativePosition) {
  float effect = (this->duration - relativePosition) / (float)this->duration;

  if (isFadeIn) {
    effect = relativePosition / (float)this->duration;
  }

  for (int x = 0; x <= samples; x++) {
    audioData[x] *= effect;
  }

  if (relativePosition + samples > this->duration && onFinish != nullptr) {
    onFinish();
  }
}

BellDSP::BellDSP(std::shared_ptr<CentralAudioBuffer> buffer) {
  this->buffer = buffer;
};

void BellDSP::applyPipeline(std::shared_ptr<AudioPipeline> pipeline) {
  std::scoped_lock lock(accessMutex);
  activePipeline = pipeline;
}

void BellDSP::queryInstantEffect(std::unique_ptr<AudioEffect> instantEffect) {
  this->instantEffect = std::move(instantEffect);
  samplesSinceInstantQueued = 0;
}

size_t BellDSP::process(uint8_t* data, size_t bytes, int channels,
                        uint32_t sampleRate, BitWidth bitWidth) {
  if (bytes > 1024 * 2 * channels) {
    return 0;
  }

  // Create a StreamInfo object to pass to the pipeline
  auto streamInfo = std::make_unique<StreamInfo>();
  streamInfo->numChannels = channels;
  streamInfo->sampleRate = static_cast<bell::SampleRate>(sampleRate);
  streamInfo->bitwidth = bitWidth;
  streamInfo->numSamples = bytes / channels / 2;

  std::scoped_lock lock(accessMutex);

  int16_t* data16Bit = (int16_t*)data;

  int length16 = bytes / 4;

  for (size_t i = 0; i < length16; i++) {
    dataLeft[i] = (data16Bit[i * 2] / (float)MAX_INT16);  // Normalize left
    dataRight[i] =
        (data16Bit[i * 2 + 1] / (float)MAX_INT16);  // Normalize right
  }
  float* sampleData[] = {&dataLeft[0], &dataRight[0]};
  streamInfo->data = sampleData;

  if (activePipeline) {
    streamInfo = activePipeline->process(std::move(streamInfo));
  }

  if (this->instantEffect != nullptr) {
    this->instantEffect->apply(dataLeft.data(), length16,
                               samplesSinceInstantQueued);

    if (streamInfo->numSamples > 1) {
      this->instantEffect->apply(dataRight.data(), length16,
                                 samplesSinceInstantQueued);
    }

    samplesSinceInstantQueued += length16;

    if (this->instantEffect->duration <= samplesSinceInstantQueued) {
      this->instantEffect = nullptr;
    }
  }

  for (size_t i = 0; i < length16; i++) {
    if (dataLeft[i] > 1.0f) {
      dataLeft[i] = 1.0f;
    }

    // Data has been downmixed to mono
    if (streamInfo->numChannels == 1) {
      data16Bit[i] = dataLeft[i] * MAX_INT16;  // Denormalize left
    } else {
      data16Bit[i * 2] = dataLeft[i] * MAX_INT16;       // Denormalize left
      data16Bit[i * 2 + 1] = dataRight[i] * MAX_INT16;  // Denormalize right
    }
  }

  if (streamInfo->numChannels == 1) {
    return bytes / 2;
  }

  return bytes;
}

std::shared_ptr<AudioPipeline> BellDSP::getActivePipeline() {
  return activePipeline;
}

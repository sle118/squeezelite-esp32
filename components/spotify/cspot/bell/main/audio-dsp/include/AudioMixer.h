#pragma once

#include <cJSON.h>    // for cJSON_GetObjectItem, cJSON, cJSON_IsArray
#include <stddef.h>   // for NULL
#include <algorithm>  // for find
#include <cstdint>    // for uint8_t
#include <memory>     // for unique_ptr
#include <stdexcept>  // for invalid_argument
#include <vector>     // for vector

#include "AudioTransform.h"  // for AudioTransform
#include "StreamInfo.h"      // for StreamInfo

namespace bell {
class AudioMixer : public bell::AudioTransform {
 public:
  enum DownmixMode { DEFAULT };

  struct MixerConfig {
    std::vector<int> source;
    int destination;
  };

  AudioMixer();
  ~AudioMixer(){};
  // Amount of channels in the input
  int from;

  // Amount of channels in the output
  int to;

  // Configuration of each channels in the mixer
  std::vector<MixerConfig> mixerConfig;

  std::unique_ptr<StreamInfo> process(
      std::unique_ptr<StreamInfo> data) override;

  void reconfigure() override {}

  void fromJSON(cJSON* json) {
    cJSON* mappedChannels = cJSON_GetObjectItem(json, "mapped_channels");

    if (mappedChannels == NULL || !cJSON_IsArray(mappedChannels)) {
      throw std::invalid_argument("Mixer configuration invalid");
    }

    this->mixerConfig = std::vector<MixerConfig>();

    cJSON* iterator = NULL;
    cJSON_ArrayForEach(iterator, mappedChannels) {
      std::vector<int> sources(0);
      cJSON* iteratorNested = NULL;
      cJSON_ArrayForEach(iteratorNested,
                         cJSON_GetObjectItem(iterator, "source")) {
        sources.push_back(iteratorNested->valueint);
      }

      int destination = cJSON_GetObjectItem(iterator, "destination")->valueint;

      this->mixerConfig.push_back(
          MixerConfig{.source = sources, .destination = destination});
    }

    std::vector<uint8_t> sources(0);

    for (auto& config : mixerConfig) {

      for (auto& source : config.source) {
        if (std::find(sources.begin(), sources.end(), source) ==
            sources.end()) {
          sources.push_back(source);
        }
      }
    }

    this->from = sources.size();
    this->to = mixerConfig.size();
  }
};
}  // namespace bell
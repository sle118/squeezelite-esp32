#pragma once

#include <stdint.h>       // for uint32_t
#include <map>            // for map
#include <memory>         // for unique_ptr, allocator
#include <mutex>          // for scoped_lock
#include <stdexcept>      // for invalid_argument
#include <string>         // for string, operator<, hash, operator==
#include <unordered_map>  // for operator!=, unordered_map, __hash_map_c...
#include <utility>        // for pair
#include <vector>         // for vector

#include "AudioTransform.h"   // for AudioTransform
#include "StreamInfo.h"       // for StreamInfo
#include "TransformConfig.h"  // for TransformConfig

extern "C" int dsps_biquad_f32_ae32(const float* input, float* output, int len,
                                    float* coef, float* w);

namespace bell {
class Biquad : public bell::AudioTransform {
 public:
  Biquad();
  ~Biquad(){};

  enum class Type {
    Free,
    Highpass,
    Lowpass,
    HighpassFO,
    LowpassFO,

    Peaking,
    Highshelf,
    HighshelfFO,
    Lowshelf,
    LowshelfFO,
    Notch,
    Bandpass,
    Allpass,
    AllpassFO
  };

  std::map<std::string, float> currentConfig;

  std::unordered_map<std::string, Type> const strMapType = {
      {"free", Type::Free},
      {"highpass", Type::Highpass},
      {"lowpass", Type::Lowpass},
      {"highpass_fo", Type::HighpassFO},
      {"lowpass_fo", Type::LowpassFO},
      {"peaking", Type::Peaking},
      {"highshelf", Type::Highshelf},
      {"highshelf_fo", Type::HighpassFO},
      {"lowshelf", Type::Lowshelf},
      {"lowshelf_fo", Type::LowpassFO},
      {"notch", Type::Notch},
      {"bandpass", Type::Bandpass},
      {"allpass", Type::Allpass},
      {"allpass_fo", Type::AllpassFO},
  };

  float freq, q, gain;
  int channel;
  Biquad::Type type;

  std::unique_ptr<StreamInfo> process(
      std::unique_ptr<StreamInfo> data) override;

  void configure(Type type, std::map<std::string, float>& config);

  void sampleRateChanged(uint32_t sampleRate) override;

  void reconfigure() override {
    std::scoped_lock lock(this->accessMutex);
    std::map<std::string, float> biquadConfig;
    this->channel = config->getChannels()[0];

    float invalid = -0x7C;

    auto type = config->getString("biquad_type");
    float bandwidth = config->getFloat("bandwidth", false, invalid);
    float slope = config->getFloat("slope", false, invalid);
    float gain = config->getFloat("gain", false, invalid);
    float frequency = config->getFloat("frequency", false, invalid);
    float q = config->getFloat("q", false, invalid);

    if (currentConfig["bandwidth"] == bandwidth &&
        currentConfig["slope"] == slope && currentConfig["gain"] == gain &&
        currentConfig["frequency"] == frequency && currentConfig["q"] == q) {
      return;
    }

    if (bandwidth != invalid)
      biquadConfig["bandwidth"] = bandwidth;
    if (slope != invalid)
      biquadConfig["slope"] = slope;
    if (gain != invalid)
      biquadConfig["gain"] = gain;
    if (frequency != invalid)
      biquadConfig["freq"] = frequency;
    if (q != invalid)
      biquadConfig["q"] = q;

    if (type == "free") {
      biquadConfig["a1"] = config->getFloat("a1");
      biquadConfig["a2"] = config->getFloat("a2");
      biquadConfig["b0"] = config->getFloat("b0");
      biquadConfig["b1"] = config->getFloat("b1");
      biquadConfig["b2"] = config->getFloat("b2");
    }

    auto typeElement = strMapType.find(type);
    if (typeElement != strMapType.end()) {
      this->configure(typeElement->second, biquadConfig);
    } else {
      throw std::invalid_argument("No biquad of type " + type);
    }
  }

 private:
  float coeffs[5];
  float w[2] = {1.0, 1.0};

  float sampleRate = 44100;

  // Generator methods for different filter types
  void highPassCoEffs(float f, float q);
  void highPassFOCoEffs(float f);
  void lowPassCoEffs(float f, float q);
  void lowPassFOCoEffs(float f);

  void peakCoEffs(float f, float gain, float q);
  void peakCoEffsBandwidth(float f, float gain, float bandwidth);

  void highShelfCoEffs(float f, float gain, float q);
  void highShelfCoEffsSlope(float f, float gain, float slope);
  void highShelfFOCoEffs(float f, float gain);

  void lowShelfCoEffs(float f, float gain, float q);
  void lowShelfCoEffsSlope(float f, float gain, float slope);
  void lowShelfFOCoEffs(float f, float gain);

  void notchCoEffs(float f, float gain, float q);
  void notchCoEffsBandwidth(float f, float gain, float bandwidth);

  void bandPassCoEffs(float f, float q);
  void bandPassCoEffsBandwidth(float f, float bandwidth);

  void allPassCoEffs(float f, float q);
  void allPassCoEffsBandwidth(float f, float bandwidth);
  void allPassFOCoEffs(float f);

  void normalizeCoEffs(float a0, float a1, float a2, float b0, float b1,
                       float b2);
};

}  // namespace bell
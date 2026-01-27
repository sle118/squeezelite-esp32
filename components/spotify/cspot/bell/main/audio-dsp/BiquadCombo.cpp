#include "BiquadCombo.h"

#include <stdio.h>  // for printf
#include <cmath>    // for sinf, M_PI
#include <utility>  // for move

using namespace bell;

BiquadCombo::BiquadCombo() {}

void BiquadCombo::sampleRateChanged(uint32_t sampleRate) {
  for (auto& biquad : biquads) {
    biquad->sampleRateChanged(sampleRate);
  }
}

std::vector<float> BiquadCombo::calculateBWQ(int order) {

  std::vector<float> qValues;
  for (int n = 0; n < order / 2; n++) {
    float q = 1.0f / (2.0f * sinf(M_PI / order * (((float)n) + 0.5)));
    qValues.push_back(q);
  }

  if (order % 2 > 0) {
    qValues.push_back(-1.0);
  }

  printf("%d\n", qValues.size());

  return qValues;
}

std::vector<float> BiquadCombo::calculateLRQ(int order) {
  auto qValues = calculateBWQ(order / 2);

  if (order % 4 > 0) {
    qValues.pop_back();
    qValues.insert(qValues.end(), qValues.begin(), qValues.end());
    qValues.push_back(0.5);
  } else {
    qValues.insert(qValues.end(), qValues.begin(), qValues.end());
  }

  return qValues;
}

void BiquadCombo::butterworth(float freq, int order, FilterType type) {
  std::vector<float> qValues = calculateBWQ(order);
  for (auto& q : qValues) {}
}

void BiquadCombo::linkwitzRiley(float freq, int order, FilterType type) {
  std::vector<float> qValues = calculateLRQ(order);
  for (auto& q : qValues) {
    auto filter = std::make_unique<Biquad>();
    filter->channel = channel;

    auto config = std::map<std::string, float>();
    config["freq"] = freq;
    config["q"] = q;

    if (q >= 0.0) {
      if (type == FilterType::Highpass) {
        filter->configure(Biquad::Type::Highpass, config);
      } else {
        filter->configure(Biquad::Type::Lowpass, config);
      }
    } else {
      if (type == FilterType::Highpass) {
        filter->configure(Biquad::Type::HighpassFO, config);
      } else {
        filter->configure(Biquad::Type::LowpassFO, config);
      }
    }

    this->biquads.push_back(std::move(filter));
  }
}

std::unique_ptr<StreamInfo> BiquadCombo::process(
    std::unique_ptr<StreamInfo> data) {
  std::scoped_lock lock(this->accessMutex);
  for (auto& transform : this->biquads) {
    data = transform->process(std::move(data));
  }

  return data;
}
#include "Compressor.h"

#include <cstdlib>  // for abs

using namespace bell;

float log2f_approx(float X) {
  float Y, F;
  int E;
  F = frexpf(fabsf(X), &E);
  Y = 1.23149591368684f;
  Y *= F;
  Y += -4.11852516267426f;
  Y *= F;
  Y += 6.02197014179219f;
  Y *= F;
  Y += -3.13396450166353f;
  Y += E;
  return (Y);
}

Compressor::Compressor() {}

void Compressor::sumChannels(std::unique_ptr<StreamInfo>& data) {
  tmp.resize(data->numSamples);
  for (int i = 0; i < data->numSamples; i++) {
    float sum = 0.0f;
    for (auto& channel : channels) {
      sum += data->data[channel][i];
    }
    tmp[i] = sum;
  }
}

void Compressor::calLoudness() {
  for (auto& value : tmp) {
    value = 20 * log10f_fast(std::abs(value) + 1.0e-9f);
    if (value >= lastLoudness) {
      value = attack * lastLoudness + (1.0 - attack) * value;
    } else {
      value = release * lastLoudness + (1.0 - release) * value;
    }

    lastLoudness = value;
  }
}

void Compressor::calGain() {
  for (auto& value : tmp) {
    if (value > threshold) {
      value = -(value - threshold) * (factor - 1.0) / factor;
    } else {
      value = 0.0f;
    }

    value += makeupGain;

    // convert to linear
    value = pow10f(value / 20.0f);
  }
}

void Compressor::applyGain(std::unique_ptr<StreamInfo>& data) {
  for (int i = 0; i < data->numSamples; i++) {
    for (auto& channel : channels) {
      data->data[channel][i] *= tmp[i];
    }
  }
}

void Compressor::configure(std::vector<int> channels, float attack,
                           float release, float threshold, float factor,
                           float makeupGain) {
  this->channels = channels;
  this->attack = expf(-1000.0 / this->sampleRate / attack);
  this->release = expf(-1000.0 / this->sampleRate / release);
  this->threshold = threshold;
  this->factor = factor;
  this->makeupGain = makeupGain;
}

std::unique_ptr<StreamInfo> Compressor::process(
    std::unique_ptr<StreamInfo> data) {
  std::scoped_lock lock(this->accessMutex);
  sumChannels(data);
  calLoudness();
  calGain();
  applyGain(data);
  return data;
}

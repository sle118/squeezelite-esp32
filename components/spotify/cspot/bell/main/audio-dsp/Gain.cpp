#include "Gain.h"

#include <cmath>   // for pow
#include <string>  // for string

using namespace bell;

Gain::Gain() : AudioTransform() {
  this->gainFactor = 1.0f;
  this->filterType = "gain";
}

void Gain::configure(std::vector<int> channels, float gainDB) {
  this->channels = channels;
  this->gainDb = gainDB;
  this->gainFactor = std::pow(10.0f, gainDB / 20.0f);
}

std::unique_ptr<StreamInfo> Gain::process(std::unique_ptr<StreamInfo> data) {
  std::scoped_lock lock(this->accessMutex);
  for (int i = 0; i < data->numSamples; i++) {
    // Apply gain to all channels
    for (auto& channel : channels) {
      data->data[channel][i] *= gainFactor;
    }
  }

  return data;
}
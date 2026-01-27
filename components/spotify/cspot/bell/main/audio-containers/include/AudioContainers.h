#pragma once

#include <iostream>  // for istream
#include <memory>    // for unique_ptr

namespace bell {
class AudioContainer;
}  // namespace bell

namespace bell::AudioContainers {
std::unique_ptr<bell::AudioContainer> guessAudioContainer(std::istream& istr);
}  // namespace bell::AudioContainers

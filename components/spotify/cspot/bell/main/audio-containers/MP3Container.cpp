#include "MP3Container.h"

#include <cstring>  // for memmove

#include "StreamInfo.h"  // for BitWidth, BitWidth::BW_16, SampleRate, Sampl...
#include "mp3dec.h"      // for MP3FindSyncWord

using namespace bell;

MP3Container::MP3Container(std::istream& istr, const std::byte* headingBytes)
    : bell::AudioContainer(istr) {
  if (headingBytes != nullptr) {
    memcpy(buffer.data(), headingBytes, 7);
    bytesInBuffer = 7;
  }
}

bool MP3Container::fillBuffer() {
  if (this->bytesInBuffer < MP3_MAX_FRAME_SIZE * 2) {
    this->istr.read((char*)buffer.data() + bytesInBuffer,
                    buffer.size() - bytesInBuffer);
    this->bytesInBuffer += istr.gcount();
  }
  return this->bytesInBuffer >= MP3_MAX_FRAME_SIZE * 2;
}

void MP3Container::consumeBytes(uint32_t len) {
  dataOffset += len;
}

std::byte* MP3Container::readSample(uint32_t& len) {
  // Align data if previous read was offseted
  if (dataOffset > 0 && bytesInBuffer > 0) {
    size_t toConsume = std::min(dataOffset, bytesInBuffer);
    memmove(buffer.data(), buffer.data() + toConsume,
            buffer.size() - toConsume);

    dataOffset -= toConsume;
    bytesInBuffer -= toConsume;
  }

  if (!this->fillBuffer()) {
    len = 0;
    return nullptr;
  }

  int startOffset =
      MP3FindSyncWord((uint8_t*)this->buffer.data(), bytesInBuffer);

  if (startOffset < 0) {
    // Discard word
    dataOffset = MP3_MAX_FRAME_SIZE;
    return nullptr;
  }

  dataOffset += startOffset;

  len = bytesInBuffer - dataOffset;

  return this->buffer.data() + dataOffset;
}

void MP3Container::parseSetupData() {
  channels = 2;
  sampleRate = bell::SampleRate::SR_44100;
  bitWidth = bell::BitWidth::BW_16;
}

#include "ADTSContainer.h"

#include <cstring>  // for memmove
#include <iostream>

#include "StreamInfo.h"  // for BitWidth, BitWidth::BW_16, SampleRate, Sampl...
// #include "aacdec.h"      // for AACFindSyncWord

using namespace bell;

#define SYNC_WORLD_LEN 4
#define SYNCWORDH 0xff
#define SYNCWORDL 0xf0

// AAC ADTS frame header len
#define AAC_ADTS_FRAME_HEADER_LEN 9

// AAC ADTS frame sync verify
#define AAC_ADTS_SYNC_VERIFY(buf) \
  ((buf[0] == 0xff) && ((buf[1] & 0xf6) == 0xf0))

// AAC ADTS Frame size value stores in 13 bits started at the 31th bit from header
#define AAC_ADTS_FRAME_GETSIZE(buf) \
  ((buf[3] & 0x03) << 11 | buf[4] << 3 | buf[5] >> 5)

ADTSContainer::ADTSContainer(std::istream& istr, const std::byte* headingBytes)
    : bell::AudioContainer(istr) {
  if (headingBytes != nullptr) {
    memcpy(buffer.data(), headingBytes, 7);
    bytesInBuffer = 7;
  }
}

bool ADTSContainer::fillBuffer() {
  if (this->bytesInBuffer < AAC_MAX_FRAME_SIZE * 2) {
    this->istr.read((char*)buffer.data() + bytesInBuffer,
                    buffer.size() - bytesInBuffer);
    this->bytesInBuffer += istr.gcount();
  }
  return this->bytesInBuffer >= AAC_MAX_FRAME_SIZE;
}

bool ADTSContainer::resyncADTS() {
  int resyncOffset = 0;
  bool resyncValid = false;

  size_t validBytes = bytesInBuffer - dataOffset;

  while (!resyncValid && resyncOffset < validBytes) {
    uint8_t* buf = (uint8_t*)this->buffer.data() + dataOffset + resyncOffset;
    if (AAC_ADTS_SYNC_VERIFY(buf)) {
      // Read frame size, and check if a consecutive frame is available
      uint32_t frameSize = AAC_ADTS_FRAME_GETSIZE(buf);

      if (frameSize + resyncOffset > validBytes) {
        // Not enough data, discard this frame
        resyncOffset++;
        continue;
      }

      buf =
          (uint8_t*)this->buffer.data() + dataOffset + resyncOffset + frameSize;

      if (AAC_ADTS_SYNC_VERIFY(buf)) {
        buf += AAC_ADTS_FRAME_GETSIZE(buf);
        if (AAC_ADTS_SYNC_VERIFY(buf)) {
          protectionAbsent = (buf[1] & 1);

          // Found 3 consecutive frames, resynced
          resyncValid = true;
        }
      }
    } else {
      resyncOffset++;
    }
  }

  dataOffset += resyncOffset;
  return resyncValid;
}

void ADTSContainer::consumeBytes(uint32_t len) {
  dataOffset += len;
}

std::byte* ADTSContainer::readSample(uint32_t& len) {
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

  uint8_t* buf = (uint8_t*)buffer.data() + dataOffset;

  if (!AAC_ADTS_SYNC_VERIFY(buf)) {
    if (!resyncADTS()) {
      len = 0;
      return nullptr;
    }
  } else {
    protectionAbsent = (buf[1] & 1);
  }

  len = AAC_ADTS_FRAME_GETSIZE(buf);

  if (len > bytesInBuffer - dataOffset) {
    if (!resyncADTS()) {
      len = 0;
      return nullptr;
    }
  }

  return buffer.data() + dataOffset;
}

void ADTSContainer::parseSetupData() {
  channels = 2;
  sampleRate = bell::SampleRate::SR_44100;
  bitWidth = bell::BitWidth::BW_16;
}

#pragma once

#include <stddef.h>  // for size_t
#include <cstdint>   // for uint8_t
#include <memory>    // for shared_ptr, unique_ptr
#include <string>    // for basic_string, string
#include <vector>    // for vector

#include "mp3dec.h"  // for MP3FrameInfo

namespace bell {
class ByteStream;

class EncodedAudioStream {
 public:
  EncodedAudioStream();
  ~EncodedAudioStream();

  // Codecs supported by this stream class
  enum class AudioCodec { AAC, MP3, OGG, NONE };

  void openWithStream(std::unique_ptr<bell::ByteStream> byteStream);
  size_t decodeFrame(uint8_t* dst);
  bool isReadable();

 private:
  std::shared_ptr<ByteStream> innerStream;
  std::vector<uint8_t> inputBuffer;
  std::vector<short> outputBuffer;
  std::string TAG = "EncryptedAudioStream";

  uint8_t* decodePtr = 0;
  int bytesInBuffer = 0;
  size_t offset = 0;

  size_t decodedSampleRate = 44100;

  AudioCodec codec = AudioCodec::NONE;

  void guessDataFormat();

  void readFully(uint8_t* dst, size_t nbytes);
  bool vectorStartsWith(std::vector<uint8_t>&, std::vector<uint8_t>&);

  std::vector<uint8_t> aacMagicBytes = {0xFF, 0xF1};
  std::vector<uint8_t> aacMagicBytes4 = {0xFF, 0xF9};
  std::vector<uint8_t> mp3MagicBytesUntagged = {0xFF, 0xFB};
  std::vector<uint8_t> mp3MagicBytesIdc = {0x49, 0x44, 0x33};

  // AACFrameInfo aacFrameInfo;
  MP3FrameInfo mp3FrameInfo;

  size_t decodeFrameMp3(uint8_t* dst);
  size_t decodeFrameAAC(uint8_t* dst);
};
}  // namespace bell

#include "EncodedAudioStream.h"

#include <string.h>     // for memcpy, memmove
#include <stdexcept>    // for runtime_error
#include <type_traits>  // for remove_extent_t
#include <utility>      // for move

#include "BellLogger.h"      // for AbstractLogger, BELL_LOG, bell
#include "ByteStream.h"      // for ByteStream
#include "DecoderGlobals.h"  // for DecodersInstance, decodersInstance, AAC_...

using namespace bell;

EncodedAudioStream::EncodedAudioStream() {
  bell::decodersInstance->ensureAAC();
  bell::decodersInstance->ensureMP3();
  inputBuffer = std::vector<uint8_t>(1024 * 4);
  outputBuffer = std::vector<short>(2 * 2 * 4 * 4);
  decodePtr = inputBuffer.data();
}

EncodedAudioStream::~EncodedAudioStream() {
  this->innerStream->close();
}

void EncodedAudioStream::openWithStream(
    std::unique_ptr<bell::ByteStream> byteStream) {
  if (this->innerStream) {
    this->innerStream->close();
  }
  this->innerStream = std::move(byteStream);
  this->guessDataFormat();
}

bool EncodedAudioStream::vectorStartsWith(std::vector<uint8_t>& vec,
                                          std::vector<uint8_t>& start) {
  if (vec.size() < start.size()) {
    return false;
  }

  for (int i = 0; i < start.size(); i++) {
    if (vec[i] != start[i]) {
      return false;
    }
  }

  return true;
}

size_t EncodedAudioStream::decodeFrame(uint8_t* dst) {
  if (innerStream->size() != 0 &&
      innerStream->position() >= innerStream->size()) {
    return 0;
  }

  switch (this->codec) {
    case AudioCodec::AAC:
      return decodeFrameAAC(dst);
      break;
    case AudioCodec::MP3:
      return decodeFrameMp3(dst);
      break;
    default:
      break;
  }

  return 0;
}

size_t EncodedAudioStream::decodeFrameMp3(uint8_t* dst) {
  size_t writtenBytes = 0;
  int bufSize = MP3_READBUF_SIZE;

  int readBytes = innerStream->read(inputBuffer.data() + bytesInBuffer,
                                    bufSize - bytesInBuffer);
  if (readBytes > 0) {
    bytesInBuffer += readBytes;
    decodePtr = inputBuffer.data();
    offset = MP3FindSyncWord(inputBuffer.data(), bytesInBuffer);

    if (offset != -1) {
      bytesInBuffer -= offset;
      decodePtr += offset;

      int decodeStatus =
          MP3Decode(bell::decodersInstance->mp3Decoder, &decodePtr,
                    &bytesInBuffer, outputBuffer.data(), 0);
      MP3GetLastFrameInfo(bell::decodersInstance->mp3Decoder, &mp3FrameInfo);
      if (decodeStatus == ERR_MP3_NONE) {
        decodedSampleRate = mp3FrameInfo.samprate;
        writtenBytes =
            (mp3FrameInfo.bitsPerSample / 8) * mp3FrameInfo.outputSamps;

        memcpy(dst, outputBuffer.data(), writtenBytes);

      } else {
        BELL_LOG(info, TAG, "Error in frame, moving two bytes %d",
                 decodeStatus);
        decodePtr += 1;
        bytesInBuffer -= 1;
      }
    } else {
      BELL_LOG(info, TAG, "Unexpected error in data, skipping a word");
      decodePtr += 3800;
      bytesInBuffer -= 3800;
    }

    memmove(inputBuffer.data(), decodePtr, bytesInBuffer);
  }
  return writtenBytes;
}

bool EncodedAudioStream::isReadable() {
  return this->codec != AudioCodec::NONE;
}

size_t EncodedAudioStream::decodeFrameAAC(uint8_t* dst) {
  return 0;
  // size_t writtenBytes = 0;
  // auto bufSize = AAC_READBUF_SIZE;

  // int readBytes = innerStream->read(inputBuffer.data() + bytesInBuffer,
  //                                   bufSize - bytesInBuffer);
  // if (readBytes > 0) {
  //   bytesInBuffer += readBytes;
  //   decodePtr = inputBuffer.data();
  //   offset = AACFindSyncWord(inputBuffer.data(), bytesInBuffer);

  //   if (offset != -1) {
  //     bytesInBuffer -= offset;
  //     decodePtr += offset;

  //     int decodeStatus =
  //         AACDecode(bell::decodersInstance->aacDecoder, &decodePtr,
  //                   &bytesInBuffer, outputBuffer.data());
  //     AACGetLastFrameInfo(bell::decodersInstance->aacDecoder, &aacFrameInfo);
  //     if (decodeStatus == ERR_AAC_NONE) {
  //       decodedSampleRate = aacFrameInfo.sampRateOut;
  //       writtenBytes =
  //           (aacFrameInfo.bitsPerSample / 8) * aacFrameInfo.outputSamps;

  //       memcpy(dst, outputBuffer.data(), writtenBytes);

  //     } else {
  //       BELL_LOG(info, TAG, "Error in frame, moving two bytes %d",
  //                decodeStatus);
  //       decodePtr += 1;
  //       bytesInBuffer -= 1;
  //     }
  //   } else {
  //     BELL_LOG(info, TAG, "Unexpected error in data, skipping a word");
  //     decodePtr += 3800;
  //     bytesInBuffer -= 3800;
  //   }

  //   memmove(inputBuffer.data(), decodePtr, bytesInBuffer);
  // }
  // return writtenBytes;
}

void EncodedAudioStream::guessDataFormat() {
  // Read 14 bytes from the stream
  this->innerStream->read(inputBuffer.data(), 14);
  bytesInBuffer = 14;

  BELL_LOG(info, TAG, "No codec set, reading secret bytes");

  if (vectorStartsWith(inputBuffer, this->mp3MagicBytesIdc) ||
      vectorStartsWith(inputBuffer, this->mp3MagicBytesUntagged)) {
    BELL_LOG(info, TAG, "Detected MP3");
    codec = AudioCodec::MP3;
  } else if (vectorStartsWith(inputBuffer, this->aacMagicBytes) ||
             vectorStartsWith(inputBuffer, this->aacMagicBytes4)) {
    BELL_LOG(info, TAG, "Detected AAC");
    codec = AudioCodec::AAC;
  }

  if (codec == AudioCodec::NONE) {
    throw std::runtime_error("Codec not supported");
  }
}

void EncodedAudioStream::readFully(uint8_t* dst, size_t nbytes) {}

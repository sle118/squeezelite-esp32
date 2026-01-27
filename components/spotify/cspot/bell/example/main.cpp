#include <memory.h>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include "AudioCodecs.h"
#include "AudioContainers.h"
#include "BellHTTPServer.h"
#include "BellTask.h"
#include "CentralAudioBuffer.h"
#include "Compressor.h"
#include "DecoderGlobals.h"
#include "EncodedAudioStream.h"
#include "HTTPClient.h"
#include "PortAudioSink.h"

#include <BellDSP.h>
#include <BellLogger.h>

std::shared_ptr<bell::CentralAudioBuffer> audioBuffer;
std::atomic<bool> isPaused = false;

class AudioPlayer : bell::Task {
 public:
  std::unique_ptr<PortAudioSink> audioSink;
  std::unique_ptr<bell::BellDSP> dsp;

  AudioPlayer() : bell::Task("player", 1024, 0, 0) {
    this->audioSink = std::make_unique<PortAudioSink>();
    this->audioSink->setParams(44100, 2, 16);
    this->dsp = std::make_unique<bell::BellDSP>(audioBuffer);
    startTask();
  }

  void runTask() override {
    while (true) {
      if (audioBuffer->hasAtLeast(64) || isPaused) {
        auto chunk = audioBuffer->readChunk();

        if (chunk != nullptr && chunk->pcmSize > 0) {
          // this->dsp->process(chunk->pcmData, chunk->pcmSize, 2, 44100,
          //                    bell::BitWidth::BW_16);

          this->audioSink->feedPCMFrames(chunk->pcmData, chunk->pcmSize);
        }
      }
    }
  }
};

int main() {
  bell::setDefaultLogger();
  bell::createDecoders();
    size_t size = sizeof(void*);
  audioBuffer = std::make_shared<bell::CentralAudioBuffer>(512);
  auto task = AudioPlayer();

  auto url = "http://193.222.135.71/378";
  // std::ifstream file("aactest.aac", std::ios::binary);

  auto req = bell::HTTPClient::get(url);
  auto container = AudioContainers::guessAudioContainer(req->stream());
  auto codec = AudioCodecs::getCodec(container.get());

  uint32_t dataLen;
  while (true) {
    uint8_t* data = codec->decode(container.get(), dataLen);

    if (!data) {
      std::cout << "data invalid" << std::endl;
      continue;
    }

    size_t toWrite = dataLen;
    while (toWrite > 0) {
      toWrite -= audioBuffer->writePCM(data + dataLen - toWrite, toWrite, 0);
    }

    // std::cout << dataLen << std::endl;
  }

  // return 0;

  // std::vector<uint8_t> frameData(1024 * 10);
  // /*
  // while (true) {
  //   size_t bytes =audioStream->decodeFrame(frameData.data());
  //   std::cout << bytes <<std::endl;

  //   size_t toWrite = bytes;

  //   if (!isPaused) {
  //     while (toWrite > 0) {
  //       toWrite -= audioBuffer->writePCM(frameData.data() + bytes - toWrite,
  //                                        toWrite, 0);
  //     }
  //   }
  // }*/
  return 0;
}
#include "PortAudioSink.h"

PortAudioSink::PortAudioSink() {
  Pa_Initialize();
  this->setParams(44100, 2, 16);
}

bool PortAudioSink::setParams(uint32_t sampleRate, uint8_t channelCount,
                              uint8_t bitDepth) {
  if (stream) {
    Pa_StopStream(stream);
  }
  PaStreamParameters outputParameters;
  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    printf("PortAudio: Default audio device not found!\n");
    // exit(0);
  }
  printf("PortAudio: Default audio device not found!\n");

  outputParameters.channelCount = channelCount;
  switch (bitDepth) {
    case 32:
      outputParameters.sampleFormat = paInt32;
      break;
    case 24:
      outputParameters.sampleFormat = paInt24;
      break;
    case 16:
      outputParameters.sampleFormat = paInt16;
      break;
    case 8:
      outputParameters.sampleFormat = paInt8;
      break;
    default:
      outputParameters.sampleFormat = paInt16;
      break;
  }
  outputParameters.suggestedLatency = 0.050;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  PaError err = Pa_OpenStream(&stream, NULL, &outputParameters, sampleRate,
                              4096 / (channelCount * bitDepth / 8), paClipOff,
                              NULL,  // blocking api
                              NULL);
  Pa_StartStream(stream);
  return !err;
}

PortAudioSink::~PortAudioSink() {
  Pa_StopStream(stream);
  Pa_Terminate();
}

void PortAudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
  Pa_WriteStream(stream, buffer, bytes / 4);
}

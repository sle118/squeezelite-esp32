#include "NamedPipeAudioSink.h"

#include <stdio.h>  // for printf

NamedPipeAudioSink::NamedPipeAudioSink() {
  printf("Start\n");
  this->namedPipeFile = std::ofstream("outputFifo", std::ios::binary);
  printf("stop\n");
}

NamedPipeAudioSink::~NamedPipeAudioSink() {
  this->namedPipeFile.close();
}

void NamedPipeAudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
  // Write the actual data
  this->namedPipeFile.write((char*)buffer, (long)bytes);
  this->namedPipeFile.flush();
}

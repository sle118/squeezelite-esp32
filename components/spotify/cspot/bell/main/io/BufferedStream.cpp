#include "BufferedStream.h"

#include <stdlib.h>     // for free, malloc
#include <algorithm>    // for min
#include <cstdint>      // for uint32_t
#include <cstring>      // for memcpy
#include <type_traits>  // for remove_extent_t

BufferedStream::BufferedStream(const std::string& taskName, uint32_t bufferSize,
                               uint32_t readThreshold, uint32_t readSize,
                               uint32_t readyThreshold,
                               uint32_t notReadyThreshold, bool waitForReady)
    : bell::Task(taskName, 4096, 5, 0) {
  this->bufferSize = bufferSize;
  this->readAt = bufferSize - readThreshold;
  this->readSize = readSize;
  this->readyThreshold = readyThreshold;
  this->notReadyThreshold = notReadyThreshold;
  this->waitForReady = waitForReady;
  this->buf = static_cast<uint8_t*>(malloc(bufferSize));
  this->bufEnd = buf + bufferSize;
  reset();
}

BufferedStream::~BufferedStream() {
  this->close();
  free(buf);
}

void BufferedStream::close() {
  this->terminate = true;
  this->readSem.give();  // force a read operation
  const std::lock_guard lock(runningMutex);
  if (this->source)
    this->source->close();
  this->source = nullptr;
}

void BufferedStream::reset() {
  this->bufReadPtr = this->buf;
  this->bufWritePtr = this->buf;
  this->readTotal = 0;
  this->bufferTotal = 0;
  this->readAvailable = 0;
  this->terminate = false;
}

bool BufferedStream::open(const std::shared_ptr<bell::ByteStream>& stream) {
  if (this->running)
    this->close();
  reset();
  this->source = stream;
  startTask();
  return source.get();
}

bool BufferedStream::open(const StreamReader& newReader,
                          uint32_t initialOffset) {
  if (this->running)
    this->close();
  reset();
  this->reader = newReader;
  this->bufferTotal = initialOffset;
  startTask();
  return source.get();
}

bool BufferedStream::isReady() const {
  return readAvailable >= readyThreshold;
}

bool BufferedStream::isNotReady() const {
  return readAvailable < notReadyThreshold;
}

size_t BufferedStream::skip(size_t len) {
  return read(nullptr, len);
}

size_t BufferedStream::position() {
  return readTotal;
}

size_t BufferedStream::size() {
  return source->size();
}

uint32_t BufferedStream::lengthBetween(uint8_t* me, uint8_t* other) {
  const std::lock_guard lock(readMutex);
  if (other <= me) {
    // buf .... other ...... me ........ bufEnd
    // buf .... me/other ........ bufEnd
    return bufEnd - me;
  } else {
    // buf ........ me ........ other .... bufEnd
    return other - me;
  }
}

size_t BufferedStream::read(uint8_t* dst, size_t len) {
  if (waitForReady && isNotReady()) {
    while ((source || reader) && !isReady()) {
    }  // end waiting after termination
  }
  if (!running && !readAvailable) {
    reset();
    return 0;
  }
  uint32_t read = 0;
  uint32_t toReadTotal =
      std::min(readAvailable.load(), static_cast<uint32_t>(len));
  while (toReadTotal > 0) {
    uint32_t toRead =
        std::min(toReadTotal, lengthBetween(bufReadPtr, bufWritePtr));
    if (dst) {
      memcpy(dst, bufReadPtr, toRead);
      dst += toRead;
    }
    readAvailable -= toRead;
    bufReadPtr += toRead;
    if (bufReadPtr >= bufEnd)
      bufReadPtr = buf;
    toReadTotal -= toRead;
    read += toRead;
    readTotal += toRead;
  }
  this->readSem.give();
  return read;
}

void BufferedStream::runTask() {
  const std::lock_guard lock(runningMutex);
  running = true;
  if (!source && reader) {
    // get the initial request on the task's thread
    source = reader(this->bufferTotal);
  }
  while (!terminate) {
    if (!source)
      break;
    if (isReady()) {
      // buffer ready, wait for any read operations
      this->readSem.wait();
    }
    if (terminate)
      break;
    if (readAvailable > readAt)
      continue;
    // here, the buffer needs re-filling
    uint32_t len;
    bool wasReady = isReady();
    do {
      uint32_t toRead =
          std::min(readSize, lengthBetween(bufWritePtr, bufReadPtr));
      if (!source) {
        len = 0;
        break;
      }
      len = source->read(bufWritePtr, toRead);
      readAvailable += len;
      bufferTotal += len;
      bufWritePtr += len;
      if (bufWritePtr >= bufEnd)  // TODO is == enough here?
        bufWritePtr = buf;
    } while (
        len &&
        readSize <
            bufferSize -
                readAvailable);  // loop until there's no more free space in the buffer
    if (!len && reader)
      source = reader(bufferTotal);
    else if (!len)
      terminate = true;
    // signal that buffer is ready for reading
    if (!wasReady && isReady()) {
      this->readySem.give();
    }
  }
  source = nullptr;
  reader = nullptr;
  running = false;
}

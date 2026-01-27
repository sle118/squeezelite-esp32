#pragma once

#include <stddef.h>    // for size_t
#include <stdint.h>    // for uint32_t, uint8_t
#include <atomic>      // for atomic
#include <functional>  // for function
#include <memory>      // for shared_ptr
#include <mutex>       // for mutex
#include <string>      // for string

#include "BellTask.h"          // for Task
#include "ByteStream.h"        // for ByteStream
#include "WrappedSemaphore.h"  // for WrappedSemaphore

/**
 * This class implements a wrapper around an arbitrary bell::ByteStream,
 * providing a circular reading buffer with configurable thresholds.
 *
 * The BufferedStream runs a bell::Task when it's started, so the caller can
 * access the buffer's data asynchronously, whenever needed. The buffer is refilled
 * automatically from source stream.
 *
 * The class implements bell::ByteStream's methods, although for proper functioning,
 * the caller code should be modified to check isReady() and isNotReady() flags.
 *
 * If the actual reading code can't be modified, waitForReady allows to wait for buffer readiness
 * during reading. Keep in mind that using the semaphore is probably more resource effective.
 *
 * The source stream (passed to open() or returned by the reader) should implement the read()
 * method correctly, such as that 0 is returned if, and only if the stream ends.
 */
class BufferedStream : public bell::ByteStream, bell::Task {
 public:
  typedef std::shared_ptr<bell::ByteStream> StreamPtr;
  typedef std::function<StreamPtr(uint32_t rangeStart)> StreamReader;

 public:
  /**
	 * @param taskName name to use for the reading task
	 * @param bufferSize total size of the reading buffer
	 * @param readThreshold how much can be read before refilling the buffer
	 * @param readSize amount of bytes to read from the source each time
	 * @param readyThreshold minimum amount of available bytes to report isReady()
	 * @param notReadyThreshold maximum amount of available bytes to report isNotReady()
	 * @param waitForReady whether to wait for the buffer to be ready during reading
	 * @param endWithSource whether to end the streaming as soon as source returns 0 from read()
	 */
  BufferedStream(const std::string& taskName, uint32_t bufferSize,
                 uint32_t readThreshold, uint32_t readSize,
                 uint32_t readyThreshold, uint32_t notReadyThreshold,
                 bool waitForReady = false);
  ~BufferedStream() override;
  bool open(const StreamPtr& stream);
  bool open(const StreamReader& newReader, uint32_t initialOffset = 0);
  void close() override;

  // inherited methods
 public:
  /**
	 * Read len bytes from the buffer to dst. If waitForReady is enabled
	 * and readAvailable is lower than notReadyThreshold, the function
	 * will block until readyThreshold bytes is available.
	 *
	 * @returns number of bytes copied to dst (might be lower than len,
	 * if the buffer does not contain len bytes available), or 0 if the source
	 * stream is already closed and there is no reader attached.
	 */
  size_t read(uint8_t* dst, size_t len) override;
  size_t skip(size_t len) override;
  size_t position() override;
  size_t size() override;

  // stream status
 public:
  /**
	 * Total amount of bytes served to read().
	 */
  uint32_t readTotal;
  /**
	 * Total amount of bytes read from source.
	 */
  uint32_t bufferTotal;
  /**
	 * Amount of bytes available to read from the buffer.
	 */
  std::atomic<uint32_t> readAvailable;
  /**
	 * Whether the caller should start reading the data. This indicates that a safe
	 * amount (determined by readyThreshold) of data is available in the buffer.
	 */
  bool isReady() const;
  /**
	 * Whether the caller should stop reading the data. This indicates that the amount of data
	 * available for reading is decreasing to a non-safe value, as data is being read
	 * faster than it can be buffered.
	 */
  bool isNotReady() const;
  /**
	 * Semaphore that is given when the buffer becomes ready (isReady() == true). Caller can
	 * wait for the semaphore instead of continuously querying isReady().
	 */
  bell::WrappedSemaphore readySem;

 private:
  std::mutex runningMutex;
  bool running = false;
  bool terminate = false;
  bell::WrappedSemaphore
      readSem;  // signal to start writing to buffer after reading from it
  std::mutex
      readMutex;  // mutex for locking read operations during writing, and vice versa
  uint32_t bufferSize;
  uint32_t readAt;
  uint32_t readSize;
  uint32_t readyThreshold;
  uint32_t notReadyThreshold;
  bool waitForReady;
  uint8_t* buf;
  uint8_t* bufEnd;
  uint8_t* bufReadPtr;
  uint8_t* bufWritePtr;
  StreamPtr source;
  StreamReader reader;
  void runTask() override;
  void reset();
  uint32_t lengthBetween(uint8_t* me, uint8_t* other);
};

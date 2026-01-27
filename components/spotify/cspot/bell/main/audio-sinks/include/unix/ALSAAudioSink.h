#pragma once

#include <BellTask.h>
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>
#include "AudioSink.h"

#define PCM_DEVICE "default"

template <typename T, int SIZE>
class RingbufferPointer {
  typedef std::unique_ptr<T> TPointer;

 public:
  explicit RingbufferPointer() {
    // create objects
    for (int i = 0; i < SIZE; i++) {
      buf_[i] = std::make_unique<T>();
    }
  }

  bool push(TPointer& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (full())
      return false;

    std::swap(buf_[head_], item);

    if (full_)
      tail_ = (tail_ + 1) % max_size_;

    head_ = (head_ + 1) % max_size_;
    full_ = head_ == tail_;

    return true;
  }

  bool pop(TPointer& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (empty())
      return false;

    std::swap(buf_[tail_], item);

    full_ = false;
    tail_ = (tail_ + 1) % max_size_;

    return true;
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = tail_;
    full_ = false;
  }

  bool empty() const { return (!full_ && (head_ == tail_)); }

  bool full() const { return full_; }

  int capacity() const { return max_size_; }

  int size() const {
    int size = max_size_;

    if (!full_) {
      if (head_ >= tail_)
        size = head_ - tail_;
      else
        size = max_size_ + head_ - tail_;
    }

    return size;
  }

 private:
  TPointer buf_[SIZE];

  std::mutex mutex_;
  int head_ = 0;
  int tail_ = 0;
  const int max_size_ = SIZE;
  bool full_ = 0;
};

class ALSAAudioSink : public AudioSink, public bell::Task {
 public:
  ALSAAudioSink();
  ~ALSAAudioSink();
  void feedPCMFrames(const uint8_t* buffer, size_t bytes);
  void runTask();

 private:
  RingbufferPointer<std::vector<uint8_t>, 3> ringbuffer;
  unsigned int pcm;
  snd_pcm_t* pcm_handle;
  snd_pcm_hw_params_t* params;
  snd_pcm_uframes_t frames;
  int buff_size;
  std::vector<uint8_t> buff;
};

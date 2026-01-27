#include "BufferedAudioSink.h"

#include "driver/i2s.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

RingbufHandle_t dataBuffer;

static void i2sFeed(void* pvParameters) {
  while (true) {
    size_t itemSize;
    char* item = (char*)xRingbufferReceiveUpTo(dataBuffer, &itemSize,
                                               portMAX_DELAY, 512);
    if (item != NULL) {
      size_t written = 0;
      while (written < itemSize) {
        i2s_write((i2s_port_t)0, item, itemSize, &written, portMAX_DELAY);
      }
      vRingbufferReturnItem(dataBuffer, (void*)item);
    }
  }
}

void BufferedAudioSink::startI2sFeed(size_t buf_size) {
  dataBuffer = xRingbufferCreate(buf_size, RINGBUF_TYPE_BYTEBUF);
  xTaskCreatePinnedToCore(&i2sFeed, "i2sFeed", 4096, NULL, 10, NULL,
                          tskNO_AFFINITY);
}

void BufferedAudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
  feedPCMFramesInternal(buffer, bytes);
}

void BufferedAudioSink::feedPCMFramesInternal(const void* pvItem,
                                              size_t xItemSize) {
  xRingbufferSend(dataBuffer, pvItem, xItemSize, portMAX_DELAY);
}

bool BufferedAudioSink::setParams(uint32_t sampleRate, uint8_t channelCount,
                                  uint8_t bitDepth) {
  // TODO override this for sinks with custom mclk
  i2s_set_clk((i2s_port_t)0, sampleRate, (i2s_bits_per_sample_t)bitDepth,
              (i2s_channel_t)channelCount);
  return true;
}
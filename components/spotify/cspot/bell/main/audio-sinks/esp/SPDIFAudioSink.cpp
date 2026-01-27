#include "SPDIFAudioSink.h"

#include "driver/i2s.h"

// See http://www.hardwarebook.info/S/PDIF for more info on this protocol
// Conversion table to biphase code mark (LSB first, ending in 1)
static const uint16_t bmc_convert[256] = {
    0x3333, 0xb333, 0xd333, 0x5333, 0xcb33, 0x4b33, 0x2b33, 0xab33, 0xcd33,
    0x4d33, 0x2d33, 0xad33, 0x3533, 0xb533, 0xd533, 0x5533, 0xccb3, 0x4cb3,
    0x2cb3, 0xacb3, 0x34b3, 0xb4b3, 0xd4b3, 0x54b3, 0x32b3, 0xb2b3, 0xd2b3,
    0x52b3, 0xcab3, 0x4ab3, 0x2ab3, 0xaab3, 0xccd3, 0x4cd3, 0x2cd3, 0xacd3,
    0x34d3, 0xb4d3, 0xd4d3, 0x54d3, 0x32d3, 0xb2d3, 0xd2d3, 0x52d3, 0xcad3,
    0x4ad3, 0x2ad3, 0xaad3, 0x3353, 0xb353, 0xd353, 0x5353, 0xcb53, 0x4b53,
    0x2b53, 0xab53, 0xcd53, 0x4d53, 0x2d53, 0xad53, 0x3553, 0xb553, 0xd553,
    0x5553, 0xcccb, 0x4ccb, 0x2ccb, 0xaccb, 0x34cb, 0xb4cb, 0xd4cb, 0x54cb,
    0x32cb, 0xb2cb, 0xd2cb, 0x52cb, 0xcacb, 0x4acb, 0x2acb, 0xaacb, 0x334b,
    0xb34b, 0xd34b, 0x534b, 0xcb4b, 0x4b4b, 0x2b4b, 0xab4b, 0xcd4b, 0x4d4b,
    0x2d4b, 0xad4b, 0x354b, 0xb54b, 0xd54b, 0x554b, 0x332b, 0xb32b, 0xd32b,
    0x532b, 0xcb2b, 0x4b2b, 0x2b2b, 0xab2b, 0xcd2b, 0x4d2b, 0x2d2b, 0xad2b,
    0x352b, 0xb52b, 0xd52b, 0x552b, 0xccab, 0x4cab, 0x2cab, 0xacab, 0x34ab,
    0xb4ab, 0xd4ab, 0x54ab, 0x32ab, 0xb2ab, 0xd2ab, 0x52ab, 0xcaab, 0x4aab,
    0x2aab, 0xaaab, 0xcccd, 0x4ccd, 0x2ccd, 0xaccd, 0x34cd, 0xb4cd, 0xd4cd,
    0x54cd, 0x32cd, 0xb2cd, 0xd2cd, 0x52cd, 0xcacd, 0x4acd, 0x2acd, 0xaacd,
    0x334d, 0xb34d, 0xd34d, 0x534d, 0xcb4d, 0x4b4d, 0x2b4d, 0xab4d, 0xcd4d,
    0x4d4d, 0x2d4d, 0xad4d, 0x354d, 0xb54d, 0xd54d, 0x554d, 0x332d, 0xb32d,
    0xd32d, 0x532d, 0xcb2d, 0x4b2d, 0x2b2d, 0xab2d, 0xcd2d, 0x4d2d, 0x2d2d,
    0xad2d, 0x352d, 0xb52d, 0xd52d, 0x552d, 0xccad, 0x4cad, 0x2cad, 0xacad,
    0x34ad, 0xb4ad, 0xd4ad, 0x54ad, 0x32ad, 0xb2ad, 0xd2ad, 0x52ad, 0xcaad,
    0x4aad, 0x2aad, 0xaaad, 0x3335, 0xb335, 0xd335, 0x5335, 0xcb35, 0x4b35,
    0x2b35, 0xab35, 0xcd35, 0x4d35, 0x2d35, 0xad35, 0x3535, 0xb535, 0xd535,
    0x5535, 0xccb5, 0x4cb5, 0x2cb5, 0xacb5, 0x34b5, 0xb4b5, 0xd4b5, 0x54b5,
    0x32b5, 0xb2b5, 0xd2b5, 0x52b5, 0xcab5, 0x4ab5, 0x2ab5, 0xaab5, 0xccd5,
    0x4cd5, 0x2cd5, 0xacd5, 0x34d5, 0xb4d5, 0xd4d5, 0x54d5, 0x32d5, 0xb2d5,
    0xd2d5, 0x52d5, 0xcad5, 0x4ad5, 0x2ad5, 0xaad5, 0x3355, 0xb355, 0xd355,
    0x5355, 0xcb55, 0x4b55, 0x2b55, 0xab55, 0xcd55, 0x4d55, 0x2d55, 0xad55,
    0x3555, 0xb555, 0xd555, 0x5555,
};

#define I2S_BUG_MAGIC (26 * 1000 * 1000)  // magic number for avoiding I2S bug
#define BITS_PER_SUBFRAME 64
#define FRAMES_PER_BLOCK 192
#define SPDIF_BUF_SIZE (BITS_PER_SUBFRAME / 8 * 2 * FRAMES_PER_BLOCK)
#define SPDIF_BUF_ARRAY_SIZE (SPDIF_BUF_SIZE / sizeof(uint32_t))

#define BMC_B 0x33173333  // block start
#define BMC_M 0x331d3333  // left ch
#define BMC_W 0x331b3333  // right ch
#define BMC_MW_DIF (BMC_M ^ BMC_W)

static uint32_t spdif_buf[SPDIF_BUF_ARRAY_SIZE];
static uint32_t* spdif_ptr;

static void spdif_buf_init(void) {
  // first bllock has W preamble
  spdif_buf[0] = BMC_B;

  // all other blocks are alternating M, then W preamble
  uint32_t bmc_mw = BMC_M;
  for (int i = 2; i < SPDIF_BUF_ARRAY_SIZE; i += 2) {
    spdif_buf[i] = bmc_mw ^= BMC_MW_DIF;
  }
}

SPDIFAudioSink::SPDIFAudioSink(uint8_t spdifPin) {
  // initialize S/PDIF buffer
  spdif_buf_init();
  spdif_ptr = spdif_buf;
  this->spdifPin = spdifPin;
  this->setParams(44100, 16, 2);
  startI2sFeed(SPDIF_BUF_SIZE * 16);
}

bool SPDIFAudioSink::setParams(uint32_t sampleRate, uint8_t channelCount,
                               uint8_t bitDepth) {
  if (bitDepth != 16 ||
      channelCount != 2)  // TODO support mono playback and different bit widths
    return false;
  int sample_rate = (int)sampleRate * 2;
  int bclk = sample_rate * 64 * 2;
  int mclk = (I2S_BUG_MAGIC / bclk) * bclk;

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    .sample_rate = (uint32_t)sample_rate,
#else
    .sample_rate = (int)sample_rate,
#endif
    .bits_per_sample = (i2s_bits_per_sample_t)(bitDepth * 2),
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = mclk,  // avoiding I2S bug
  };
  i2s_pin_config_t pin_config = {
      .bck_io_num = -1,
      .ws_io_num = -1,
      .data_out_num = spdifPin,
      .data_in_num = -1,
  };
  i2s_driver_uninstall((i2s_port_t)0);
  int err = i2s_driver_install((i2s_port_t)0, &i2s_config, 0, nullptr);
  i2s_set_pin((i2s_port_t)0, &pin_config);
  return !err;
}

SPDIFAudioSink::~SPDIFAudioSink() {
  i2s_driver_uninstall((i2s_port_t)0);
}

int num_frames = 0;

void SPDIFAudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
  for (int i = 0; i < bytes; i += 2) {
    /**
         * What is this, and why does it work?
         *
         * Rather than assemble all S/PDIF frames from scratch we want to do the
         * minimum amount of work possible. To that extent, we fix the final four
         * bits (VUCP) to be all-zero prior to BMC encoding (= valid, no subcode
         * or channel-status bits set, even parity), and zero the lowest 8 sample
         * bits (prior to BMC encoding). This is all done in spdif_buf_init(),
         * aligning at word boundaries and setting alternating preambles as well
         * as encoding 8 bits of zeros as 0x33, leaving the final bit high.
         *
         * We must therefore BMC encode our 16 bit PCM data in such a way that:
         *  - the first (least significant) bit is 0 (to fit with 0x33 zeros)
         *  - the final bit is 1 (so as to fit with the following 0x33 VUCP bits)
         *  - the result has even parity
         *
         * As biphase mark code retains parity (0 encodes as two 1s or two 0s),
         * this is evidently not possible without loss of data, as the input PCM
         * data isn't already even parity. We can use the first (least significant)
         * bit as parity bit to achieve our desired encoding.
         * 
         * The bmc_convert table converts the lower and upper 8 bit of our PCM
         * frames into 16 bit biphase mark code patterns with the first two bits
         * encoding the LSB and the final bit always high. We combine both 16bit
         * patterns into a 32 bit encoding of our original input data by shifting
         * the first (lower) 16 bit into position, then sign-extending the second
         * (higher) 16bit pattern. If that pattern started with a 1, the resulting
         * 32 bit pattern will now contain 1s in the first 16 bits.
         *
         * Keep in mind that the shifted value in the first (lower) 16 bits always
         * ends in a 1 bit, so the entire pattern must be flipped in case the
         * second (higher) 16 bit pattern starts with a 1 bit. XORing the sign-
         * extended component to the first one achieves exactly that.
         *
         * Finally, we zero out the very first bit of the resulting value. This
         * may change the lowest bit of our encoded value, but ensures that our
         * newly encoded bits form a valid BMC pattern with the already zeroed out
         * lower 8 bits in the pattern set up in spdif_buf_init().
         *
         * Further, this also happens to ensure even parity:
         * All entries in the BMC table end in a 1, so an all-zero pattern would
         * end (after encoding an even number of bits) in two 0 bits. Setting any
         * bit will cause the BMC-encoded pattern to flip its first (lowest) bit,
         * meaning we can use that bit to infer parity. Setting it to zero flips
         * the first (lowest) bit such that we always have even parity.
         *
         * I did not come up with this, all credit goes to
         * github.com/amedes/esp_a2dp_sink_spdif
         */
    uint32_t lo = ((uint32_t)(bmc_convert[buffer[i]]) << 16);
    uint32_t hi = (uint32_t)((int16_t)bmc_convert[buffer[i + 1]]);

    *(spdif_ptr + 1) = ((lo ^ hi) << 1) >> 1;

    spdif_ptr += 2;  // advance to next audio data

    if (spdif_ptr >= &spdif_buf[SPDIF_BUF_ARRAY_SIZE]) {
      feedPCMFramesInternal(spdif_buf, sizeof(spdif_buf));
      spdif_ptr = spdif_buf;
    }
  }
}
#ifndef __ESCODEC_COMMON_H__
#define __ESCODEC_COMMON_H__

typedef enum BitsLength {
  BIT_LENGTH_MIN = -1,
  BIT_LENGTH_16BITS = 0x03,
  BIT_LENGTH_18BITS = 0x02,
  BIT_LENGTH_20BITS = 0x01,
  BIT_LENGTH_24BITS = 0x00,
  BIT_LENGTH_32BITS = 0x04,
  BIT_LENGTH_MAX,
} BitsLength;

typedef enum {
  SAMPLE_RATE_MIN = -1,
  SAMPLE_RATE_16K,
  SAMPLE_RATE_32K,
  SAMPLE_RATE_44_1K,
  SAMPLE_RATE_MAX,
} SampleRate;

typedef enum {
  MclkDiv_MIN = -1,
  MclkDiv_1 = 1,
  MclkDiv_2 = 2,
  MclkDiv_3 = 3,
  MclkDiv_4 = 4,
  MclkDiv_6 = 5,
  MclkDiv_8 = 6,
  MclkDiv_9 = 7,
  MclkDiv_11 = 8,
  MclkDiv_12 = 9,
  MclkDiv_16 = 10,
  MclkDiv_18 = 11,
  MclkDiv_22 = 12,
  MclkDiv_24 = 13,
  MclkDiv_33 = 14,
  MclkDiv_36 = 15,
  MclkDiv_44 = 16,
  MclkDiv_48 = 17,
  MclkDiv_66 = 18,
  MclkDiv_72 = 19,
  MclkDiv_5 = 20,
  MclkDiv_10 = 21,
  MclkDiv_15 = 22,
  MclkDiv_17 = 23,
  MclkDiv_20 = 24,
  MclkDiv_25 = 25,
  MclkDiv_30 = 26,
  MclkDiv_32 = 27,
  MclkDiv_34 = 28,
  MclkDiv_7 = 29,
  MclkDiv_13 = 30,
  MclkDiv_14 = 31,
  MclkDiv_MAX,
} SclkDiv;

typedef enum {
  LclkDiv_MIN = -1,
  LclkDiv_128 = 0,
  LclkDiv_192 = 1,
  LclkDiv_256 = 2,
  LclkDiv_384 = 3,
  LclkDiv_512 = 4,
  LclkDiv_576 = 5,
  LclkDiv_768 = 6,
  LclkDiv_1024 = 7,
  LclkDiv_1152 = 8,
  LclkDiv_1408 = 9,
  LclkDiv_1536 = 10,
  LclkDiv_2112 = 11,
  LclkDiv_2304 = 12,

  LclkDiv_125 = 16,
  LclkDiv_136 = 17,
  LclkDiv_250 = 18,
  LclkDiv_272 = 19,
  LclkDiv_375 = 20,
  LclkDiv_500 = 21,
  LclkDiv_544 = 22,
  LclkDiv_750 = 23,
  LclkDiv_1000 = 24,
  LclkDiv_1088 = 25,
  LclkDiv_1496 = 26,
  LclkDiv_1500 = 27,
  LclkDiv_MAX,
} LclkDiv;

typedef enum {
  ADC_INPUT_MIN = -1,
  ADC_INPUT_LINPUT1_RINPUT1 = 0x00,
  ADC_INPUT_MIC1 = 0x05,
  ADC_INPUT_MIC2 = 0x06,
  ADC_INPUT_LINPUT2_RINPUT2 = 0x50,
  ADC_INPUT_DIFFERENCE = 0xf0,
  ADC_INPUT_MAX,
} AdcInput;

typedef enum {
  DAC_OUTPUT_MIN = -1,
  DAC_OUTPUT_LOUT1 = 0x04,
  DAC_OUTPUT_LOUT2 = 0x08,
  DAC_OUTPUT_SPK = 0x09,
  DAC_OUTPUT_ROUT1 = 0x10,
  DAC_OUTPUT_ROUT2 = 0x20,
  DAC_OUTPUT_ALL = 0x3c,
  DAC_OUTPUT_MAX,
} DacOutput;

typedef enum {
  D2SE_PGA_GAIN_MIN = -1,
  D2SE_PGA_GAIN_DIS = 0,
  D2SE_PGA_GAIN_EN = 1,
  D2SE_PGA_GAIN_MAX = 2,
} D2SEPGA;

typedef enum {
  MIC_GAIN_MIN = -1,
  MIC_GAIN_0DB = 0,
  MIC_GAIN_3DB = 3,
  MIC_GAIN_6DB = 6,
  MIC_GAIN_9DB = 9,
  MIC_GAIN_12DB = 12,
  MIC_GAIN_15DB = 15,
  MIC_GAIN_18DB = 18,
  MIC_GAIN_21DB = 21,
  MIC_GAIN_24DB = 24,
#if defined CONFIG_CODEC_CHIP_IS_ES8311
  MIC_GAIN_30DB = 30,
  MIC_GAIN_36DB = 36,
  MIC_GAIN_42DB = 42,
#endif
  MIC_GAIN_MAX,
} MicGain;

typedef enum {
  ES_MODULE_MIN = -1,
  ES_MODULE_ADC = 0x01,
  ES_MODULE_DAC = 0x02,
  ES_MODULE_ADC_DAC = 0x03,
  ES_MODULE_LINE = 0x04,
  ES_MODULE_MAX
} ESCodecModule;

typedef enum {
  ES_MODE_MIN = -1,
  ES_MODE_SLAVE = 0x00,
  ES_MODE_MASTER = 0x01,
  ES_MODE_MAX,
} ESCodecMode;

typedef enum {
  ES_ = -1,
  ES_I2S_NORMAL = 0,
  ES_I2S_LEFT = 1,
  ES_I2S_RIGHT = 2,
  ES_I2S_DSP = 3,
  ES_I2S_MAX
} ESCodecI2SFmt;

typedef struct {
  SclkDiv sclkDiv;
  LclkDiv lclkDiv;
} ESCodecI2sClock;

#endif  //__ESCODEC_COMMON_H__

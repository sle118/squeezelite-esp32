#include "VorbisDecoder.h"

#include <stdlib.h>  // for free, malloc

#include "CodecType.h"     // for bell
#include "config_types.h"  // for ogg_int16_t

namespace bell {
class AudioContainer;
}  // namespace bell

using namespace bell;

extern "C" {
extern vorbis_dsp_state* vorbis_dsp_create(vorbis_info* vi);
extern void vorbis_dsp_destroy(vorbis_dsp_state* v);
extern int vorbis_dsp_restart(vorbis_dsp_state* v);
extern int vorbis_dsp_headerin(vorbis_info* vi, vorbis_comment* vc,
                               ogg_packet* op);
extern int vorbis_dsp_synthesis(vorbis_dsp_state* vd, ogg_packet* op,
                                int decodep);
extern int vorbis_dsp_pcmout(vorbis_dsp_state* v, ogg_int16_t* pcm,
                             int samples);
extern int vorbis_dsp_read(vorbis_dsp_state* v, int samples);
}

#define VORBIS_BUF_SAMPLES 1024
#define VORBIS_BUF_CHANNELS 2

VorbisDecoder::VorbisDecoder() {
  vi = new vorbis_info;
  vorbis_info_init(vi);
  vc = new vorbis_comment;
  vorbis_comment_init(vc);

  op.packet = new ogg_reference;
  op.packet->buffer = new ogg_buffer;
  op.packet->buffer->refcount = 0;
  op.packet->buffer->ptr.owner = nullptr;
  op.packet->buffer->ptr.next = nullptr;
  op.packet->begin = 0;
  op.packet->next = nullptr;
  op.granulepos = -1;
  op.packetno = 10;

  pcmData = (int16_t*)malloc(VORBIS_BUF_SAMPLES * VORBIS_BUF_CHANNELS *
                             sizeof(uint16_t));
}

VorbisDecoder::~VorbisDecoder() {
  vorbis_info_clear(vi);
  vorbis_comment_clear(vc);
  if (vd)
    vorbis_dsp_destroy(vd);
  vd = nullptr;
  free(pcmData);
}

bool VorbisDecoder::setup(AudioContainer* container) {
  /*
  uint32_t setupLen;
  uint8_t* setup = container->getSetupData(setupLen, AudioCodec::VORBIS);
  if (!setup)
    return false;
  op.b_o_s = true;                    // mark this page as beginning of stream
  uint32_t bytesLeft = setupLen - 1;  // minus header count length (8 bit)
  std::vector<uint32_t> headers(setup[0]);
  for (uint8_t i = 0; i < setup[0]; i++) {
    uint8_t* sizeByte = (uint8_t*)setup + 1 + i;
    headers[i] = 0;
    while (*sizeByte == 255) {
      headers[i] += *(sizeByte++);
      bytesLeft--;
    }
    headers[i] += *sizeByte;
    bytesLeft--;
  }
  // parse all headers from the setup data
  for (const auto& headerSize : headers) {
    setPacket(setup + setupLen - bytesLeft, headerSize);
    bytesLeft -= headerSize;
    lastErrno = vorbis_dsp_headerin(vi, vc, &op);
    if (lastErrno < 0) {
      bytesLeft = 0;
      break;
    }
  }
  // parse last header, not present in header table (seems to happen for MP4 containers)
  if (bytesLeft) {
    setPacket(setup + setupLen - bytesLeft, bytesLeft);
    lastErrno = vorbis_dsp_headerin(vi, vc, &op);
  }
  // disable BOS to allow reading audio data
  op.b_o_s = false;
  // set up the codec
  if (vd)
    vorbis_dsp_restart(vd);
  else
    vd = vorbis_dsp_create(vi);
  return !lastErrno;
  */
  return false;
}

bool VorbisDecoder::setup(uint32_t sampleRate, uint8_t channelCount,
                          uint8_t bitDepth) {
  // manual setup is not allowed
  return false;
}

uint8_t* VorbisDecoder::decode(uint8_t* inData, uint32_t& inLen,
                               uint32_t& outLen) {
  if (!inData || !vi)
    return nullptr;
  setPacket(inData, inLen);
  // sources:
  //  - vorbisfile.c:556
  //  - vorbisfile.c:1557
  lastErrno = vorbis_dsp_synthesis(vd, &op, 1);
  if (lastErrno < 0)
    return nullptr;
  int samples = vorbis_dsp_pcmout(vd, pcmData, VORBIS_BUF_SAMPLES);
  outLen = samples;
  if (samples) {
    if (samples > 0) {
      vorbis_dsp_read(vd, samples);
      outLen = samples * 2 * vi->channels;
    }
  }
  inLen = 0;
  return (uint8_t*)pcmData;
}

void VorbisDecoder::setPacket(uint8_t* inData, uint32_t inLen) const {
  op.packet->buffer->data = static_cast<unsigned char*>(inData);
  op.packet->buffer->size = static_cast<long>(inLen);
  op.packet->length = static_cast<long>(inLen);
}

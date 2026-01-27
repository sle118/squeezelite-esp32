#include "ALSAAudioSink.h"

ALSAAudioSink::ALSAAudioSink() : Task("", 0, 0, 0) {
  /* Open the PCM device in playback mode */
  if (pcm = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0) <
            0) {
    printf("ERROR: Can't open \"%s\" PCM device. %s\n", PCM_DEVICE,
           snd_strerror(pcm));
  }

  /* Allocate parameters object and fill it with default values*/
  snd_pcm_hw_params_alloca(&params);

  snd_pcm_hw_params_any(pcm_handle, params);

  /* Set parameters */
  if (pcm = snd_pcm_hw_params_set_access(pcm_handle, params,
                                         SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

  if (pcm = snd_pcm_hw_params_set_format(pcm_handle, params,
                                         SND_PCM_FORMAT_S16_LE) < 0)
    printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));

  if (pcm = snd_pcm_hw_params_set_channels(pcm_handle, params, 2) < 0)
    printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));
  unsigned int rate = 44100;
  if (pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0) < 0)
    printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));
  unsigned int periodTime = 800;
  int dir = -1;
  snd_pcm_hw_params_set_period_time_near(pcm_handle, params, &periodTime, &dir);
  /* Write parameters */
  if (pcm = snd_pcm_hw_params(pcm_handle, params) < 0)
    printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(pcm));

  /* Resume information */
  printf("PCM name: '%s'\n", snd_pcm_name(pcm_handle));

  printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));
  unsigned int tmp;
  snd_pcm_hw_params_get_channels(params, &tmp);
  printf("channels: %i ", tmp);
  if (tmp == 1)
    printf("(mono)\n");
  else if (tmp == 2)
    printf("(stereo)\n");

  snd_pcm_hw_params_get_period_time(params, &tmp, NULL);
  printf("period_time = %d\n", tmp);
  snd_pcm_hw_params_get_period_size(params, &frames, 0);

  this->buff_size = frames * 2 * 2 /* 2 -> sample size */;
  printf("required buff_size: %d\n", buff_size);
  this->startTask();
}

ALSAAudioSink::~ALSAAudioSink() {
  snd_pcm_drain(pcm_handle);
  snd_pcm_close(pcm_handle);
}

void ALSAAudioSink::runTask() {
  std::unique_ptr<std::vector<uint8_t>> dataPtr;
  while (true) {
    if (!this->ringbuffer.pop(dataPtr)) {
      usleep(100);
      continue;
    }
    if (pcm = snd_pcm_writei(pcm_handle, dataPtr->data(), this->frames) ==
              -EPIPE) {

      snd_pcm_prepare(pcm_handle);
    } else if (pcm < 0) {
      printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
    }
  }
}

void ALSAAudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {

  buff.insert(buff.end(), buffer, buffer + bytes);
  while (buff.size() > this->buff_size) {
    auto ptr = std::make_unique<std::vector<uint8_t>>(
        this->buff.begin(), this->buff.begin() + this->buff_size);
    this->buff = std::vector<uint8_t>(this->buff.begin() + this->buff_size,
                                      this->buff.end());
    while (!this->ringbuffer.push(ptr)) {
      usleep(100);
    };
  }
}

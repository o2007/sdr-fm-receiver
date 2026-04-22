#ifndef AUDIO_GAIN_H
#define AUDIO_GAIN_H

#include <stdint.h>
#include <sys/types.h>

void scale_pcm_i16(uint8_t *buf, ssize_t n, int vol_pct);

#endif

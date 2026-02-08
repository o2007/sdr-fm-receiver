#ifndef WAV_WRITER_H
#define WAV_WRITER_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_bytes_written;
} WavWriter;

int wav_open(WavWriter *w, const char *path, uint32_t sample_rate,
             uint16_t channels, uint16_t bits_per_sample);
int wav_write_i16(WavWriter *w, const int16_t *samples, uint32_t n_samples_total);
int wav_close(WavWriter *w);

#endif

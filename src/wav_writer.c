#include "wav_writer.h"
#include <string.h>

static void write_u16_le(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    fwrite(b, 1, 2, f);
}
static void write_u32_le(FILE *f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v & 0xFF),
        (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 24) & 0xFF)
    };
    fwrite(b, 1, 4, f);
}

int wav_open(WavWriter *w, const char *path, uint32_t sample_rate,
             uint16_t channels, uint16_t bits_per_sample)
{
    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "wb");
    if (!w->f) return -1;

    w->sample_rate = sample_rate;
    w->channels = channels;
    w->bits_per_sample = bits_per_sample;
    w->data_bytes_written = 0;

    // RIFF header (we'll patch sizes on close)
    fwrite("RIFF", 1, 4, w->f);
    write_u32_le(w->f, 0);              // placeholder: RIFF chunk size
    fwrite("WAVE", 1, 4, w->f);

    // fmt chunk
    fwrite("fmt ", 1, 4, w->f);
    write_u32_le(w->f, 16);             // PCM fmt chunk size
    write_u16_le(w->f, 1);              // AudioFormat=1 PCM
    write_u16_le(w->f, channels);
    write_u32_le(w->f, sample_rate);

    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    uint32_t byte_rate = sample_rate * block_align;

    write_u32_le(w->f, byte_rate);
    write_u16_le(w->f, block_align);
    write_u16_le(w->f, bits_per_sample);

    // data chunk
    fwrite("data", 1, 4, w->f);
    write_u32_le(w->f, 0);              // placeholder: data chunk size

    return 0;
}

int wav_write_i16(WavWriter *w, const int16_t *samples, uint32_t n_samples_total)
{
    if (!w || !w->f) return -1;
    size_t wrote = fwrite(samples, sizeof(int16_t), n_samples_total, w->f);
    w->data_bytes_written += (uint32_t)(wrote * sizeof(int16_t));
    return (wrote == n_samples_total) ? 0 : -1;
}

int wav_close(WavWriter *w)
{
    if (!w || !w->f) return -1;

    // Patch sizes:
    // RIFF size = 4 ("WAVE") + (8+fmt) + (8+data)
    uint32_t riff_size = 4 + (8 + 16) + (8 + w->data_bytes_written);

    // data chunk size = data bytes
    uint32_t data_size = w->data_bytes_written;

    // RIFF size is at offset 4
    fseek(w->f, 4, SEEK_SET);
    write_u32_le(w->f, riff_size);

    // data size is at offset: 12 + 8 + 16 + 4 = 40
    fseek(w->f, 40, SEEK_SET);
    write_u32_le(w->f, data_size);

    fclose(w->f);
    w->f = NULL;
    return 0;
}

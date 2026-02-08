#ifndef FM_AUDIO_H
#define FM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    // Decimating FIR
    int M;              // decimation factor
    int ntaps;
    float *h;           // taps length ntaps
    float *xhist;       // ring buffer length ntaps
    int widx;           // ring write index
    int phase;          // counts samples since last output (0..M-1)

    // De-emphasis
    float deemph_y;
    float deemph_alpha;

    // Simple peak/limiter
    float peak;
    float peak_decay;
} FmAudio;

int fm_audio_init(FmAudio *a, float fs_in, int decim_M,
                  float lpf_cut_hz, int ntaps,
                  float deemph_tau_sec);
void fm_audio_free(FmAudio *a);

/**
 * Feed one discriminator sample at fs_in.
 * Returns 1 if an audio sample at fs_out is produced (stored in *y_out), else 0.
 */
int fm_audio_process(FmAudio *a, float x_in, float *y_out);

/**
 * Convert float [-1,1] to int16 with hard clip.
 */
int16_t fm_float_to_i16(float x);

#endif

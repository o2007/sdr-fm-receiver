#include "fm_audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void design_lpf_hamming(float *h, int ntaps, float fs, float fc)
{
    // Windowed-sinc low-pass, Hamming window, unity DC gain.
    // fc in Hz, fs in Hz.
    float norm = fc / fs; // 0..0.5
    int M = ntaps - 1;
    float sum = 0.0f;

    for (int n = 0; n < ntaps; n++) {
        float m = (float)n - (float)M / 2.0f;
        float w = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)n / (float)M);

        float x;
        if (fabsf(m) < 1e-9f) {
            x = 2.0f * norm;
        } else {
            x = sinf(2.0f * (float)M_PI * norm * m) / ((float)M_PI * m);
        }
        h[n] = x * w;
        sum += h[n];
    }

    // Normalize DC gain to 1
    if (sum != 0.0f) {
        for (int n = 0; n < ntaps; n++) h[n] /= sum;
    }
}

int fm_audio_init(FmAudio *a, float fs_in, int decim_M,
                  float lpf_cut_hz, int ntaps,
                  float deemph_tau_sec)
{
    memset(a, 0, sizeof(*a));
    a->M = decim_M;
    a->ntaps = ntaps;

    a->h = (float *)malloc((size_t)ntaps * sizeof(float));
    a->xhist = (float *)malloc((size_t)ntaps * sizeof(float));
    if (!a->h || !a->xhist) return -1;

    memset(a->xhist, 0, (size_t)ntaps * sizeof(float));
    a->widx = 0;
    a->phase = 0;

    design_lpf_hamming(a->h, ntaps, fs_in, lpf_cut_hz);

    // De-emphasis (1st order IIR low-pass): y += alpha*(x - y)
    // alpha = dt/(tau+dt)
    float fs_out = fs_in / (float)decim_M;
    float dt = 1.0f / fs_out;
    a->deemph_alpha = dt / (deemph_tau_sec + dt);
    a->deemph_y = 0.0f;

    // Peak tracker (for simple limiter/AGC)
    a->peak = 1e-3f;
    a->peak_decay = 0.9995f; // per output sample (48k) -> fairly slow decay

    return 0;
}

void fm_audio_free(FmAudio *a)
{
    free(a->h);
    free(a->xhist);
    a->h = NULL;
    a->xhist = NULL;
}

static float fir_dot_ring(const float *h, const float *xhist, int ntaps, int widx)
{
    // xhist is ring buffer, widx points to next write position
    // Current newest sample is at (widx-1).
    float acc = 0.0f;
    int idx = widx;
    for (int k = 0; k < ntaps; k++) {
        idx = (idx - 1);
        if (idx < 0) idx += ntaps;
        acc += h[k] * xhist[idx];
    }
    return acc;
}

int fm_audio_process(FmAudio *a, float x_in, float *y_out)
{
    // push sample into ring
    a->xhist[a->widx] = x_in;
    a->widx++;
    if (a->widx >= a->ntaps) a->widx = 0;

    // only output every M samples
    a->phase++;
    if (a->phase < a->M) return 0;
    a->phase = 0;

    // FIR at the decimated instants
    float y = fir_dot_ring(a->h, a->xhist, a->ntaps, a->widx);

    // De-emphasis (50 us in EU)
    a->deemph_y += a->deemph_alpha * (y - a->deemph_y);
    y = a->deemph_y;

    // Simple limiter / auto gain:
    float ay = fabsf(y);
    if (ay > a->peak) a->peak = ay;
    else a->peak *= a->peak_decay;

    float gain = 0.90f / (a->peak + 1e-9f);
    y *= gain;

    // Hard clip as safety
    if (y > 1.0f) y = 1.0f;
    if (y < -1.0f) y = -1.0f;

    *y_out = y;
    return 1;
}

int16_t fm_float_to_i16(float x)
{
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    // symmetric-ish mapping
    int v = (int)lrintf(x * 32767.0f);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

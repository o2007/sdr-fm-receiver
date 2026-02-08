#include "rf_decim.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void design_lpf(float *h, int n, float fs, float fc)
{
    float norm = fc / fs;
    int M = n - 1;
    float sum = 0.0f;

    for (int i = 0; i < n; i++) {
        float m = i - M / 2.0f;
        float w = 0.54f - 0.46f *cosf(2*M_PI*i/M);

        float x = (fabsf(m) < 1e-6f)
            ? 2*norm
            : sinf(2*M_PI*norm*m)/(M_PI*m);

        h[i] = x * w;
        sum += h[i];
    }
    for (int i = 0; i < n; i++) h[i] /= sum;
}

int rf_decim_init(RfDecim *d,
                  float fs_in,
                  int M,
                  float cutoff,
                  int ntaps,
                  float dc_beta)
{
    memset(d, 0, sizeof(*d));
    d->M = M;
    d->ntaps = ntaps;
    d->dc_beta = dc_beta;

    d->h  = malloc(ntaps*sizeof(float));
    d->ih = calloc(ntaps,sizeof(float));
    d->qh = calloc(ntaps,sizeof(float));
    if (!d->h || !d->ih || !d->qh) return -1;

    design_lpf(d->h, ntaps, fs_in, cutoff);
    return 0;
}

static float fir(const float *h, const float *x, int n, int w)
{
    float y = 0.0f;
    for (int k = 0; k < n; k++) {
        int i = w - k; if (i < 0) i += n;
        y += h[k] * x[i];
    }
    return y;
}

int rf_decim_process(RfDecim *d,
                     float I,
                     float Q,
                     float *Io,
                     float *Qo)
{
    float ei = I - d->dc_i;
    float eq = Q - d->dc_q;
    d->dc_i += d->dc_beta * ei;
    d->dc_q += d->dc_beta * eq;

    d->ih[d->widx] = ei;
    d->qh[d->widx] = eq;
    d->widx = (d->widx + 1) % d->ntaps;

    if (++d->phase < d->M) return 0;
    d->phase = 0;

    *Io = fir(d->h, d->ih, d->ntaps, d->widx);
    *Qo = fir(d->h, d->qh, d->ntaps, d->widx);
    return 1;
}

void rf_decim_free(RfDecim *d)
{
    free(d->h);
    free(d->ih);
    free(d->qh);
}

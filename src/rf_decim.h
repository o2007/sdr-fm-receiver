#ifndef RF_DECIM_H
#define RF_DECIM_H

#include <stddef.h>

typedef struct {
    int M;          // decimation factor
    int ntaps;
    float *h;       // taps
    float *ihist;   // I ring buffer
    float *qhist;   // Q ring buffer
    int widx;
    int phase;

    // DC blocker
    float dc_i, dc_q;
    float dc_beta;
} RfDecim;


int rf_decim_init(RfDecim *d,
                  float fs_in,
                  int M,
                  float cutoff,
                  int ntaps,
                  float dc_beta);

int rf_decim_process(RfDecim *d,
                     float I,
                     float Q,
                     float *Io,
                     float *Qo);

void rf_decim_free(RfDecim *d);

#endif

#include "fm_demod.h"
#include <math.h>

void fm_discriminator_block(const float *iq, size_t n, FmDemodState *st, float *out)
{
    float prevI = st->prevI;
    float prevQ = st->prevQ;

    size_t i0 = 0;

    if (!st->has_prev) {
        // Seed previous sample from the first sample of this block
        prevI = iq[0];
        prevQ = iq[1];
        st->has_prev = 1;

        out[0] = 0.0f;  // no previous sample yet -> define as 0
        i0 = 1;
    }

    for (size_t k = i0; k < n; k++) {
        float I = iq[2*k + 0];
        float Q = iq[2*k + 1];

        // imag(conj(prev)*curr) and real(conj(prev)*curr)
        float imag = I * prevQ - Q * prevI;
        float real = I * prevI + Q * prevQ;

        out[k] = atan2f(imag, real);

        prevI = I;
        prevQ = Q;
    }

    st->prevI = prevI;
    st->prevQ = prevQ;
}

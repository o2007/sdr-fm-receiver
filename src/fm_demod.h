#ifndef FM_DEMOD_H
#define FM_DEMOD_H

#include <stddef.h>

typedef struct {
    float prevI;
    float prevQ;
    int has_prev;
} FmDemodState;

// Initialize state
static inline void fm_demod_init(FmDemodState *st) {
    st->prevI = 0.0f;
    st->prevQ = 0.0f;
    st->has_prev = 0;
}

/**
 * FM discriminator using phase difference:
 * y[n] = atan2( I[n]*Q[n-1] - Q[n]*I[n-1], I[n]*I[n-1] + Q[n]*Q[n-1] )
 *
 * Inputs:
 *   iq_interleaved: float array length 2*n (I,Q,I,Q,...)
 * Outputs:
 *   out: float array length n
 *
 * Keeps state across blocks via st.
 */
void fm_discriminator_block(const float *iq_interleaved, size_t n,
                            FmDemodState *st, float *out);

#endif

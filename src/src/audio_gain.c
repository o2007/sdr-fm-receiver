#include "audio_gain.h"

#include <string.h>

void scale_pcm_i16(uint8_t *buf, ssize_t n, int vol_pct)
{
    if (n <= 0) return;

    if (vol_pct <= 0) {
        memset(buf, 0, (size_t)n);
        return;
    }
    if (vol_pct == 100) return;

    float gain = (float)vol_pct / 100.0f;
    ssize_t m = n & ~1;

    for (ssize_t i = 0; i < m; i += 2) {
        int16_t s = (int16_t)((uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8));
        float x = (float)s * gain;
        float ax = (x < 0.0f) ? -x : x;

        // Gentle limiter above ~85% FS to keep boosted audio cleaner.
        if (ax > 28000.0f) {
            float excess = ax - 28000.0f;
            ax = 28000.0f + excess * 0.25f;
            x = (x < 0.0f) ? -ax : ax;
        }

        int v = (int)x;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        uint16_t u = (uint16_t)(int16_t)v;
        buf[i] = (uint8_t)(u & 0xFF);
        buf[i + 1] = (uint8_t)((u >> 8) & 0xFF);
    }
}

#include "iq_reader.h"
#include "fm_demod.h"
#include <stdio.h>
#include <stdlib.h>

static void stats(const float *x, size_t n, float *mn, float *mx, float *mean)
{
    float a = x[0], b = x[0];
    double m = 0.0;
    for (size_t i = 0; i < n; i++) {
        float v = x[i];
        if (v < a) a = v;
        if (v > b) b = v;
        m += (double)v;
    }
    *mn = a;
    *mx = b;
    *mean = (float)(m / (double)n);
}

int main(void)
{
    const char *path =
        "data/iq/iq_fc95700000_fs2400000_u8iq_20260207_163657.raw";

    const size_t BLK = 4096;

    IQReader *r = iq_reader_open(path, BLK);
    if (!r) {
        fprintf(stderr, "Failed to open IQ file: %s\n", path);
        return 1;
    }

    float *fm = (float *)malloc(BLK * sizeof(float));
    if (!fm) {
        fprintf(stderr, "malloc failed\n");
        iq_reader_close(r);
        return 1;
    }

    FILE *dump = fopen("fm_block.txt", "w");
    if (!dump) {
        fprintf(stderr, "Failed to open fm_block.txt for writing\n");
        free(fm);
        iq_reader_close(r);
        return 1;
    }

    FmDemodState st;
    fm_demod_init(&st);

    size_t n;
    size_t block = 0;
    size_t dumped = 0;
    const size_t DUMP_MAX = 2000;

    while ((n = iq_reader_read(r)) > 0) {
        fm_discriminator_block(r->iq_buf, n, &st, fm);

        float mn, mx, mean;
        stats(fm, n, &mn, &mx, &mean);

        printf("Block %zu: N=%zu | fm min=%.4f max=%.4f mean=%.6f\n",
               block++, n, mn, mx, mean);

        // Dump first DUMP_MAX samples total across blocks
        size_t to_dump = 0;
        if (dumped < DUMP_MAX) {
            to_dump = DUMP_MAX - dumped;
            if (to_dump > n) to_dump = n;
        }

        for (size_t i = 0; i < to_dump; i++) {
            fprintf(dump, "%.9f\n", fm[i]);
        }
        dumped += to_dump;

        if (dumped >= DUMP_MAX) break;
    }

    fclose(dump);
    free(fm);
    iq_reader_close(r);

    printf("Wrote fm_block.txt (%zu samples)\n", dumped);
    return 0;
}

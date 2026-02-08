#include "iq_reader.h"
#include <stdio.h>
#include <math.h>

int main(void)
{
    const char *path =
        "data/iq/iq_fc95700000_fs2400000_u8iq_20260207_163657.raw";

    IQReader *r = iq_reader_open(path, 4096);
    if (!r) {
        fprintf(stderr, "Failed to open IQ file\n");
        return 1;
    }

    size_t n;
    size_t block = 0;

    while ((n = iq_reader_read(r)) > 0) {
        float sumI2 = 0.0f, sumQ2 = 0.0f, sumMag2 = 0.0f;
        float meanI = 0.0f, meanQ = 0.0f;
        size_t satI = 0, satQ = 0;

        for (size_t i = 0; i < n; i++) {
            float I = r->iq_buf[2*i + 0];
            float Q = r->iq_buf[2*i + 1];

            meanI += I;
            meanQ += Q;

            sumI2 += I*I;
            sumQ2 += Q*Q;
            sumMag2 += I*I + Q*Q;

            // Clipping check using original bytes (0..255)
            // (Works because iq_reader keeps u8_buf for the last read)
            uint8_t Iu8 = r->u8_buf[2*i + 0];
            uint8_t Qu8 = r->u8_buf[2*i + 1];
            if (Iu8 == 0 || Iu8 == 255) satI++;
            if (Qu8 == 0 || Qu8 == 255) satQ++;
        }

        meanI /= (float)n;
        meanQ /= (float)n;

        float Irms = sqrtf(sumI2 / (float)n);
        float Qrms = sqrtf(sumQ2 / (float)n);
        float Mrms = sqrtf(sumMag2 / (float)n);

        printf("Block %zu: N=%zu | meanI=%.4f meanQ=%.4f | Irms=%.4f Qrms=%.4f Mrms=%.4f | satI=%.6f satQ=%.6f\n",
           block++, n, meanI, meanQ, Irms, Qrms, Mrms,
           (float)satI/(float)n, (float)satQ/(float)n);
}


    iq_reader_close(r);
    return 0;
}

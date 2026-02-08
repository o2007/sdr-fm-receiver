#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "iq_reader.h"
#include "rf_decim.h"
#include "fm_demod.h"
#include "fm_audio.h"
#include "wav_writer.h"

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
            "usage: %s iq.raw fs_in decim_dummy out.wav\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    float fs_in = atof(argv[2]);

    // ===== RF STAGE =====
    const int   RF_M = 10;
    const float RF_LPF = 100e3;
    const int   RF_TAPS = 121;

    // ===== AUDIO STAGE =====
    const int   AUD_M = 5;
    const float AUD_LPF = 15e3;
    const int   AUD_TAPS = 201;
    const float DEEMPH = 50e-6f;

    const size_t BLK = 4096;

    IQReader *r = iq_reader_open(path, BLK);
    if (!r) return 1;

    RfDecim rf;
    rf_decim_init(&rf, fs_in, RF_M, RF_LPF, RF_TAPS, 1e-3f);

    float fs_rf = fs_in / RF_M;
    float fs_out = fs_rf / AUD_M;

    FmDemodState dem = {0};
    float *fm = malloc(BLK*sizeof(float));

    FmAudio audio;
    fm_audio_init(&audio, fs_rf, AUD_M, AUD_LPF, AUD_TAPS, DEEMPH);

    WavWriter w;
    /* add bits_per_sample (16) to match wav_open declaration */
    wav_open(&w, argv[4], (uint32_t)fs_out, 1, 16);

    float *iq_rf = malloc((BLK/RF_M+8)*2*sizeof(float));
    int16_t aud[2048];
    size_t aud_n = 0;

    while (1) {
        size_t n = iq_reader_read(r);
        if (n == 0) break;

        size_t nr = 0;
        for (size_t i = 0; i < n; i++) {
            float Io, Qo;
            if (rf_decim_process(&rf,
                r->iq_buf[2*i],
                r->iq_buf[2*i+1],
                &Io,&Qo))
            {
                iq_rf[2*nr]   = Io;
                iq_rf[2*nr+1] = Qo;
                nr++;
            }
        }

        fm_discriminator_block(iq_rf, nr, &dem, fm);

        for (size_t i = 0; i < nr; i++) {
            float y;
            if (fm_audio_process(&audio, fm[i], &y)) {
                aud[aud_n++] = fm_float_to_i16(y);
                if (aud_n == 2048) {
                    wav_write_i16(&w, aud, aud_n);
                    aud_n = 0;
                }
            }
        }
    }

    if (aud_n) wav_write_i16(&w, aud, aud_n);

    wav_close(&w);
    fm_audio_free(&audio);
    rf_decim_free(&rf);
    iq_reader_close(r);
    free(fm);
    free(iq_rf);

    return 0;
}

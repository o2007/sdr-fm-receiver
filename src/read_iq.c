#include <rtl-sdr.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define DEFAULT_SAMPLE_RATE 2400000      // 2.4 MS/s
#define DEFAULT_CENTER_FREQ 100000000    // 100 MHz
#define DEFAULT_SECONDS     5            // capture duration
#define BUF_LEN            (16 * 16384)  // bytes (I/Q interleaved)

// Write a timestamped filename into out[], returns 0 on success.
static int make_filename(char *out, size_t out_sz,
                         uint32_t fc_hz, uint32_t fs_hz) {
    time_t t = time(NULL);
    struct tm tm;
    if (!localtime_r(&t, &tm)) return -1;

    // Example: data/iq/iq_fc100000000_fs2400000_u8iq_20260207_154500.raw
    int n = snprintf(out, out_sz,
                     "data/iq/iq_fc%u_fs%u_u8iq_%04d%02d%02d_%02d%02d%02d.raw",
                     fc_hz, fs_hz,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
}

int main(int argc, char **argv) {
    uint32_t fs = DEFAULT_SAMPLE_RATE;
    uint32_t fc = DEFAULT_CENTER_FREQ;
    int seconds = DEFAULT_SECONDS;

    // Optional CLI:
    //   ./read_iq [center_freq_hz] [sample_rate_hz] [seconds]
    if (argc >= 2) fc = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) fs = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) seconds = atoi(argv[3]);
    if (seconds <= 0) seconds = DEFAULT_SECONDS;

    rtlsdr_dev_t *dev = NULL;
    int r = rtlsdr_open(&dev, 0);
    if (r < 0 || !dev) {
        fprintf(stderr, "ERROR: rtlsdr_open failed (device 0). Is it plugged in?\n");
        return 1;
    }

    // Configure tuner
    rtlsdr_set_sample_rate(dev, fs);
    rtlsdr_set_center_freq(dev, fc);
    rtlsdr_set_tuner_gain_mode(dev, 1);   // 0 = auto gain
    rtlsdr_set_tuner_gain(dev,300);
    rtlsdr_reset_buffer(dev);

    // Ensure output folder exists (data/iq)
    system("mkdir -p data/iq >/dev/null 2>&1");

    char filename[256];
    if (make_filename(filename, sizeof(filename), fc, fs) != 0) {
        fprintf(stderr, "ERROR: could not create output filename\n");
        rtlsdr_close(dev);
        return 1;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: could not open output file: %s\n", filename);
        rtlsdr_close(dev);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(BUF_LEN);
    if (!buf) {
        fprintf(stderr, "ERROR: malloc failed\n");
        fclose(f);
        rtlsdr_close(dev);
        return 1;
    }

    // Approx bytes per second: fs samples/sec * 2 bytes/sample (I+Q)
    double bytes_per_sec = (double)fs * 2.0;
    size_t target_bytes = (size_t)(bytes_per_sec * (double)seconds);

    size_t total_written = 0;
    int n_read = 0;

    fprintf(stderr, "Capturing IQ:\n");
    fprintf(stderr, "  fc = %u Hz\n  fs = %u S/s\n  seconds = %d\n  file = %s\n",
            fc, fs, seconds, filename);

    while (total_written < target_bytes) {
        r = rtlsdr_read_sync(dev, buf, BUF_LEN, &n_read);
        if (r < 0) {
            fprintf(stderr, "ERROR: rtlsdr_read_sync failed\n");
            break;
        }
        if (n_read > 0) {
            size_t w = fwrite(buf, 1, (size_t)n_read, f);
            total_written += w;
            if (w != (size_t)n_read) {
                fprintf(stderr, "ERROR: short write to file\n");
                break;
            }
        }
    }

    fprintf(stderr, "Done. Wrote %zu bytes (%.2f MB)\n",
            total_written, (double)total_written / (1024.0 * 1024.0));

    free(buf);
    fclose(f);
    rtlsdr_close(dev);
    return 0;
}

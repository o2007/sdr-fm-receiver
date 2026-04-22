#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "audio_gain.h"
#include "encoder_gpio.h"
#include "lcd1602_i2c.h"
#include "radio_config.h"
#include "radio_process.h"
#include "time_util.h"

static volatile sig_atomic_t g_stop = 0;
static pthread_mutex_t g_state_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_volume_pct = 80;

static ChildProc g_aplay = { .pid = -1, .fd_in = -1, .fd_out = -1 };
static ChildProc g_rtl = { .pid = -1, .fd_in = -1, .fd_out = -1 };
static pthread_t g_relay_tid;
static int g_relay_running = 0;
static Lcd1602 g_lcd = { .fd = -1, .enabled = 0, .bus = 1, .addr = 0x27, .backlight = 1 };

static const char *k_gain_steps[] = {
    "0.0", "0.9", "1.4", "2.7", "3.7", "7.7", "8.7", "12.5", "14.4", "15.7",
    "16.6", "19.7", "20.7", "22.9", "25.4", "28.0", "29.7", "32.8", "33.8",
    "36.4", "37.2", "38.6", "40.2", "42.1", "43.4", "43.9", "44.5", "48.0", "49.6"
};
static const int k_gain_steps_count = (int)(sizeof(k_gain_steps) / sizeof(k_gain_steps[0]));
static int g_ui_udp_sock = -1;
static struct sockaddr_in g_ui_udp_addr;
static int g_ui_udp_enabled = 0;
static char g_ui_status_path[128] = "";
static int64_t g_last_status_write_ms = 0;

typedef struct {
    int enabled;
    int gpio;
    int chip_fd;
    int line_fd;
    int last_state;
    int64_t last_change_ms;
    int debounce_ms;
} ButtonInput;

static ButtonInput g_step_btn = {
    .enabled = 0, .gpio = 22, .chip_fd = -1, .line_fd = -1,
    .last_state = 1, .last_change_ms = 0, .debounce_ms = 120
};

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int step_button_init(ButtonInput *b)
{
    if (!b || !b->enabled) return 0;
    b->chip_fd = -1;
    b->line_fd = -1;
    b->last_state = 1;
    b->last_change_ms = mono_now_ms();

    for (int chip = 0; chip < 16; chip++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", chip);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        struct gpiohandle_request req;
        memset(&req, 0, sizeof(req));
        req.lineoffsets[0] = (unsigned int)b->gpio;
        req.lines = 1;
        req.flags = GPIOHANDLE_REQUEST_INPUT;
#ifdef GPIOHANDLE_REQUEST_BIAS_PULL_UP
        req.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
#endif
        snprintf(req.consumer_label, sizeof(req.consumer_label), "radio_step_btn");

        if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) == 0) {
            b->chip_fd = fd;
            b->line_fd = req.fd;

            struct gpiohandle_data data;
            memset(&data, 0, sizeof(data));
            if (ioctl(b->line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) == 0) {
                b->last_state = data.values[0] ? 1 : 0;
            }
            fprintf(stderr, "Step button GPIO %d on %s\n", b->gpio, path);
            return 0;
        }
        close(fd);
    }

    fprintf(stderr, "WARN: step button init failed on GPIO %d\n", b->gpio);
    return -1;
}

static void step_button_close(ButtonInput *b)
{
    if (!b) return;
    if (b->line_fd >= 0) close(b->line_fd);
    if (b->chip_fd >= 0) close(b->chip_fd);
    b->line_fd = -1;
    b->chip_fd = -1;
}

static int step_button_poll_pressed(ButtonInput *b)
{
    if (!b || !b->enabled || b->line_fd < 0) return 0;
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    if (ioctl(b->line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) != 0) return 0;

    int st = data.values[0] ? 1 : 0;
    int64_t t = mono_now_ms();
    if (st != b->last_state) {
        if ((t - b->last_change_ms) < b->debounce_ms) {
            b->last_state = st;
            return 0;
        }
        b->last_change_ms = t;
        int prev = b->last_state;
        b->last_state = st;
        // active-low button press event
        if (prev == 1 && st == 0) return 1;
    }
    return 0;
}

static void ui_send_pcm(const uint8_t *buf, ssize_t n)
{
    if (!g_ui_udp_enabled || g_ui_udp_sock < 0 || !buf || n <= 0) return;
    const size_t max_chunk = 1200;
    ssize_t off = 0;
    while (off < n) {
        size_t chunk = (size_t)(n - off);
        if (chunk > max_chunk) chunk = max_chunk;
        (void)sendto(g_ui_udp_sock, buf + off, chunk, MSG_DONTWAIT,
                     (const struct sockaddr *)&g_ui_udp_addr, sizeof(g_ui_udp_addr));
        off += (ssize_t)chunk;
    }
}

static void ui_write_status(const RadioConfig *cfg, int force)
{
    if (!cfg || g_ui_status_path[0] == '\0') return;
    int64_t tnow = mono_now_ms();
    if (!force && (tnow - g_last_status_write_ms) < 100) return;
    g_last_status_write_ms = tnow;

    int vol;
    pthread_mutex_lock(&g_state_mu);
    vol = g_volume_pct;
    pthread_mutex_unlock(&g_state_mu);

    char json[512];
    snprintf(json, sizeof(json),
             "{ \"target_freq_hz\": %d, \"current_freq_hz\": %d, \"volume_pct\": %d, \"gain\": \"%s\", \"ts_ms\": %lld }\n",
             cfg->target_freq_hz, cfg->freq_hz, vol, cfg->gain, (long long)tnow);

    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_ui_status_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    (void)write(fd, json, strlen(json));
    close(fd);
    (void)rename(tmp, g_ui_status_path);
}

static void *relay_thread_fn(void *arg)
{
    (void)arg;
    uint8_t buf[8192];

    while (!g_stop) {
        if (!child_is_running(&g_rtl)) break;
        ssize_t n = read(g_rtl.fd_out, buf, sizeof(buf));
        if (n <= 0) break;

        int vol;
        pthread_mutex_lock(&g_state_mu);
        vol = g_volume_pct;
        pthread_mutex_unlock(&g_state_mu);
        scale_pcm_i16(buf, n, vol);
        ui_send_pcm(buf, n);

        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(g_aplay.fd_in, buf + off, (size_t)(n - off));
            if (w > 0) off += w;
            else if (w < 0 && errno == EINTR) continue;
            else return NULL;
        }
    }

    return NULL;
}

static int start_or_restart_relay_thread(void)
{
    if (g_relay_running) {
        pthread_join(g_relay_tid, NULL);
        g_relay_running = 0;
    }
    if (pthread_create(&g_relay_tid, NULL, relay_thread_fn, NULL) != 0) {
        fprintf(stderr, "WARN: failed to start relay thread\n");
        return -1;
    }
    g_relay_running = 1;
    return 0;
}

static int restart_rtl_chain(const RadioConfig *cfg, const char *warn_msg)
{
    child_kill_group(&g_rtl);
    sleep_ms(cfg->rtl_restart_delay_ms);
    if (spawn_rtl_fm(&g_rtl, cfg) != 0) {
        fprintf(stderr, "%s\n", warn_msg);
        return -1;
    }
    (void)start_or_restart_relay_thread();
    return 0;
}

static void config_defaults(RadioConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->freq_hz = 95700000;
    c->target_freq_hz = c->freq_hz;
    c->min_freq_hz = 87500000;
    c->max_freq_hz = 108000000;
    c->step_hz = 100000;
    c->volume_pct = 80;
    c->volume_step_pct = 4;
    c->volume_max_pct = 400; // allows stronger boost where needed
    c->rtl_index = 0;
    c->ppm = 0;
    strcpy(c->gain, "auto");
    strcpy(c->sdr_rate, "240k");
    c->audio_rate = 96000;
    strcpy(c->atan_mode, "std");
    strcpy(c->alsa_dev, "plughw:0,0");
    c->retune_cooldown_ms = 250;
    c->retune_settle_ms = 220;
    c->rtl_restart_delay_ms = 450;
    c->lcd_enabled = 0;
    c->lcd_i2c_bus = 1;
    c->lcd_i2c_addr = 0x27;
    c->gain_encoder_enabled = 0;
    c->gain_gpio_a = 5;
    c->gain_gpio_b = 6;
    c->step_button_enabled = 1;
    c->step_button_gpio = 22;
    c->ui_udp_enabled = 0;
    c->ui_udp_port = 7355;
    c->ui_status_path[0] = '\0';
}

static int parse_int_arg(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int parse_i2c_addr_arg(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (!end || *end != '\0' || v < 0x03 || v > 0x77) return -1;
    *out = (int)v;
    return 0;
}

static int gain_string_to_index(const char *gain)
{
    if (!gain || strcmp(gain, "auto") == 0 || strcmp(gain, "AUTO") == 0) return -1;
    char *end = NULL;
    double target = strtod(gain, &end);
    if (!end || *end != '\0') return -1;

    int best = 0;
    double best_abs = 1e9;
    for (int i = 0; i < k_gain_steps_count; i++) {
        double v = strtod(k_gain_steps[i], NULL);
        double d = v - target;
        if (d < 0) d = -d;
        if (d < best_abs) {
            best_abs = d;
            best = i;
        }
    }
    return best;
}

static void set_gain_from_index(RadioConfig *cfg, int gain_idx)
{
    if (gain_idx < 0) snprintf(cfg->gain, sizeof(cfg->gain), "%s", "auto");
    else if (gain_idx >= k_gain_steps_count) snprintf(cfg->gain, sizeof(cfg->gain), "%s", k_gain_steps[k_gain_steps_count - 1]);
    else snprintf(cfg->gain, sizeof(cfg->gain), "%s", k_gain_steps[gain_idx]);
}

static void lcd_show_status(const RadioConfig *cfg)
{
    if (!g_lcd.enabled || !cfg) return;

    char line0[17];
    char line1[17];

    int vol;
    pthread_mutex_lock(&g_state_mu);
    vol = g_volume_pct;
    pthread_mutex_unlock(&g_state_mu);

    // 16-char LCD dashboard:
    // LCD-safe Icelandic transliteration labels.
    snprintf(line0, sizeof(line0), "Tidni %5.1f", (double)cfg->target_freq_hz / 1e6);
    snprintf(line1, sizeof(line1), "Hljodstrykur %03d", vol);

    if (lcd1602_set_cursor(&g_lcd, 0, 0) == 0) (void)lcd1602_print_padded(&g_lcd, line0, 16);
    if (lcd1602_set_cursor(&g_lcd, 0, 1) == 0) (void)lcd1602_print_padded(&g_lcd, line1, 16);
}

static int parse_args(int argc, char **argv, RadioConfig *c, Encoder *tune, Encoder *vol)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--start-freq") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->freq_hz);
        else if (strcmp(argv[i], "--min-freq") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->min_freq_hz);
        else if (strcmp(argv[i], "--max-freq") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->max_freq_hz);
        else if (strcmp(argv[i], "--step-hz") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->step_hz);
        else if (strcmp(argv[i], "--gpio-a") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &tune->gpio_a);
        else if (strcmp(argv[i], "--gpio-b") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &tune->gpio_b);
        else if (strcmp(argv[i], "--gpio-vol-a") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &vol->gpio_a);
        else if (strcmp(argv[i], "--gpio-vol-b") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &vol->gpio_b);
        else if (strcmp(argv[i], "--gpio-gain-a") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->gain_gpio_a);
        else if (strcmp(argv[i], "--gpio-gain-b") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->gain_gpio_b);
        else if (strcmp(argv[i], "--gain-encoder-enable") == 0) c->gain_encoder_enabled = 1;
        else if (strcmp(argv[i], "--gain-encoder-disable") == 0) c->gain_encoder_enabled = 0;
        else if (strcmp(argv[i], "--step-button-enable") == 0) c->step_button_enabled = 1;
        else if (strcmp(argv[i], "--step-button-disable") == 0) c->step_button_enabled = 0;
        else if (strcmp(argv[i], "--gpio-step-button") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->step_button_gpio);
        else if (strcmp(argv[i], "--ui-udp-enable") == 0) c->ui_udp_enabled = 1;
        else if (strcmp(argv[i], "--ui-udp-disable") == 0) c->ui_udp_enabled = 0;
        else if (strcmp(argv[i], "--ui-udp-port") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->ui_udp_port);
        else if (strcmp(argv[i], "--ui-status-path") == 0 && i + 1 < argc) snprintf(c->ui_status_path, sizeof(c->ui_status_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--volume-start") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->volume_pct);
        else if (strcmp(argv[i], "--volume-step") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->volume_step_pct);
        else if (strcmp(argv[i], "--volume-max") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->volume_max_pct);
        else if (strcmp(argv[i], "--alsa-device") == 0 && i + 1 < argc) snprintf(c->alsa_dev, sizeof(c->alsa_dev), "%s", argv[++i]);
        else if (strcmp(argv[i], "--rtl-index") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->rtl_index);
        else if (strcmp(argv[i], "--gain") == 0 && i + 1 < argc) snprintf(c->gain, sizeof(c->gain), "%s", argv[++i]);
        else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->ppm);
        else if (strcmp(argv[i], "--sdr-rate") == 0 && i + 1 < argc) snprintf(c->sdr_rate, sizeof(c->sdr_rate), "%s", argv[++i]);
        else if (strcmp(argv[i], "--audio-rate") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->audio_rate);
        else if (strcmp(argv[i], "--atan-mode") == 0 && i + 1 < argc) snprintf(c->atan_mode, sizeof(c->atan_mode), "%s", argv[++i]);
        else if (strcmp(argv[i], "--retune-cooldown-ms") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->retune_cooldown_ms);
        else if (strcmp(argv[i], "--retune-settle-ms") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->retune_settle_ms);
        else if (strcmp(argv[i], "--rtl-restart-delay-ms") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->rtl_restart_delay_ms);
        else if (strcmp(argv[i], "--lcd-enable") == 0) c->lcd_enabled = 1;
        else if (strcmp(argv[i], "--lcd-disable") == 0) c->lcd_enabled = 0;
        else if (strcmp(argv[i], "--lcd-bus") == 0 && i + 1 < argc) parse_int_arg(argv[++i], &c->lcd_i2c_bus);
        else if (strcmp(argv[i], "--lcd-addr") == 0 && i + 1 < argc) parse_i2c_addr_arg(argv[++i], &c->lcd_i2c_addr);
        else {
            fprintf(stderr, "Unknown or incomplete arg: %s\n", argv[i]);
            return -1;
        }
    }

    c->target_freq_hz = c->freq_hz;
    if (c->volume_pct < 0) c->volume_pct = 0;
    if (c->volume_max_pct < 100) c->volume_max_pct = 100;
    if (c->volume_max_pct > 400) c->volume_max_pct = 400;
    if (c->volume_pct > c->volume_max_pct) c->volume_pct = c->volume_max_pct;
    if (c->ui_udp_port < 1024) c->ui_udp_port = 7355;
    if (c->ui_udp_port > 65535) c->ui_udp_port = 7355;
    return 0;
}

int main(int argc, char **argv)
{
    RadioConfig cfg;
    config_defaults(&cfg);

    Encoder tune = { .gpio_a = 17, .gpio_b = 27, .chip_fd = -1, .line_fd = -1, .fd_a = -1, .fd_b = -1 };
    Encoder vol = { .gpio_a = 23, .gpio_b = 24, .chip_fd = -1, .line_fd = -1, .fd_a = -1, .fd_b = -1 };
    Encoder gain = { .gpio_a = 5, .gpio_b = 6, .chip_fd = -1, .line_fd = -1, .fd_a = -1, .fd_b = -1 };

    if (parse_args(argc, argv, &cfg, &tune, &vol) != 0) {
        fprintf(stderr, "Usage: radio_encoder [--start-freq HZ] [--step-hz HZ] [--volume-max PCT] [--lcd-enable]\n");
        return 1;
    }

    gain.gpio_a = cfg.gain_gpio_a;
    gain.gpio_b = cfg.gain_gpio_b;
    g_step_btn.enabled = cfg.step_button_enabled;
    g_step_btn.gpio = cfg.step_button_gpio;

    try_enable_pullups(tune.gpio_a, tune.gpio_b, vol.gpio_a, vol.gpio_b, gain.gpio_a, gain.gpio_b);
    if (encoder_init(&tune) != 0 || encoder_init(&vol) != 0) {
        fprintf(stderr, "ERROR: failed to initialize GPIO encoder inputs\n");
        fprintf(stderr, "Hint: run as root (sudo) or set GPIO permissions.\n");
        encoder_close(&tune);
        encoder_close(&vol);
        encoder_close(&gain);
        return 1;
    }
    if (cfg.gain_encoder_enabled && encoder_init(&gain) != 0) {
        fprintf(stderr, "WARN: gain encoder init failed on GPIO %d/%d; continuing without gain knob.\n",
                gain.gpio_a, gain.gpio_b);
        cfg.gain_encoder_enabled = 0;
        encoder_close(&gain);
    }
    if (g_step_btn.enabled && step_button_init(&g_step_btn) != 0) {
        g_step_btn.enabled = 0;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    pthread_mutex_lock(&g_state_mu);
    g_volume_pct = cfg.volume_pct;
    pthread_mutex_unlock(&g_state_mu);
    g_ui_udp_enabled = cfg.ui_udp_enabled;
    snprintf(g_ui_status_path, sizeof(g_ui_status_path), "%s", cfg.ui_status_path);
    if (g_ui_udp_enabled) {
        g_ui_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (g_ui_udp_sock >= 0) {
            memset(&g_ui_udp_addr, 0, sizeof(g_ui_udp_addr));
            g_ui_udp_addr.sin_family = AF_INET;
            g_ui_udp_addr.sin_port = htons((uint16_t)cfg.ui_udp_port);
            g_ui_udp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            fprintf(stderr, "UI UDP stream enabled: 127.0.0.1:%d\n", cfg.ui_udp_port);
        } else {
            fprintf(stderr, "WARN: failed to open UI UDP socket\n");
            g_ui_udp_enabled = 0;
        }
    }

    if (spawn_aplay(&g_aplay, &cfg) != 0) {
        fprintf(stderr, "ERROR: failed to start aplay\n");
        encoder_close(&tune);
        encoder_close(&vol);
        encoder_close(&gain);
        return 1;
    }
    if (spawn_rtl_fm(&g_rtl, &cfg) != 0) {
        fprintf(stderr, "ERROR: failed to start rtl_fm\n");
        child_kill_group(&g_aplay);
        encoder_close(&tune);
        encoder_close(&vol);
        encoder_close(&gain);
        return 1;
    }

    if (cfg.lcd_enabled) {
        if (lcd1602_init(&g_lcd, cfg.lcd_i2c_bus, cfg.lcd_i2c_addr, 1) == 0) {
            fprintf(stderr, "LCD ready on /dev/i2c-%d addr 0x%02X\n", cfg.lcd_i2c_bus, cfg.lcd_i2c_addr);
            (void)lcd1602_clear(&g_lcd);
            lcd_show_status(&cfg);
        } else {
            fprintf(stderr, "WARN: LCD init failed on /dev/i2c-%d addr 0x%02X\n", cfg.lcd_i2c_bus, cfg.lcd_i2c_addr);
        }
    }

    (void)start_or_restart_relay_thread();

    fprintf(stderr, "Encoder control started (C modular).\n");
    fprintf(stderr, "Start: %.1f MHz, step: %d kHz, volume: %d%% (max %d%%), gain: %s\n",
            (double)cfg.freq_hz / 1e6, cfg.step_hz / 1000, cfg.volume_pct, cfg.volume_max_pct, cfg.gain);
    if (cfg.gain_encoder_enabled) {
        fprintf(stderr, "Gain encoder on GPIO %d/%d\n", cfg.gain_gpio_a, cfg.gain_gpio_b);
    }
    if (g_step_btn.enabled) {
        fprintf(stderr, "Step button on GPIO %d (toggle 10k/100k)\n", g_step_btn.gpio);
    }
    if (g_ui_status_path[0] != '\0') {
        fprintf(stderr, "UI status path: %s\n", g_ui_status_path);
    }
    ui_write_status(&cfg, 1);

    int64_t last_input_ms = 0;
    int64_t last_retune_ms = mono_now_ms();
    int64_t last_rtl_retry_ms = 0;
    int gain_idx = gain_string_to_index(cfg.gain);
    int gain_pending = 0;

    while (!g_stop) {
        if (step_button_poll_pressed(&g_step_btn)) {
            cfg.step_hz = (cfg.step_hz == 100000) ? 10000 : 100000;
            fprintf(stderr, "Step: %d Hz\n", cfg.step_hz);
            lcd_show_status(&cfg);
            ui_write_status(&cfg, 1);
        }

        int st = encoder_poll_step(&tune);
        if (st != 0) {
            cfg.target_freq_hz += st * cfg.step_hz;
            if (cfg.target_freq_hz < cfg.min_freq_hz) cfg.target_freq_hz = cfg.min_freq_hz;
            if (cfg.target_freq_hz > cfg.max_freq_hz) cfg.target_freq_hz = cfg.max_freq_hz;
            fprintf(stderr, "Target: %.1f MHz\n", (double)cfg.target_freq_hz / 1e6);
            lcd_show_status(&cfg);
            ui_write_status(&cfg, 1);
            last_input_ms = mono_now_ms();
        }

        int sv = encoder_poll_step(&vol);
        if (sv != 0) {
            pthread_mutex_lock(&g_state_mu);
            int v = g_volume_pct + sv * cfg.volume_step_pct;
            if (v < 0) v = 0;
            if (v > cfg.volume_max_pct) v = cfg.volume_max_pct;
            g_volume_pct = v;
            pthread_mutex_unlock(&g_state_mu);
            fprintf(stderr, "Volume: %d%%\n", v);
            lcd_show_status(&cfg);
            ui_write_status(&cfg, 1);
        }

        if (cfg.gain_encoder_enabled) {
            int sg = encoder_poll_step(&gain);
            if (sg != 0) {
                if (gain_idx < 0) {
                    // From AUTO, either rotation direction should immediately pick
                    // a real gain so the knob never feels dead.
                    gain_idx = (k_gain_steps_count / 2) + (sg > 0 ? 1 : -1);
                } else {
                    gain_idx += sg;
                }
                if (gain_idx < -1) gain_idx = -1;
                if (gain_idx > k_gain_steps_count - 1) gain_idx = k_gain_steps_count - 1;
                set_gain_from_index(&cfg, gain_idx);
                gain_pending = 1;
                fprintf(stderr, "Gain: %s dB\n", strcmp(cfg.gain, "auto") == 0 ? "auto" : cfg.gain);
                lcd_show_status(&cfg);
                ui_write_status(&cfg, 1);
            }
        }

        int64_t tnow = mono_now_ms();
        if (cfg.target_freq_hz != cfg.freq_hz &&
            (tnow - last_input_ms) >= cfg.retune_settle_ms &&
            (tnow - last_retune_ms) >= cfg.retune_cooldown_ms) {

            cfg.freq_hz = cfg.target_freq_hz;
            fprintf(stderr, "Tune: %.1f MHz\n", (double)cfg.freq_hz / 1e6);
            lcd_show_status(&cfg);
            ui_write_status(&cfg, 1);

            (void)restart_rtl_chain(&cfg, "WARN: rtl_fm restart failed; retrying.");
            last_retune_ms = mono_now_ms();
        } else if (gain_pending && (tnow - last_retune_ms) >= cfg.retune_cooldown_ms) {
            fprintf(stderr, "Apply gain: %s dB\n", strcmp(cfg.gain, "auto") == 0 ? "auto" : cfg.gain);
            if (restart_rtl_chain(&cfg, "WARN: rtl_fm restart failed after gain change; retrying.") == 0) {
                gain_pending = 0;
            }
            last_retune_ms = mono_now_ms();
            lcd_show_status(&cfg);
            ui_write_status(&cfg, 1);
        }

        if (!child_is_running(&g_rtl) && (tnow - last_rtl_retry_ms) >= 600) {
            fprintf(stderr, "Radio process stopped; restarting with backoff.\n");
            last_rtl_retry_ms = tnow;
            (void)restart_rtl_chain(&cfg, "WARN: rtl_fm restart failed; retrying.");
        }

        ui_write_status(&cfg, 0);
        sleep_ms(2);
    }

    if (g_relay_running) pthread_join(g_relay_tid, NULL);
    child_kill_group(&g_rtl);
    child_kill_group(&g_aplay);
    if (g_lcd.enabled) {
        (void)lcd1602_clear(&g_lcd);
        (void)lcd1602_set_cursor(&g_lcd, 0, 0);
        (void)lcd1602_print_padded(&g_lcd, "Radio stopped", 16);
    }
    lcd1602_close(&g_lcd);
    encoder_close(&tune);
    encoder_close(&vol);
    encoder_close(&gain);
    step_button_close(&g_step_btn);
    if (g_ui_udp_sock >= 0) close(g_ui_udp_sock);
    return 0;
}

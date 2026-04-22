#ifndef RADIO_CONFIG_H
#define RADIO_CONFIG_H

typedef struct {
    int freq_hz;
    int target_freq_hz;
    int min_freq_hz;
    int max_freq_hz;
    int step_hz;

    int volume_pct;
    int volume_step_pct;
    int volume_max_pct;

    int rtl_index;
    int ppm;
    char gain[32];
    char sdr_rate[32];
    int audio_rate;
    char atan_mode[16];
    char alsa_dev[64];

    int retune_cooldown_ms;
    int retune_settle_ms;
    int rtl_restart_delay_ms;

    int lcd_enabled;
    int lcd_i2c_bus;
    int lcd_i2c_addr;

    int gain_encoder_enabled;
    int gain_gpio_a;
    int gain_gpio_b;

    int step_button_enabled;
    int step_button_gpio;

    int ui_udp_enabled;
    int ui_udp_port;
    char ui_status_path[128];
} RadioConfig;

#endif

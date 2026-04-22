# SDR FM Physical Radio (Pi 5)

This folder now contains the cleaned final implementation:

- `fm_live_reset.sh`: stable `rtl_fm -> aplay` live playback pipeline
- `radio_encoder.c`: main dual-encoder radio daemon
- `encoder_gpio.c/.h`: GPIO + quadrature decoder module
- `radio_process.c/.h`: process management for `rtl_fm` + `aplay`
- `audio_gain.c/.h`: runtime volume scaling/limiter module
- `radio_config.h`: shared runtime config struct
- `freqshow_terminal.py`: FreqShow-style terminal spectrum/waterfall viewer (no HDMI required)
- `freqshow_web.py`: FreqShow-style browser UI (intended to become HDMI UI surface)
  - Encoder 1: frequency
  - Encoder 2: volume
- `Makefile`: simple run targets

## Quick Start

```bash
cd /home/sdr/sdr-fm-receiver/src
make
make run_encoder
```

`run_encoder` defaults to `--volume-max 200` so you can go above unity gain.

## Wiring (BCM)

Frequency encoder:
- Out A -> GPIO17
- Out B -> GPIO27
- GND pins -> Pi GND

Volume encoder:
- Out A -> GPIO23
- Out B -> GPIO24
- GND pins -> Pi GND

Gain encoder (optional, tuner RF gain):
- Out A -> GPIO5
- Out B -> GPIO6
- GND pins -> Pi GND

Frequency encoder button (optional, step toggle 100k/10k):
- SW -> GPIO22
- GND -> Pi GND

Notes:
- Mechanical encoders shown in your photo are passive, so no VCC pin is used.
- If direction is reversed, swap A/B for that encoder.

## LCD 1602 (I2C Backpack) Wiring

This code supports a 1602 LCD with a PCF8574 I2C backpack (4 pins: `GND`, `VCC`, `SDA`, `SCL`).

Connect to Raspberry Pi:
- LCD `GND` -> Pi `GND` (physical pin 6)
- LCD `VCC` -> Pi `5V` (physical pin 2 or 4)
- LCD `SDA` -> Pi `GPIO2 / SDA1` (physical pin 3)
- LCD `SCL` -> Pi `GPIO3 / SCL1` (physical pin 5)

Enable I2C on Pi:

```bash
sudo raspi-config
# Interface Options -> I2C -> Enable
sudo reboot
```

Detect LCD I2C address (usually `0x27` or `0x3F`):

```bash
sudo apt-get install -y i2c-tools
i2cdetect -y 1
```

Run radio with LCD enabled:

```bash
cd /home/sdr/sdr-fm-receiver/src
make
make run_encoder_lcd
```

With gain encoder enabled:

```bash
make run_encoder_lcd_gain
```

With gain encoder + UI bridge (for linked web display):

```bash
make run_encoder_lcd_gain_ui
```

Or set address manually:

```bash
./radio_encoder --start-freq 99400000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x3F
```

Manual run with gain encoder:

```bash
./radio_encoder --start-freq 99400000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --gain-encoder-enable --gpio-gain-a 5 --gpio-gain-b 6 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27
```

LCD output:
- Row 1: current tuned FM frequency (MHz)
- Row 2: `Volume` plus current step size (`10k`/`100k`)

## Other Run Targets

```bash
make run95
make run95_hi
make run_encoder95
```

## Terminal Spectrum (No HDMI)

This is based on the same core FFT flow as Adafruit FreqShow:
- read IQ from `rtl_sdr`
- FFT + `fftshift`
- dB conversion with rolling auto min/max scaling
- render as either instantaneous spectrum or scrolling waterfall

Run from SSH/terminal:

```bash
cd /home/sdr/sdr-fm-receiver/src
make run_spectrum
```

Useful variants:

```bash
make run_spectrum95
make run_spectrum_instant
python3 ./freqshow_terminal.py --freq 104.5 --sample-rate 2.4 --mode waterfall
```

## Web Spectrum UI (No HDMI, Separate Window/Tab)

Runs a live FreqShow-style UI in your browser with the same RTL-SDR FFT pipeline.

```bash
cd /home/sdr/sdr-fm-receiver/src
make run_spectrum_web
```

Then open:

```text
http://127.0.0.1:8090
```

Controls:
- Tune by buttons or typing MHz and pressing `Tune`
- Switch `waterfall` / `instant`
- Change tuner gain (`auto` or fixed dB)
- Keyboard: `Left/Right` arrows tune by 0.1 MHz, `M` toggles mode

Important:
- Stop `radio_encoder` (or any `rtl_fm/rtl_sdr`) first so the SDR is free.

### Linked To Live Radio Playback

If you want the web UI to show what you are actually listening to from `radio_encoder`:

1. Start radio with UI bridge enabled:

```bash
make run_encoder_lcd_gain_ui
```

2. In another terminal, start linked web UI:

```bash
make run_spectrum_web_linked
```

3. Open:

```text
http://127.0.0.1:8090
```

In linked mode, the web page is read-only and follows:
- target/current frequency from `radio_encoder`
- volume/gain from `radio_encoder`
- live audio spectrum from the same PCM stream sent to speakers

## Auto Start On Boot (systemd)

Install and enable:

```bash
cd /home/sdr/sdr-fm-receiver/src
make
make install_service
make enable_service
```

Check service:

```bash
make service_status
make service_logs
```

Disable if needed:

```bash
make disable_service
```

# SDR FM Physical Radio (Pi 5)

Cleaned, Git-ready C project for a physical FM radio on Raspberry Pi with:
- frequency encoder
- volume encoder
- optional gain encoder
- optional step-toggle button (100k/10k)
- optional LCD1602 over I2C

## Folder Layout

- `src/` C implementation files
- `include/` shared headers
- `scripts/` helper shell scripts
- `systemd/` service unit
- `data/iq/` optional local IQ captures (raw captures are ignored by `.gitignore`)
- `build/` intermediate object files (generated)

## Build

```bash
cd /home/sdr/sdr-fm-receiver/src
make
```

Build output:
- `radio_encoder` (binary)
- `build/*.o` (objects)

## Run

```bash
make run_encoder
```

Other useful presets:

```bash
make run95
make run95_hi
make run_encoder_lcd
make run_encoder_lcd_gain
make run_encoder_lcd_gain_ui
```

## Wiring (BCM)

Frequency encoder:
- A -> GPIO17
- B -> GPIO27

Volume encoder:
- A -> GPIO23
- B -> GPIO24

Gain encoder (optional):
- A -> GPIO5
- B -> GPIO6

Step button (optional):
- SW -> GPIO22
- GND -> Pi GND

## LCD 1602 (I2C Backpack)

- GND -> Pi GND
- VCC -> Pi 5V
- SDA -> GPIO2 (SDA1)
- SCL -> GPIO3 (SCL1)

Enable I2C:

```bash
sudo raspi-config
# Interface Options -> I2C -> Enable
sudo reboot
```

Detect LCD address (usually `0x27` or `0x3F`):

```bash
sudo apt-get install -y i2c-tools
i2cdetect -y 1
```

## Service

Install and enable on boot:

```bash
make
make install_service
make enable_service
```

Inspect service:

```bash
make service_status
make service_logs
```

Disable:

```bash
make disable_service
```

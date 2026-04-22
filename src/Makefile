CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=

TARGET = radio_encoder
SRCS = radio_encoder.c encoder_gpio.c radio_process.c audio_gain.c lcd1602_i2c.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) -lpthread

run95:
	./fm_live_reset.sh 95700000 plughw:0,0 0 auto 0

run95_hi:
	./fm_live_reset.sh 95700000 plughw:0,0 0 auto 0 240k 96000 std

run_encoder:
	./radio_encoder --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400

run_encoder_lcd:
	./radio_encoder --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27

run_encoder_lcd_gain:
	./radio_encoder --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --gain-encoder-enable --gpio-gain-a 5 --gpio-gain-b 6 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27

run_encoder_lcd_gain_ui:
	./radio_encoder --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --gain-encoder-enable --gpio-gain-a 5 --gpio-gain-b 6 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27 --ui-udp-enable --ui-udp-port 7355 --ui-status-path /tmp/radio_ui_status.json

run_encoder95:
	./radio_encoder --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400

run_spectrum:
	python3 ./freqshow_terminal.py --freq 99.4 --sample-rate 2.4 --mode waterfall

run_spectrum95:
	python3 ./freqshow_terminal.py --freq 95.7 --sample-rate 2.4 --mode waterfall

run_spectrum_instant:
	python3 ./freqshow_terminal.py --freq 99.4 --sample-rate 2.4 --mode instant

run_spectrum_web:
	python3 ./freqshow_web.py --host 127.0.0.1 --port 8090 --freq 99.4 --sample-rate 2.4 --mode waterfall

run_spectrum_web_linked:
	python3 ./freqshow_web.py --host 127.0.0.1 --port 8090 --mode waterfall --radio-status-file /tmp/radio_ui_status.json --audio-udp-port 7355

run_encoder_sudo:
	sudo ./radio_encoder --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400

clean:
	rm -rf __pycache__
	rm -f .lgd-nfy0
	rm -f $(TARGET) $(OBJS)

install_service: $(TARGET)
	sudo cp ./radio_encoder.service /etc/systemd/system/radio_encoder.service
	sudo systemctl daemon-reload

enable_service:
	sudo systemctl enable --now radio_encoder.service

disable_service:
	sudo systemctl disable --now radio_encoder.service

service_status:
	systemctl status --no-pager radio_encoder.service

service_logs:
	journalctl -u radio_encoder.service -n 100 --no-pager

.PHONY: all clean run95 run95_hi run_encoder run_encoder_lcd run_encoder_lcd_gain run_encoder_lcd_gain_ui run_encoder95 run_spectrum run_spectrum95 run_spectrum_instant run_spectrum_web run_spectrum_web_linked run_encoder_sudo install_service enable_service disable_service service_status service_logs

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

BUILD_DIR := build
SRC_DIR := .
TARGET := radio_encoder

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) -lpthread

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run95: $(TARGET)
	./scripts/fm_live_reset.sh 95700000 plughw:0,0 0 auto 0

run95_hi: $(TARGET)
	./scripts/fm_live_reset.sh 95700000 plughw:0,0 0 auto 0 240k 96000 std

run_encoder: $(TARGET)
	./$(TARGET) --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400

run_encoder_lcd: $(TARGET)
	./$(TARGET) --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27

run_encoder_lcd_gain: $(TARGET)
	./$(TARGET) --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --gain-encoder-enable --gpio-gain-a 5 --gpio-gain-b 6 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27

run_encoder_lcd_gain_ui: $(TARGET)
	./$(TARGET) --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --gain-encoder-enable --gpio-gain-a 5 --gpio-gain-b 6 --volume-start 80 --volume-step 4 --volume-max 400 --lcd-enable --lcd-bus 1 --lcd-addr 0x27 --ui-udp-enable --ui-udp-port 7355 --ui-status-path /tmp/radio_ui_status.json

run_encoder_sudo: $(TARGET)
	sudo ./$(TARGET) --start-freq 95700000 --step-hz 100000 --gpio-a 17 --gpio-b 27 --gpio-vol-a 23 --gpio-vol-b 24 --volume-start 80 --volume-step 4 --volume-max 400

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)

install_service: $(TARGET)
	sudo cp ./systemd/radio_encoder.service /etc/systemd/system/radio_encoder.service
	sudo systemctl daemon-reload

enable_service:
	sudo systemctl enable --now radio_encoder.service

disable_service:
	sudo systemctl disable --now radio_encoder.service

service_status:
	systemctl status --no-pager radio_encoder.service

service_logs:
	journalctl -u radio_encoder.service -n 100 --no-pager

.PHONY: all clean run95 run95_hi run_encoder run_encoder_lcd run_encoder_lcd_gain run_encoder_lcd_gain_ui run_encoder_sudo install_service enable_service disable_service service_status service_logs

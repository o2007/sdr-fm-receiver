#define _POSIX_C_SOURCE 200809L

#include "lcd1602_i2c.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/*
 * Common PCF8574 LCD backpack pin map:
 * P0=RS, P1=RW, P2=E, P3=BL, P4=D4, P5=D5, P6=D6, P7=D7
 */
#define LCD_RS 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08

static void sleep_us(int us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (long)(us % 1000000) * 1000L;
    nanosleep(&ts, NULL);
}

static int expander_write(Lcd1602 *lcd, unsigned char v)
{
    if (!lcd || lcd->fd < 0) return -1;
    unsigned char b = v | (lcd->backlight ? LCD_BL : 0x00);
    if (write(lcd->fd, &b, 1) != 1) return -1;
    return 0;
}

static int pulse_enable(Lcd1602 *lcd, unsigned char v)
{
    if (expander_write(lcd, v | LCD_EN) != 0) return -1;
    sleep_us(1);
    if (expander_write(lcd, v & (unsigned char)~LCD_EN) != 0) return -1;
    sleep_us(50);
    return 0;
}

static int write4bits(Lcd1602 *lcd, unsigned char nibble_with_data)
{
    if (expander_write(lcd, nibble_with_data) != 0) return -1;
    return pulse_enable(lcd, nibble_with_data);
}

static int send_byte(Lcd1602 *lcd, unsigned char value, int is_data)
{
    unsigned char mode = is_data ? LCD_RS : 0x00;
    unsigned char high = (value & 0xF0) | mode;
    unsigned char low = ((value << 4) & 0xF0) | mode;
    if (write4bits(lcd, high) != 0) return -1;
    if (write4bits(lcd, low) != 0) return -1;
    return 0;
}

static int lcd_cmd(Lcd1602 *lcd, unsigned char cmd)
{
    return send_byte(lcd, cmd, 0);
}

static int lcd_data(Lcd1602 *lcd, unsigned char ch)
{
    return send_byte(lcd, ch, 1);
}

int lcd1602_init(Lcd1602 *lcd, int bus, int addr, int backlight)
{
    if (!lcd) return -1;
    memset(lcd, 0, sizeof(*lcd));
    lcd->fd = -1;
    lcd->enabled = 0;
    lcd->bus = bus;
    lcd->addr = addr;
    lcd->backlight = backlight ? 1 : 0;

    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);
    int fd = open(dev, O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        close(fd);
        return -1;
    }
    lcd->fd = fd;

    sleep_us(50000);
    if (write4bits(lcd, 0x30) != 0) return -1;
    sleep_us(4500);
    if (write4bits(lcd, 0x30) != 0) return -1;
    sleep_us(4500);
    if (write4bits(lcd, 0x30) != 0) return -1;
    sleep_us(150);
    if (write4bits(lcd, 0x20) != 0) return -1; // 4-bit mode

    if (lcd_cmd(lcd, 0x28) != 0) return -1; // 2 line, 5x8
    if (lcd_cmd(lcd, 0x08) != 0) return -1; // display off
    if (lcd_cmd(lcd, 0x01) != 0) return -1; // clear
    sleep_us(2000);
    if (lcd_cmd(lcd, 0x06) != 0) return -1; // entry mode
    if (lcd_cmd(lcd, 0x0C) != 0) return -1; // display on, cursor off

    lcd->enabled = 1;
    return 0;
}

void lcd1602_close(Lcd1602 *lcd)
{
    if (!lcd) return;
    if (lcd->fd >= 0) close(lcd->fd);
    lcd->fd = -1;
    lcd->enabled = 0;
}

int lcd1602_clear(Lcd1602 *lcd)
{
    if (!lcd || !lcd->enabled) return -1;
    if (lcd_cmd(lcd, 0x01) != 0) return -1;
    sleep_us(2000);
    return 0;
}

int lcd1602_set_cursor(Lcd1602 *lcd, int col, int row)
{
    if (!lcd || !lcd->enabled) return -1;
    static const unsigned char row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (row < 0) row = 0;
    if (row > 1) row = 1;
    if (col < 0) col = 0;
    if (col > 15) col = 15;
    return lcd_cmd(lcd, (unsigned char)(0x80 | (row_offsets[row] + col)));
}

int lcd1602_print_padded(Lcd1602 *lcd, const char *text, int width)
{
    if (!lcd || !lcd->enabled || !text) return -1;
    if (width < 1) width = 1;
    if (width > 16) width = 16;

    int n = (int)strlen(text);
    if (n > width) n = width;
    for (int i = 0; i < n; i++) {
        if (lcd_data(lcd, (unsigned char)text[i]) != 0) return -1;
    }
    for (int i = n; i < width; i++) {
        if (lcd_data(lcd, (unsigned char)' ') != 0) return -1;
    }
    return 0;
}

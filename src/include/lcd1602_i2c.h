#ifndef LCD1602_I2C_H
#define LCD1602_I2C_H

typedef struct {
    int fd;
    int enabled;
    int bus;
    int addr;
    int backlight;
} Lcd1602;

int lcd1602_init(Lcd1602 *lcd, int bus, int addr, int backlight);
void lcd1602_close(Lcd1602 *lcd);
int lcd1602_clear(Lcd1602 *lcd);
int lcd1602_set_cursor(Lcd1602 *lcd, int col, int row);
int lcd1602_print_padded(Lcd1602 *lcd, const char *text, int width);

#endif

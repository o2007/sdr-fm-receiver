#ifndef ENCODER_GPIO_H
#define ENCODER_GPIO_H

typedef struct {
    int gpio_a;
    int gpio_b;
    int chip_fd;
    int line_fd;
    int fd_a;
    int fd_b;
    int last_ab;
    int quarter_steps;
    int use_gpiochip;
} Encoder;

void try_enable_pullups(int gpio_a, int gpio_b, int gpio_c, int gpio_d, int gpio_e, int gpio_f);
int encoder_init(Encoder *e);
void encoder_close(Encoder *e);
int encoder_poll_step(Encoder *e);

#endif

#define _POSIX_C_SOURCE 200809L

#include "encoder_gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int write_text_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return (n == (ssize_t)strlen(value)) ? 0 : -1;
}

static int gpio_export(int gpio)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", gpio);
    if (write_text_file("/sys/class/gpio/export", buf) == 0) return 0;
    if (errno == EBUSY) return 0;
    return -1;
}

static void gpio_unexport(int gpio)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", gpio);
    (void)write_text_file("/sys/class/gpio/unexport", buf);
}

static int gpio_set_direction_in(int gpio)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    return write_text_file(path, "in");
}

static int gpio_open_value_fd(int gpio)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    return open(path, O_RDONLY | O_CLOEXEC);
}

static int gpio_read_value_fd(int fd)
{
    char c = '0';
    if (lseek(fd, 0, SEEK_SET) < 0) return 0;
    if (read(fd, &c, 1) != 1) return 0;
    return (c == '1') ? 1 : 0;
}

void try_enable_pullups(int gpio_a, int gpio_b, int gpio_c, int gpio_d, int gpio_e, int gpio_f)
{
    char cmd[384];
    snprintf(cmd, sizeof(cmd),
             "command -v raspi-gpio >/dev/null 2>&1 && "
             "raspi-gpio set %d pu && raspi-gpio set %d pu && "
             "raspi-gpio set %d pu && raspi-gpio set %d pu && "
             "raspi-gpio set %d pu && raspi-gpio set %d pu || true",
             gpio_a, gpio_b, gpio_c, gpio_d, gpio_e, gpio_f);
    (void)system(cmd);
}

int encoder_init(Encoder *e)
{
    e->chip_fd = -1;
    e->line_fd = -1;
    e->use_gpiochip = 0;

    for (int chip = 0; chip < 16; chip++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", chip);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        struct gpiohandle_request req;
        memset(&req, 0, sizeof(req));
        req.lineoffsets[0] = (unsigned int)e->gpio_a;
        req.lineoffsets[1] = (unsigned int)e->gpio_b;
        req.lines = 2;
        req.flags = GPIOHANDLE_REQUEST_INPUT;
#ifdef GPIOHANDLE_REQUEST_BIAS_PULL_UP
        req.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
#endif
        snprintf(req.consumer_label, sizeof(req.consumer_label), "radio_encoder");

        if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) == 0) {
            e->chip_fd = fd;
            e->line_fd = req.fd;
            e->use_gpiochip = 1;

            struct gpiohandle_data data;
            memset(&data, 0, sizeof(data));
            if (ioctl(e->line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) != 0) {
                fprintf(stderr, "GPIO char-dev read failed for %d/%d: %s\n",
                        e->gpio_a, e->gpio_b, strerror(errno));
                return -1;
            }
            e->last_ab = ((data.values[0] ? 1 : 0) << 1) | (data.values[1] ? 1 : 0);
            e->quarter_steps = 0;
            fprintf(stderr, "Encoder GPIO %d/%d on %s\n", e->gpio_a, e->gpio_b, path);
            return 0;
        }

        close(fd);
    }

    if (gpio_export(e->gpio_a) != 0) {
        fprintf(stderr, "GPIO export failed for %d: %s\n", e->gpio_a, strerror(errno));
        return -1;
    }
    if (gpio_export(e->gpio_b) != 0) {
        fprintf(stderr, "GPIO export failed for %d: %s\n", e->gpio_b, strerror(errno));
        return -1;
    }
    if (gpio_set_direction_in(e->gpio_a) != 0) {
        fprintf(stderr, "GPIO direction(in) failed for %d: %s\n", e->gpio_a, strerror(errno));
        return -1;
    }
    if (gpio_set_direction_in(e->gpio_b) != 0) {
        fprintf(stderr, "GPIO direction(in) failed for %d: %s\n", e->gpio_b, strerror(errno));
        return -1;
    }

    e->fd_a = gpio_open_value_fd(e->gpio_a);
    e->fd_b = gpio_open_value_fd(e->gpio_b);
    if (e->fd_a < 0 || e->fd_b < 0) {
        fprintf(stderr, "GPIO value open failed for %d/%d: %s\n", e->gpio_a, e->gpio_b, strerror(errno));
        return -1;
    }

    int a = gpio_read_value_fd(e->fd_a);
    int b = gpio_read_value_fd(e->fd_b);
    e->last_ab = (a << 1) | b;
    e->quarter_steps = 0;
    e->use_gpiochip = 0;
    return 0;
}

void encoder_close(Encoder *e)
{
    if (e->line_fd >= 0) close(e->line_fd);
    if (e->chip_fd >= 0) close(e->chip_fd);
    if (e->fd_a >= 0) close(e->fd_a);
    if (e->fd_b >= 0) close(e->fd_b);
    if (!e->use_gpiochip) {
        gpio_unexport(e->gpio_a);
        gpio_unexport(e->gpio_b);
    }
}

int encoder_poll_step(Encoder *e)
{
    int a = 0, b = 0;
    if (e->use_gpiochip) {
        struct gpiohandle_data data;
        memset(&data, 0, sizeof(data));
        if (ioctl(e->line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) != 0) return 0;
        a = data.values[0] ? 1 : 0;
        b = data.values[1] ? 1 : 0;
    } else {
        a = gpio_read_value_fd(e->fd_a);
        b = gpio_read_value_fd(e->fd_b);
    }

    int ab = (a << 1) | b;
    int prev = e->last_ab;
    if (ab == prev) return 0;
    e->last_ab = ab;
    int tr = (prev << 2) | ab;

    switch (tr) {
        case 0x1: case 0x7: case 0xE: case 0x8:
            e->quarter_steps++;
            break;
        case 0x2: case 0x4: case 0xD: case 0xB:
            e->quarter_steps--;
            break;
        default:
            return 0;
    }

    // Most mechanical encoders used in this project feel better at half-step.
    // This improves responsiveness and reduces the "laggy" feel.
    if (e->quarter_steps >= 2) {
        e->quarter_steps = 0;
        return +1;
    }
    if (e->quarter_steps <= -2) {
        e->quarter_steps = 0;
        return -1;
    }
    return 0;
}

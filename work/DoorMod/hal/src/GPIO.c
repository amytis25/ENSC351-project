#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include "hal/GPIO.h"

/* Store file descriptors for opened lines, indexed by chip and line */
#define MAX_CHIPS 2
#define MAX_LINES_PER_CHIP 70

static struct {
    int line_fd;
    bool is_output;
    bool in_use;
} gpio_fds[MAX_CHIPS][MAX_LINES_PER_CHIP] = {{{-1, false, false}}};

static bool gpio_line_init(int chip, int line, bool is_output) {
    char chip_path[32];
    snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%d", chip);
    
    printf("Initializing GPIO: chip %d, line %d, %s\n", 
           chip, line, is_output ? "output" : "input");
    
    int chip_fd = open(chip_path, O_RDWR);
    if (chip_fd < 0) {
        printf("Failed to open %s: ", chip_path);
        perror(NULL);
        return false;
    }

    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    req.config.flags = is_output ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;

    if (!is_output) {
        req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    }

    /* Try to get line info first to verify it exists */
    struct gpio_v2_line_info linfo;
    memset(&linfo, 0, sizeof(linfo));
    linfo.offset = line;
    
    if (ioctl(chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, &linfo) < 0) {
        printf("Failed to get info for line %d: ", line);
        perror(NULL);
        close(chip_fd);
        return false;
    }
    
    printf("Found GPIO line: chip %d, line %d, name: '%s'\n", 
           chip, line, linfo.name);

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        printf("Failed to get line handle: ");
        perror(NULL);
        close(chip_fd);
        return false;
    }

    if (req.fd < 0) {
        close(chip_fd);
        return false;
    }
    close(chip_fd);

    if (chip < MAX_CHIPS && line < MAX_LINES_PER_CHIP) {
        /* Close existing if any */
        if (gpio_fds[chip][line].line_fd >= 0) {
            close(gpio_fds[chip][line].line_fd);
        }
        gpio_fds[chip][line].line_fd = req.fd;
        gpio_fds[chip][line].is_output = is_output;
        gpio_fds[chip][line].in_use = true;
    }
    return true;
}

bool export_pin(int chip, int line, const char* direction) {
    if (!direction || chip < 0 || chip >= MAX_CHIPS || 
        line < 0 || line >= MAX_LINES_PER_CHIP) return false;
    return gpio_line_init(chip, line, strcmp(direction, "out") == 0);
}

bool set_pin_direction(int chip, int line, const char* direction) {
    if (!direction || chip < 0 || chip >= MAX_CHIPS || 
        line < 0 || line >= MAX_LINES_PER_CHIP) return false;
    /* Must re-initialize with new direction */
    return gpio_line_init(chip, line, strcmp(direction, "out") == 0);
}

bool write_pin_value(int chip, int line, int value) {
    if (chip < 0 || chip >= MAX_CHIPS || 
        line < 0 || line >= MAX_LINES_PER_CHIP) return false;
    
    if (!gpio_fds[chip][line].in_use || 
        gpio_fds[chip][line].line_fd < 0 || 
        !gpio_fds[chip][line].is_output) return false;

    struct gpio_v2_line_values data;
    memset(&data, 0, sizeof(data));
    data.mask = 1;
    data.bits = value ? 1 : 0;

    if (ioctl(gpio_fds[chip][line].line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &data) < 0) {
        perror("GPIO_V2_LINE_SET_VALUES_IOCTL");
        return false;
    }
    return true;
}

int read_pin_value(int chip, int line) {
    if (chip < 0 || chip >= MAX_CHIPS || 
        line < 0 || line >= MAX_LINES_PER_CHIP) return -1;
    
    if (!gpio_fds[chip][line].in_use || 
        gpio_fds[chip][line].line_fd < 0) return -1;

    struct gpio_v2_line_values data;
    memset(&data, 0, sizeof(data));
    data.mask = 1;

    if (ioctl(gpio_fds[chip][line].line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &data) < 0) {
        perror("GPIO_V2_LINE_GET_VALUES_IOCTL");
        return -1;
    }
    return (data.bits & 1) ? 1 : 0;
}






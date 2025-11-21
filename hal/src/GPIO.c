#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include "hal/GPIO.h"

/* Debug output control - set to 0 to disable debug printfs */
//#define GPIO_DEBUG 1
//#define GPIO_DEBUG_VERBOSE 0  /* Set to 1 for detailed per-read debug messages */

#if GPIO_DEBUG
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) do {} while(0)
#endif

#if GPIO_DEBUG_VERBOSE
    #define DEBUG_VERBOSE(...) printf(__VA_ARGS__)
#else
    #define DEBUG_VERBOSE(...) do {} while(0)
#endif

/* Store file descriptors for opened lines, indexed by chip and line */
#define MAX_CHIPS 3
#define MAX_LINES_PER_CHIP 70

static struct {
    int line_fd;
    bool is_output;
    bool in_use;
} gpio_fds[MAX_CHIPS][MAX_LINES_PER_CHIP] = {{{-1, false, false}}};

static bool gpio_line_init(int chip, int line, bool is_output) {
    char chip_path[32];
    snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%d", chip);
    
    DEBUG_PRINT("Initializing GPIO: chip %d, line %d, %s\n", 
           chip, line, is_output ? "output" : "input");
    
    int chip_fd = open(chip_path, O_RDWR);
    if (chip_fd < 0) {
        DEBUG_PRINT("Failed to open %s: ", chip_path);
        perror(NULL);
        return false;
    }

    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    req.config.flags = is_output ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;

    /* For input pins, use pull-down instead of pull-up for HC-SR04 */
    if (!is_output) {
        req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    }

    /* Try to get line info first to verify it exists */
    struct gpio_v2_line_info linfo;
    memset(&linfo, 0, sizeof(linfo));
    linfo.offset = line;
    
    if (ioctl(chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, &linfo) < 0) {
        DEBUG_PRINT("Failed to get info for line %d: ", line);
        perror(NULL);
        close(chip_fd);
        return false;
    }
    
    DEBUG_PRINT("Found GPIO line: chip %d, line %d, name: '%s'\n", 
           chip, line, linfo.name);

    /* Request the GPIO line - ioctl fills in req.fd field */
    DEBUG_PRINT("Requesting GPIO line...\n");
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        DEBUG_PRINT("Failed to get line handle: ");
        perror(NULL);
        close(chip_fd);
        return false;
    }
    
    /* Close the chip fd, we only need the line fd now */
    close(chip_fd);
    
    /* Check if we got a valid fd */
    if (req.fd < 0) {
        DEBUG_PRINT("Invalid line fd received: %d\n", req.fd);
        return false;
    }
    
    DEBUG_PRINT("GPIO line request successful, fd = %d\n", req.fd);

    /* Store the line file descriptor */
    if (chip < MAX_CHIPS && line < MAX_LINES_PER_CHIP) {
        /* Close existing if any */
        if (gpio_fds[chip][line].line_fd >= 0) {
            close(gpio_fds[chip][line].line_fd);
        }
        gpio_fds[chip][line].line_fd = req.fd;
        gpio_fds[chip][line].is_output = is_output;
        gpio_fds[chip][line].in_use = true;
        DEBUG_PRINT("Stored GPIO fd for chip %d, line %d\n", chip, line);
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
    data.mask = 1ULL;  /* We want to affect only the first line */
    data.bits = value ? 1ULL : 0ULL;

    if (ioctl(gpio_fds[chip][line].line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &data) < 0) {
        DEBUG_PRINT("Failed to set value for chip %d, line %d: ", chip, line);
        perror(NULL);
        return false;
    }
    return true;
}

int read_pin_value(int chip, int line) {
    if (chip < 0 || chip >= MAX_CHIPS || 
        line < 0 || line >= MAX_LINES_PER_CHIP) {
        DEBUG_PRINT("read_pin_value: Invalid chip %d or line %d\n", chip, line);
        return -1;
    }
    
    if (!gpio_fds[chip][line].in_use) {
        DEBUG_PRINT("read_pin_value: GPIO chip %d, line %d not in use\n", chip, line);
        return -1;
    }
    
    if (gpio_fds[chip][line].line_fd < 0) {
        DEBUG_PRINT("read_pin_value: Invalid fd %d for chip %d, line %d\n", 
                   gpio_fds[chip][line].line_fd, chip, line);
        return -1;
    }

    DEBUG_VERBOSE("read_pin_value: Reading from chip %d, line %d, fd = %d\n", 
               chip, line, gpio_fds[chip][line].line_fd);

    struct gpio_v2_line_values data;
    memset(&data, 0, sizeof(data));
    data.mask = 1ULL;  /* We want to read only the first line */

    if (ioctl(gpio_fds[chip][line].line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &data) < 0) {
        DEBUG_PRINT("Failed to read value from chip %d, line %d, fd %d: ", 
                   chip, line, gpio_fds[chip][line].line_fd);
        perror(NULL);
        return -1;
    }
    return (data.bits & 1ULL) ? 1 : 0;
}






// terminal_output.h 
// ENSC 351 Fall 2025
// Helper functions for terminal output, recieved over listen-only UDP

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "hal/periodTimer.h"

// Opens sender socket to host:port. Returns false on failure.
bool listener_udp_init(const char* host_ip, int port);

// Display status on host terminal
void udp_send_status(
    int samples_in_second,
    int led_hz,
    double avg_light,
    int dips,
    const Period_statistics_t *light_stats,
    const double *history_samples,
    int history_size);

// Close sender socket.
void listener_socket_close(void);
// UDP.h 
// ENSC 351 Fall 2025
// Header for UDP command server (port 12345) 

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Callback table
typedef struct {
    long long (*get_count)(void);           // total samples so far
    int       (*get_history_size)(void);    // samples in previous second
    int       (*get_dips)(void);            // dips detected in previous second
    double*   (*get_history)(int* size);    // malloc'd array of voltages; caller frees
    void      (*request_shutdown)(void);    // called on "stop" command
} UdpCallbacks;

// Start UDP server thread on specified port
int udp_start(uint16_t port, UdpCallbacks cb);

// Stop UDP server and cleanup. Does not crash program
void udp_stop(void);
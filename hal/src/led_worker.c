#define _POSIX_C_SOURCE 200809L
#include "hal/led_worker.h"
#include "hal/led.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum { 
    LED_TASK_BLINK_RED,
    LED_TASK_HUB_SUCCESS,
    LED_TASK_NETWORK_ERROR,
    LED_TASK_HUB_FAILURE,
    LED_TASK_LOCK_SUCCESS,
    LED_TASK_LOCK_FAILURE,
    LED_TASK_UNLOCK_SUCCESS,
    LED_TASK_UNLOCK_FAILURE,
    LED_TASK_DOOR_ERROR
} LedTaskType;

typedef struct {
    LedTaskType type;
    int flashes;
    int freqHz;
    int duty;
} LedTask;

// Simple ring buffer queue
#define LED_QUEUE_SIZE 32
static LedTask g_queue[LED_QUEUE_SIZE];
static int g_q_head = 0; // next to pop
static int g_q_tail = 0; // next to push
static pthread_mutex_t g_q_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_q_cond  = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static int g_worker_running = 0;

/* queue_count removed — not used. */

static int queue_push(const LedTask *t) {
    int next = (g_q_tail + 1) % LED_QUEUE_SIZE;
    if (next == g_q_head) return -1; // full
    g_queue[g_q_tail] = *t;
    g_q_tail = next;
    return 0;
}

static int queue_pop(LedTask *out) {
    if (g_q_head == g_q_tail) return -1; // empty
    *out = g_queue[g_q_head];
    g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE;
    return 0;
}

static void *worker_fn(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_q_mutex);
        while (g_q_head == g_q_tail && g_worker_running) {
            pthread_cond_wait(&g_q_cond, &g_q_mutex);
        }
        if (!g_worker_running && g_q_head == g_q_tail) {
            pthread_mutex_unlock(&g_q_mutex);
            break;
        }
        LedTask t;
        if (queue_pop(&t) != 0) {
            pthread_mutex_unlock(&g_q_mutex);
            continue;
        }
        pthread_mutex_unlock(&g_q_mutex);

        switch (t.type) {
            case LED_TASK_BLINK_RED:
                LED_blink_red_n(t.flashes, t.freqHz, t.duty);
                break;
            case LED_TASK_HUB_SUCCESS:
                LED_hub_command_success();
                break;
            case LED_TASK_NETWORK_ERROR:
                LED_status_network_error();
                break;
            case LED_TASK_HUB_FAILURE:
                LED_hub_command_failure();
                break;
            case LED_TASK_LOCK_SUCCESS:
                LED_lock_success_sequence();
                break;
            case LED_TASK_LOCK_FAILURE:
                LED_lock_failure_sequence();
                break;
            case LED_TASK_UNLOCK_SUCCESS:
                LED_unlock_success_sequence();
                break;
            case LED_TASK_UNLOCK_FAILURE:
                LED_unlock_failure_sequence();
                break;
            case LED_TASK_DOOR_ERROR:
                LED_status_door_error();
                break;
        }
    }
    return NULL;
}

bool LED_worker_init(void) {
    pthread_mutex_lock(&g_q_mutex);
    if (g_worker_running) { pthread_mutex_unlock(&g_q_mutex); return true; }
    g_worker_running = 1;
    if (pthread_create(&g_worker, NULL, worker_fn, NULL) != 0) {
        g_worker_running = 0;
        pthread_mutex_unlock(&g_q_mutex);
        return false;
    }
    pthread_mutex_unlock(&g_q_mutex);
    return true;
}

void LED_worker_shutdown(void) {
    pthread_mutex_lock(&g_q_mutex);
    if (!g_worker_running) { pthread_mutex_unlock(&g_q_mutex); return; }
    g_worker_running = 0;
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
    pthread_join(g_worker, NULL);
}

void LED_enqueue_blink_red_n(int flashes, int freqHz, int dutyPercent) {
    LedTask t = { .type = LED_TASK_BLINK_RED, .flashes = flashes, .freqHz = freqHz, .duty = dutyPercent };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) {
        // queue full: discard oldest by advancing head then push
        g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE;
        queue_push(&t);
    }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_hub_command_success(void) {
    LedTask t = { .type = LED_TASK_HUB_SUCCESS };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_status_network_error(void) {
    LedTask t = { .type = LED_TASK_NETWORK_ERROR };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_hub_command_failure(void) {
    LedTask t = { .type = LED_TASK_HUB_FAILURE };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_lock_success(void) {
    LedTask t = { .type = LED_TASK_LOCK_SUCCESS };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_lock_failure(void) {
    LedTask t = { .type = LED_TASK_LOCK_FAILURE };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_unlock_success(void) {
    LedTask t = { .type = LED_TASK_UNLOCK_SUCCESS };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_unlock_failure(void) {
    LedTask t = { .type = LED_TASK_UNLOCK_FAILURE };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

void LED_enqueue_status_door_error(void) {
    LedTask t = { .type = LED_TASK_DOOR_ERROR };
    pthread_mutex_lock(&g_q_mutex);
    if (queue_push(&t) != 0) { g_q_head = (g_q_head + 1) % LED_QUEUE_SIZE; queue_push(&t); }
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

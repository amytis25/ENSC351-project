#include "hal/system_webhook.h"
#include "discord_alert.h" 
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Simple singly-linked queue of strdup'd messages
typedef struct msg_node {
    char *msg;
    struct msg_node *next;
} msg_node_t;

static pthread_t worker_thread;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static msg_node_t *queue_head = NULL;
static msg_node_t *queue_tail = NULL;
static int running = 0;
static char *g_webhook_url = NULL;

static void enqueue_msg(const char *s)
{
    if (!s) return;
    msg_node_t *n = malloc(sizeof(*n));
    if (!n) return;
    n->msg = strdup(s);
    n->next = NULL;

    pthread_mutex_lock(&queue_lock);
    if (queue_tail) queue_tail->next = n; else queue_head = n;
    queue_tail = n;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_lock);
}

static char *dequeue_msg(void)
{
    pthread_mutex_lock(&queue_lock);
    while (running && queue_head == NULL) {
        pthread_cond_wait(&queue_cond, &queue_lock);
    }
    if (!running && queue_head == NULL) {
        pthread_mutex_unlock(&queue_lock);
        return NULL;
    }
    msg_node_t *n = queue_head;
    queue_head = n->next;
    if (!queue_head) queue_tail = NULL;
    pthread_mutex_unlock(&queue_lock);

    char *m = n->msg;
    free(n);
    return m;
}

static void *worker(void *arg)
{
    (void)arg;
    while (1) {
        char *m = dequeue_msg();
        if (!m) break;
        if (g_webhook_url) {
            sendDiscordAlert(g_webhook_url, m);
        }
        free(m);
    }
    return NULL;
}

bool hub_webhook_init(const char *webhook_url)
{
    if (webhook_url == NULL) return false;

    // store URL
    g_webhook_url = strdup(webhook_url);
    if (!g_webhook_url) return false;

    if (!discordStart()) {
        free(g_webhook_url);
        g_webhook_url = NULL;
        return false;
    }

    running = 1;
    if (pthread_create(&worker_thread, NULL, worker, NULL) != 0) {
        running = 0;
        discordCleanup();
        free(g_webhook_url);
        g_webhook_url = NULL;
        return false;
    }
    return true;
}

void hub_webhook_shutdown(void)
{
    // stop worker
    pthread_mutex_lock(&queue_lock);
    running = 0;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_lock);

    pthread_join(worker_thread, NULL);

    // drain and free any remaining messages
    pthread_mutex_lock(&queue_lock);
    msg_node_t *n = queue_head;
    while (n) {
        msg_node_t *next = n->next;
        free(n->msg);
        free(n);
        n = next;
    }
    queue_head = queue_tail = NULL;
    pthread_mutex_unlock(&queue_lock);

    if (g_webhook_url) {
        discordCleanup();
        free(g_webhook_url);
        g_webhook_url = NULL;
    }
}

void hub_webhook_send(const char *msg)
{
    if (!msg) return;
    pthread_mutex_lock(&queue_lock);
    if (!running) {
        pthread_mutex_unlock(&queue_lock);
        return;
    }
    pthread_mutex_unlock(&queue_lock);
    enqueue_msg(msg);
}

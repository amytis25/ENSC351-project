#define _POSIX_C_SOURCE 200809L
#include "http_api.h"
#include "doorMod.h"
#include "hal/hub_udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int server_sock = -1;
static volatile int server_running = 0;
static pthread_t server_thread;
static char g_module_id[32] = {0};

// very small helper to send an HTTP response
static void send_response(int client, const char *body)
{
    char header[256];
    int len = strlen(body);
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n",
                        len);
    send(client, header, hlen, 0);
    send(client, body, len, 0);
}

// parse query param value for key from path like /api/status?module=D1
static char *get_query_value(const char *path, const char *key)
{
    const char *q = strchr(path, '?');
    if (!q) return NULL;
    q++; // skip ?
    char *copy = strdup(q);
    char *save = NULL;
    char *tok = strtok_r(copy, "&", &save);
    while (tok) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            char *k = tok; char *v = eq + 1;
            if (strcmp(k, key) == 0) {
                char *res = strdup(v);
                free(copy);
                return res;
            }
        }
        tok = strtok_r(NULL, "&", &save);
    }
    free(copy);
    return NULL;
}

static void handle_client(int client)
{
    char buf[8192];
    ssize_t rc = recv(client, buf, sizeof(buf)-1, 0);
    if (rc <= 0) { close(client); return; }
    buf[rc] = '\0';

    // parse request line
    char method[8] = {0};
    char path[1024] = {0};
    if (sscanf(buf, "%7s %1023s", method, path) < 2) {
        close(client); return;
    }

    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/status", 11) == 0) {
        char *mod = get_query_value(path, "module");
        if (!mod) {
            send_response(client, "{\"error\":\"missing module\"}");
            free(mod);
            close(client);
            return;
        }
        // prefer hub status; fallback to local status if module == local
        HubDoorStatus st;
        if (hub_udp_get_status(mod, &st)) {
            char out[512];
            snprintf(out, sizeof(out), "{\"module\":\"%s\",\"d0_open\":%s,\"d0_locked\":%s,\"lastHB\":%lld}",
                     st.module_id,
                     st.d0_open ? "true" : "false",
                     st.d0_locked ? "true" : "false",
                     st.last_heartbeat_ms);
            send_response(client, out);
            free(mod);
            close(client);
            return;
        }

        // if module equals local, use direct get_door_status
        if (strcmp(mod, g_module_id) == 0) {
            Door_t d = { .state = UNKNOWN };
            d = get_door_status(&d);
            char out[256];
            snprintf(out, sizeof(out), "{\"module\":\"%s\",\"state\":%d}", mod, d.state);
            send_response(client, out);
            free(mod);
            close(client);
            return;
        }

        send_response(client, "{\"error\":\"no status\"}");
        free(mod);
        close(client);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/command") == 0) {
        // find body (very small/simple parser)
        char *body = strstr(buf, "\r\n\r\n");
        if (!body) { send_response(client, "{\"error\":\"no body\"}"); close(client); return; }
        body += 4;
        // expect form-encoded: module=D1&target=D0&action=LOCK
        char *mod = NULL; char *target = NULL; char *action = NULL;
        char *bcopy = strdup(body);
        char *save = NULL;
        char *tok = strtok_r(bcopy, "&", &save);
        while (tok) {
            char *eq = strchr(tok, '=');
            if (eq) {
                *eq = '\0';
                char *k = tok; char *v = eq + 1;
                if (strcmp(k, "module") == 0) mod = strdup(v);
                else if (strcmp(k, "target") == 0) target = strdup(v);
                else if (strcmp(k, "action") == 0) action = strdup(v);
            }
            tok = strtok_r(NULL, "&", &save);
        }
        free(bcopy);

        if (!mod || !action) {
            send_response(client, "{\"error\":\"missing fields\"}");
            free(mod); free(target); free(action);
            close(client); return;
        }

        // If target module is local, perform directly
        if (strcmp(mod, g_module_id) == 0) {
            Door_t d = { .state = UNKNOWN };
            if (strcmp(action, "LOCK") == 0) {
                d = lockDoor(&d);
            } else if (strcmp(action, "UNLOCK") == 0) {
                d = unlockDoor(&d);
            } else if (strcmp(action, "STATUS") == 0) {
                d = get_door_status(&d);
            } else {
                send_response(client, "{\"error\":\"unknown action\"}");
                free(mod); free(target); free(action);
                close(client); return;
            }
            char out[256];
            snprintf(out, sizeof(out), "{\"result\":\"ok\",\"state\":%d}", d.state);
            send_response(client, out);
            free(mod); free(target); free(action);
            close(client); return;
        }

        // Otherwise: forward command to hub or other mechanism. For now
        // we return accepted; the hub will deliver changes via heartbeat/events.
        send_response(client, "{\"result\":\"accepted\"}");
        free(mod); free(target); free(action);
        close(client); return;
    }

    send_response(client, "{\"error\":\"unknown endpoint\"}");
    close(client);
}

static void *server_loop(void *arg)
{
    (void)arg;
    while (server_running) {
        struct sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);
        int client = accept(server_sock, (struct sockaddr*)&cliaddr, &len);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_client(client);
    }
    return NULL;
}

bool http_api_start(const char *bind_addr, unsigned short port, const char *local_module_id)
{
    if (server_running) return false;
    if (local_module_id) strncpy(g_module_id, local_module_id, sizeof(g_module_id)-1);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) return false;

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!bind_addr) bind_addr = "127.0.0.1";
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        close(server_sock); server_sock = -1; return false;
    }

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_sock); server_sock = -1; return false;
    }

    if (listen(server_sock, 5) < 0) {
        close(server_sock); server_sock = -1; return false;
    }

    server_running = 1;
    if (pthread_create(&server_thread, NULL, server_loop, NULL) != 0) {
        server_running = 0; close(server_sock); server_sock = -1; return false;
    }
    return true;
}

void http_api_stop(void)
{
    if (!server_running) return;
    server_running = 0;
    if (server_sock >= 0) {
        shutdown(server_sock, SHUT_RDWR);
        close(server_sock);
        server_sock = -1;
    }
    pthread_join(server_thread, NULL);
}

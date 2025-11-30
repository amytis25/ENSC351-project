#ifndef HTTP_API_H
#define HTTP_API_H

#include <stdbool.h>

// Small local-only HTTP API for door_system.
// Starts a listener on `bind_addr:port` (use "127.0.0.1" to restrict to localhost).
bool http_api_start(const char *bind_addr, unsigned short port, const char *local_module_id);
void http_api_stop(void);

#endif // HTTP_API_H

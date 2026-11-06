/* Minimal WebSocket client (RFC 6455): binary messages only, no TLS, non-blocking after connect. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*on_open)(void);
    void (*on_close)(void);
    void (*on_message)(const uint8_t *data, size_t len);
} ws_callbacks_t;

#define WS_MAX_MESSAGE 8192

/* Returns false if the connection or handshake failed (no callbacks are fired in that case). */
bool ws_connect(const char *host, int port, const char *path);
/* Pumps the socket; fires callbacks. Safe to call when not connected. */
void ws_poll(const ws_callbacks_t *cb);
bool ws_send_binary(const uint8_t *data, size_t len);
bool ws_is_open(void);
void ws_close(void);

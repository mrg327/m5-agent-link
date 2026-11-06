#include "sim_ws.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RX_CAP (WS_MAX_MESSAGE + 16)

static int s_fd = -1;
static bool s_open;
static uint8_t s_rx[RX_CAP];
static size_t s_rx_len;
static uint8_t s_msg[WS_MAX_MESSAGE];
static size_t s_msg_len;
static bool s_msg_overflow;

bool ws_is_open(void) { return s_open; }

void ws_close(void)
{
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
    s_open = false;
    s_rx_len = s_msg_len = 0;
    s_msg_overflow = false;
}

static bool write_all(const uint8_t *p, size_t n)
{
    while (n) {
        ssize_t w = send(s_fd, p, n, 0);
        if (w > 0) {
            p += w;
            n -= (size_t)w;
        } else if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct pollfd pf = {s_fd, POLLOUT, 0};
            if (poll(&pf, 1, 1000) <= 0) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool ws_connect(const char *host, int port, const char *path)
{
    ws_close();
    s_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_fd < 0) return false;
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1 || connect(s_fd, (struct sockaddr *)&a, sizeof a) != 0) {
        ws_close();
        return false;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    char req[256];
    int n = snprintf(req, sizeof req,
                     "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
                     path, host, port);
    if (!write_all((const uint8_t *)req, (size_t)n)) {
        ws_close();
        return false;
    }
    /* Read the HTTP response head (blocking, bounded) so a wrong server is reported as a failed connect. */
    char head[1024];
    size_t len = 0;
    for (;;) {
        struct pollfd pf = {s_fd, POLLIN, 0};
        if (len >= sizeof head - 1 || poll(&pf, 1, 2000) <= 0) break;
        ssize_t r = recv(s_fd, head + len, 1, 0); /* byte-wise: never consume frame bytes after the head */
        if (r <= 0) break;
        len += (size_t)r;
        if (len >= 4 && memcmp(head + len - 4, "\r\n\r\n", 4) == 0) {
            head[len] = 0;
            if (strncmp(head, "HTTP/1.1 101", 12) == 0) {
                fcntl(s_fd, F_SETFL, fcntl(s_fd, F_GETFL) | O_NONBLOCK);
                s_open = true;
                return true;
            }
            break;
        }
    }
    ws_close();
    return false;
}

bool ws_send_binary(const uint8_t *data, size_t len)
{
    if (!s_open || len > WS_MAX_MESSAGE) return false;
    uint8_t hdr[8];
    size_t h = 0;
    hdr[h++] = 0x82;
    if (len < 126) {
        hdr[h++] = 0x80 | (uint8_t)len;
    } else {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(len >> 8);
        hdr[h++] = (uint8_t)len;
    }
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)rand();
    static uint8_t out[WS_MAX_MESSAGE + 16];
    memcpy(out, hdr, h);
    memcpy(out + h, mask, 4);
    for (size_t i = 0; i < len; i++) out[h + 4 + i] = data[i] ^ mask[i & 3];
    if (!write_all(out, h + 4 + len)) {
        ws_close();
        return false;
    }
    return true;
}

static void send_control(uint8_t opcode, const uint8_t *payload, size_t len)
{
    uint8_t out[2 + 4 + 125];
    uint8_t mask[4] = {1, 2, 3, 4};
    if (len > 125) len = 125;
    out[0] = 0x80 | opcode;
    out[1] = 0x80 | (uint8_t)len;
    memcpy(out + 2, mask, 4);
    for (size_t i = 0; i < len; i++) out[6 + i] = payload[i] ^ mask[i & 3];
    write_all(out, 6 + len);
}

/* Consumes whole frames from s_rx. Returns false when the connection must be closed. */
static bool parse_frames(const ws_callbacks_t *cb)
{
    size_t pos = 0;
    while (s_rx_len - pos >= 2) {
        const uint8_t *f = s_rx + pos;
        bool fin = f[0] & 0x80;
        uint8_t op = f[0] & 0x0f;
        uint64_t len = f[1] & 0x7f;
        size_t h = 2;
        if (f[1] & 0x80) return false; /* servers never mask */
        if (len == 126) {
            if (s_rx_len - pos < 4) break;
            len = (uint64_t)f[2] << 8 | f[3];
            h = 4;
        } else if (len == 127) {
            return false; /* far beyond WS_MAX_MESSAGE */
        }
        if (len > WS_MAX_MESSAGE) return false;
        if (s_rx_len - pos < h + len) break;
        const uint8_t *p = f + h;
        pos += h + (size_t)len;
        if (op == 0x8) return false;
        if (op == 0x9) {
            send_control(0xA, p, (size_t)len);
        } else if (op == 0x0 || op == 0x1 || op == 0x2) {
            if (op != 0x0) {
                s_msg_len = 0;
                s_msg_overflow = false;
            }
            if (s_msg_len + len > sizeof s_msg) s_msg_overflow = true;
            else memcpy(s_msg + s_msg_len, p, (size_t)len), s_msg_len += (size_t)len;
            if (fin && !s_msg_overflow && cb->on_message) cb->on_message(s_msg, s_msg_len);
            if (fin) s_msg_len = 0;
        }
    }
    memmove(s_rx, s_rx + pos, s_rx_len - pos);
    s_rx_len -= pos;
    return true;
}

void ws_poll(const ws_callbacks_t *cb)
{
    if (!s_open) return;
    for (;;) {
        ssize_t r = recv(s_fd, s_rx + s_rx_len, RX_CAP - s_rx_len, 0);
        if (r > 0) {
            s_rx_len += (size_t)r;
            if (!parse_frames(cb)) break;
            continue;
        }
        if (r < 0 && (errno == EAGAIN || errno == EINTR)) return;
        break; /* closed or error */
    }
    ws_close();
    if (cb->on_close) cb->on_close();
}

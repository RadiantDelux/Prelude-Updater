/*
 * Prelude Updater - minimal HTTPS client for libnx.
 *
 * Network transport is derived from Prelude-Nro's nextendo_net.c:
 * Copyright (C) 2026 Nextendo Network.
 * Modifications/additions for Prelude Updater: 2026.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include <switch.h>
#include <switch/runtime/devices/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "net.h"

#define MAX_RESPONSE_SIZE (4 * 1024 * 1024)
#define MAX_REDIRECTS 5
#define HEADER_CAP 16384

typedef struct {
    int status;
    long content_length;
    char location[1536];
    bool chunked;
} HttpMeta;

static int tcp_connect_ip(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    sa.sin_addr.s_addr = inet_addr(ip);

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval timeout = { .tv_sec = 8, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wfds, NULL, &timeout) <= 0) {
            close(fd);
            return -1;
        }

        int soerr = 0;
        socklen_t slen = sizeof(soerr);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) < 0 || soerr != 0) {
            close(fd);
            return -1;
        }
    } else if (rc < 0) {
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, flags);
    return fd;
}

static const char *resolve_host(const char *host) {
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) return NULL;
    return inet_ntoa(*(struct in_addr *)he->h_addr_list[0]);
}

static bool parse_https_url(const char *url, char *host, size_t host_cap,
                            char *path, size_t path_cap) {
    const char prefix[] = "https://";
    size_t plen = sizeof(prefix) - 1;
    if (!url || strncmp(url, prefix, plen) != 0) return false;

    const char *p = url + plen;
    const char *slash = strchr(p, '/');
    const char *end_host = slash ? slash : p + strlen(p);
    size_t hlen = (size_t)(end_host - p);
    if (hlen == 0 || hlen >= host_cap) return false;

    memcpy(host, p, hlen);
    host[hlen] = '\0';

    if (slash) {
        if (strlen(slash) >= path_cap) return false;
        strcpy(path, slash);
    } else {
        if (path_cap < 2) return false;
        strcpy(path, "/");
    }
    return true;
}

static int parse_status_line(const unsigned char *hdr, size_t len) {
    if (len < 12 || memcmp(hdr, "HTTP/", 5) != 0) return NET_ERR_HTTP;
    const unsigned char *space = memchr(hdr, ' ', len < 64 ? len : 64);
    if (!space || space + 3 >= hdr + len) return NET_ERR_HTTP;
    if (!isdigit(space[1]) || !isdigit(space[2]) || !isdigit(space[3])) return NET_ERR_HTTP;
    return (space[1] - '0') * 100 + (space[2] - '0') * 10 + (space[3] - '0');
}

static bool ascii_starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (!*s) return false;
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
        s++;
        prefix++;
    }
    return true;
}

static void trim_copy(char *dst, size_t dst_cap, const char *begin, const char *end) {
    while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    size_t len = (size_t)(end - begin);
    if (len >= dst_cap) len = dst_cap - 1;
    memcpy(dst, begin, len);
    dst[len] = '\0';
}

static const char *find_crlf_bounded(const char *p, const char *end) {
    while (p + 1 < end) {
        if (p[0] == '\r' && p[1] == '\n') return p;
        p++;
    }
    return NULL;
}

static void parse_headers(const unsigned char *hdr, size_t len, HttpMeta *meta) {
    memset(meta, 0, sizeof(*meta));
    meta->status = parse_status_line(hdr, len);
    meta->content_length = -1;

    const char *p = (const char *)hdr;
    const char *end = p + len;
    const char *line = find_crlf_bounded(p, end);
    if (!line) return;
    p = line + 2;

    while (p < end) {
        const char *eol = find_crlf_bounded(p, end);
        if (!eol || eol == p) break;

        if ((size_t)(eol - p) >= strlen("Content-Length:") &&
            ascii_starts_with_ci(p, "Content-Length:")) {
            const char *v = p + strlen("Content-Length:");
            meta->content_length = strtol(v, NULL, 10);
        } else if ((size_t)(eol - p) >= strlen("Location:") &&
                   ascii_starts_with_ci(p, "Location:")) {
            const char *v = p + strlen("Location:");
            trim_copy(meta->location, sizeof(meta->location), v, eol);
        } else if ((size_t)(eol - p) >= strlen("Transfer-Encoding:") &&
                   ascii_starts_with_ci(p, "Transfer-Encoding:")) {
            char value[128];
            const char *v = p + strlen("Transfer-Encoding:");
            trim_copy(value, sizeof(value), v, eol);
            for (char *q = value; *q; q++) *q = (char)tolower((unsigned char)*q);
            if (strstr(value, "chunked")) meta->chunked = true;
        }
        p = eol + 2;
    }
}

static bool is_redirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool resolve_redirect(const char *current_url, const char *location,
                             char *out, size_t out_cap) {
    if (!location || !location[0]) return false;
    if (strncmp(location, "https://", 8) == 0) {
        if (strlen(location) >= out_cap) return false;
        strcpy(out, location);
        return true;
    }

    if (location[0] == '/') {
        char host[256];
        char path[1536];
        if (!parse_https_url(current_url, host, sizeof(host), path, sizeof(path))) return false;
        int n = snprintf(out, out_cap, "https://%s%s", host, location);
        return n > 0 && (size_t)n < out_cap;
    }
    return false;
}

static int ssl_open(const char *host, SslContext *ctx, SslConnection *conn, int *out_fd) {
    const char *ip = resolve_host(host);
    if (!ip) return NET_ERR_DNS;

    int fd = tcp_connect_ip(ip, 443);
    if (fd < 0) return NET_ERR_CONNECT;

    Result rc = sslCreateContext(ctx, SslVersion_Auto);
    if (R_FAILED(rc)) { close(fd); return NET_ERR_SSL; }

    rc = sslContextCreateConnection(ctx, conn);
    if (R_FAILED(rc)) {
        sslContextClose(ctx);
        close(fd);
        return NET_ERR_SSL;
    }

    int ssl_fd = socketSslConnectionSetSocketDescriptor(conn, fd);
    if (ssl_fd < 0) {
        sslConnectionClose(conn);
        sslContextClose(ctx);
        close(fd);
        return NET_ERR_SSL;
    }

    rc = sslConnectionSetHostName(conn, host, strlen(host));
    if (R_FAILED(rc)) {
        sslConnectionClose(conn);
        sslContextClose(ctx);
        close(ssl_fd);
        return NET_ERR_SSL;
    }

    /* Keep certificate verification enabled. GitHub is the trust boundary. */
    static const u8 alpn[] = { 8, 'h','t','t','p','/','1','.','1' };
    sslConnectionSetNextAlpnProto(conn, alpn, sizeof(alpn));

    rc = sslConnectionDoHandshake(conn, NULL, NULL, NULL, 0);
    if (R_FAILED(rc)) {
        sslConnectionClose(conn);
        sslContextClose(ctx);
        close(ssl_fd);
        return NET_ERR_SSL;
    }

    *out_fd = ssl_fd;
    return NET_OK;
}

static void ssl_close(SslContext *ctx, SslConnection *conn, int fd) {
    sslConnectionClose(conn);
    sslContextClose(ctx);
    if (fd >= 0) close(fd);
}

static int ssl_write_request(SslConnection *conn, const char *host, const char *path) {
    char req[2048];
    int len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Prelude-Updater/1.0\r\n"
        "Accept: application/vnd.github+json, application/octet-stream, */*\r\n"
        "X-GitHub-Api-Version: 2022-11-28\r\n"
        "Connection: close\r\n\r\n",
        path, host);
    if (len <= 0 || (size_t)len >= sizeof(req)) return NET_ERR_URL;

    size_t sent = 0;
    while (sent < (size_t)len) {
        u32 written = 0;
        Result rc = sslConnectionWrite(conn, req + sent, (u32)(len - sent), &written);
        if (R_FAILED(rc) || written == 0) return NET_ERR_SSL;
        sent += written;
    }
    return NET_OK;
}

static bool find_header_end(const unsigned char *buf, size_t len, size_t *out_off) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            *out_off = i + 4;
            return true;
        }
    }
    return false;
}

static unsigned char *https_get_once(const char *url, size_t *out_len,
                                     HttpMeta *meta, int *out_error) {
    *out_len = 0;
    *out_error = NET_ERR_UNKNOWN;

    char host[256];
    char path[1536];
    if (!parse_https_url(url, host, sizeof(host), path, sizeof(path))) {
        *out_error = NET_ERR_URL;
        return NULL;
    }

    SslContext ctx;
    SslConnection conn;
    int fd = -1;
    int err = ssl_open(host, &ctx, &conn, &fd);
    if (err != NET_OK) { *out_error = err; return NULL; }

    err = ssl_write_request(&conn, host, path);
    if (err != NET_OK) {
        ssl_close(&ctx, &conn, fd);
        *out_error = err;
        return NULL;
    }

    size_t cap = 65536;
    size_t len = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) {
        ssl_close(&ctx, &conn, fd);
        *out_error = NET_ERR_OOM;
        return NULL;
    }

    for (;;) {
        if (len + 32768 > cap) {
            if (cap >= MAX_RESPONSE_SIZE) {
                free(buf);
                ssl_close(&ctx, &conn, fd);
                *out_error = NET_ERR_HTTP;
                return NULL;
            }
            size_t new_cap = cap * 2;
            if (new_cap > MAX_RESPONSE_SIZE) new_cap = MAX_RESPONSE_SIZE;
            unsigned char *tmp = realloc(buf, new_cap);
            if (!tmp) {
                free(buf);
                ssl_close(&ctx, &conn, fd);
                *out_error = NET_ERR_OOM;
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }

        u32 got = 0;
        Result rc = sslConnectionRead(&conn, buf + len, (u32)(cap - len), &got);
        if (R_FAILED(rc) || got == 0) break;
        len += got;
    }
    ssl_close(&ctx, &conn, fd);

    size_t body_off = 0;
    if (!find_header_end(buf, len, &body_off)) {
        free(buf);
        *out_error = NET_ERR_HTTP;
        return NULL;
    }

    parse_headers(buf, body_off, meta);
    size_t body_len = len - body_off;
    unsigned char *body = malloc(body_len + 1);
    if (!body) {
        free(buf);
        *out_error = NET_ERR_OOM;
        return NULL;
    }
    memcpy(body, buf + body_off, body_len);
    body[body_len] = '\0';
    free(buf);

    *out_len = body_len;
    *out_error = NET_OK;
    return body;
}

static long https_download_once(const char *url, FILE *out,
                                HttpMeta *meta, int *out_error) {
    *out_error = NET_ERR_UNKNOWN;

    char host[256];
    char path[1536];
    if (!parse_https_url(url, host, sizeof(host), path, sizeof(path))) {
        *out_error = NET_ERR_URL;
        return -1;
    }

    SslContext ctx;
    SslConnection conn;
    int fd = -1;
    int err = ssl_open(host, &ctx, &conn, &fd);
    if (err != NET_OK) { *out_error = err; return -1; }

    err = ssl_write_request(&conn, host, path);
    if (err != NET_OK) {
        ssl_close(&ctx, &conn, fd);
        *out_error = err;
        return -1;
    }

    unsigned char rbuf[32768];
    unsigned char hdr[HEADER_CAP];
    size_t hlen = 0;
    bool in_body = false;
    long body_bytes = 0;
    bool read_error = false;

    memset(meta, 0, sizeof(*meta));
    meta->content_length = -1;

    for (;;) {
        u32 got = 0;
        Result rc = sslConnectionRead(&conn, rbuf, sizeof(rbuf), &got);
        if (R_FAILED(rc)) { read_error = true; break; }
        if (got == 0) break;

        if (in_body) {
            if (!is_redirect(meta->status)) {
                if (fwrite(rbuf, 1, got, out) != got) {
                    ssl_close(&ctx, &conn, fd);
                    *out_error = NET_ERR_WRITE;
                    return -1;
                }
                body_bytes += got;
            }
            continue;
        }

        size_t i = 0;
        while (i < got && hlen < sizeof(hdr)) {
            hdr[hlen++] = rbuf[i++];
            size_t off = 0;
            if (find_header_end(hdr, hlen, &off)) {
                parse_headers(hdr, off, meta);
                in_body = true;
                if (i < got && !is_redirect(meta->status)) {
                    size_t rem = got - i;
                    if (fwrite(rbuf + i, 1, rem, out) != rem) {
                        ssl_close(&ctx, &conn, fd);
                        *out_error = NET_ERR_WRITE;
                        return -1;
                    }
                    body_bytes += (long)rem;
                }
                break;
            }
        }

        if (!in_body && hlen == sizeof(hdr)) {
            ssl_close(&ctx, &conn, fd);
            *out_error = NET_ERR_HTTP;
            return -1;
        }
    }

    ssl_close(&ctx, &conn, fd);

    if (!in_body) {
        *out_error = NET_ERR_HTTP;
        return -1;
    }

    if (!is_redirect(meta->status)) {
        if (meta->content_length >= 0 && body_bytes != meta->content_length) {
            *out_error = NET_ERR_HTTP;
            return -1;
        }
        if (meta->content_length < 0 && read_error) {
            *out_error = NET_ERR_SSL;
            return -1;
        }
    }

    *out_error = NET_OK;
    return body_bytes;
}

unsigned char *net_https_get_url(const char *url, size_t *out_len,
                                 int *out_status, int *out_error) {
    *out_len = 0;
    *out_status = 0;
    *out_error = NET_ERR_UNKNOWN;

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) { *out_error = NET_ERR_CONNECT; return NULL; }
    rc = sslInitialize(4);
    if (R_FAILED(rc)) {
        socketExit();
        *out_error = NET_ERR_SSL;
        return NULL;
    }

    char current[2048];
    if (!url || strlen(url) >= sizeof(current)) {
        sslExit(); socketExit();
        *out_error = NET_ERR_URL;
        return NULL;
    }
    strcpy(current, url);

    unsigned char *result = NULL;
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        HttpMeta meta;
        size_t len = 0;
        int err = NET_ERR_UNKNOWN;
        unsigned char *body = https_get_once(current, &len, &meta, &err);
        if (!body) {
            *out_error = err;
            break;
        }

        if (is_redirect(meta.status)) {
            char next[2048];
            bool ok = resolve_redirect(current, meta.location, next, sizeof(next));
            free(body);
            if (!ok || hop == MAX_REDIRECTS) {
                *out_error = NET_ERR_REDIRECT;
                break;
            }
            strcpy(current, next);
            continue;
        }

        *out_status = meta.status;
        *out_len = len;
        *out_error = NET_OK;
        result = body;
        break;
    }

    sslExit();
    socketExit();
    return result;
}

long net_https_download_url(const char *url, FILE *out,
                            int *out_status, int *out_error) {
    *out_status = 0;
    *out_error = NET_ERR_UNKNOWN;

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) { *out_error = NET_ERR_CONNECT; return -1; }
    rc = sslInitialize(4);
    if (R_FAILED(rc)) {
        socketExit();
        *out_error = NET_ERR_SSL;
        return -1;
    }

    char current[2048];
    if (!url || strlen(url) >= sizeof(current)) {
        sslExit(); socketExit();
        *out_error = NET_ERR_URL;
        return -1;
    }
    strcpy(current, url);

    long result = -1;
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        HttpMeta meta;
        int err = NET_ERR_UNKNOWN;
        long bytes = https_download_once(current, out, &meta, &err);
        if (bytes < 0) {
            *out_error = err;
            break;
        }

        if (is_redirect(meta.status)) {
            char next[2048];
            bool ok = resolve_redirect(current, meta.location, next, sizeof(next));
            if (!ok || hop == MAX_REDIRECTS) {
                *out_error = NET_ERR_REDIRECT;
                break;
            }
            strcpy(current, next);
            continue;
        }

        *out_status = meta.status;
        *out_error = NET_OK;
        result = bytes;
        break;
    }

    sslExit();
    socketExit();
    return result;
}

const char *net_error_string(int error) {
    switch (error) {
        case NET_OK: return "sin error";
        case NET_ERR_DNS: return "fallo DNS";
        case NET_ERR_CONNECT: return "fallo de conexion";
        case NET_ERR_SSL: return "fallo TLS/SSL";
        case NET_ERR_HTTP: return "respuesta HTTP invalida o incompleta";
        case NET_ERR_OOM: return "memoria insuficiente";
        case NET_ERR_WRITE: return "error escribiendo en la SD";
        case NET_ERR_REDIRECT: return "redirect HTTPS invalido";
        case NET_ERR_URL: return "URL invalida";
        default: return "error de red desconocido";
    }
}

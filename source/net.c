/*
 * Prelude Updater - HTTPS transport using devkitPro switch-curl.
 * Copyright (C) 2026 RadiantDelux.
 * AGPL-3.0-or-later
 */

#include <switch.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"

#define MAX_MEMORY_RESPONSE (4 * 1024 * 1024)
#define CURL_BUFFER_SIZE    (256 * 1024L)
#define MAX_REDIRECTS       5L

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
    bool oom;
} MemoryBuffer;

typedef struct {
    FILE *file;
    NetProgressCallback progress_cb;
    void *progress_user;
    long expected_total;
    long current_status;
    long body_bytes;
} DownloadContext;

static size_t memory_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    MemoryBuffer *buf = (MemoryBuffer *)userdata;
    size_t bytes = size * nmemb;
    if (!bytes) return 0;

    if (buf->size + bytes + 1 > MAX_MEMORY_RESPONSE) {
        buf->oom = true;
        return 0;
    }

    size_t need = buf->size + bytes + 1;
    if (need > buf->capacity) {
        size_t new_cap = buf->capacity ? buf->capacity : 65536;
        while (new_cap < need) new_cap *= 2;
        if (new_cap > MAX_MEMORY_RESPONSE) new_cap = MAX_MEMORY_RESPONSE;

        unsigned char *tmp = (unsigned char *)realloc(buf->data, new_cap);
        if (!tmp) {
            buf->oom = true;
            return 0;
        }
        buf->data = tmp;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, ptr, bytes);
    buf->size += bytes;
    buf->data[buf->size] = '\0';
    return bytes;
}

static size_t file_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    DownloadContext *ctx = (DownloadContext *)userdata;
    size_t bytes = size * nmemb;
    if (!ctx || !ctx->file) return 0;

    /* Ignore optional HTML bodies attached to redirect responses. */
    if (ctx->current_status >= 300 && ctx->current_status < 400) return bytes;

    size_t written = fwrite(ptr, 1, bytes, ctx->file);
    ctx->body_bytes += (long)written;
    return written;
}

static size_t download_header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    DownloadContext *ctx = (DownloadContext *)userdata;
    size_t bytes = size * nitems;
    if (!ctx || bytes < 5) return bytes;

    if (memcmp(buffer, "HTTP/", 5) == 0) {
        const char *space = memchr(buffer, ' ', bytes);
        if (space && space + 3 < buffer + bytes) {
            long status = strtol(space + 1, NULL, 10);
            if (status > 0) ctx->current_status = status;
        }
    }
    return bytes;
}

static int xfer_info_cb(void *clientp,
                        curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    DownloadContext *ctx = (DownloadContext *)clientp;
    if (!ctx || !ctx->progress_cb) return 0;

    long total = dltotal > 0 ? (long)dltotal : ctx->expected_total;
    return ctx->progress_cb((long)dlnow, total, ctx->progress_user) ? 1 : 0;
}

static int map_curl_error(CURLcode code) {
    switch (code) {
        case CURLE_OK: return NET_OK;
        case CURLE_URL_MALFORMAT: return NET_ERR_URL;
        case CURLE_COULDNT_RESOLVE_HOST: return NET_ERR_DNS;
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT: return NET_ERR_CONNECT;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
            return NET_ERR_SSL;
        case CURLE_TOO_MANY_REDIRECTS: return NET_ERR_REDIRECT;
        case CURLE_WRITE_ERROR: return NET_ERR_WRITE;
        case CURLE_ABORTED_BY_CALLBACK: return NET_ERR_ABORTED;
        case CURLE_OUT_OF_MEMORY: return NET_ERR_OOM;
        default: return NET_ERR_HTTP;
    }
}

static CURL *create_easy(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Prelude-Updater/1.1.0");
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, CURL_BUFFER_SIZE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    return curl;
}

static bool g_network_ready = false;

static void network_end(void) {
    if (!g_network_ready) return;
    curl_global_cleanup();
    socketExit();
    g_network_ready = false;
}

static bool network_begin(void) {
    if (g_network_ready) return true;

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) return false;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        socketExit();
        return false;
    }

    g_network_ready = true;
    atexit(network_end);
    return true;
}

unsigned char *net_https_get_url(const char *url, size_t *out_len,
                                 int *out_status, int *out_error) {
    if (out_len) *out_len = 0;
    if (out_status) *out_status = 0;
    if (out_error) *out_error = NET_ERR_UNKNOWN;
    if (!url || strncmp(url, "https://", 8) != 0) {
        if (out_error) *out_error = NET_ERR_URL;
        return NULL;
    }

    if (!network_begin()) {
        if (out_error) *out_error = NET_ERR_CONNECT;
        return NULL;
    }

    MemoryBuffer buf = {0};
    CURL *curl = create_easy(url);
    if (!curl) {
        if (out_error) *out_error = NET_ERR_OOM;
        return NULL;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memory_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (out_status) *out_status = (int)status;
    if (res != CURLE_OK || buf.oom) {
        free(buf.data);
        if (out_error) *out_error = buf.oom ? NET_ERR_OOM : map_curl_error(res);
        return NULL;
    }

    if (!buf.data) {
        buf.data = (unsigned char *)calloc(1, 1);
        if (!buf.data) {
            if (out_error) *out_error = NET_ERR_OOM;
            return NULL;
        }
    }

    if (out_len) *out_len = buf.size;
    if (out_error) *out_error = NET_OK;
    return buf.data;
}

long net_https_download_url(const char *url, FILE *out,
                            int *out_status, int *out_error,
                            NetProgressCallback progress_cb, void *progress_user) {
    if (out_status) *out_status = 0;
    if (out_error) *out_error = NET_ERR_UNKNOWN;
    if (!url || !out || strncmp(url, "https://", 8) != 0) {
        if (out_error) *out_error = NET_ERR_URL;
        return -1;
    }

    if (!network_begin()) {
        if (out_error) *out_error = NET_ERR_CONNECT;
        return -1;
    }

    CURL *curl = create_easy(url);
    if (!curl) {
        if (out_error) *out_error = NET_ERR_OOM;
        return -1;
    }

    DownloadContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.file = out;
    ctx.progress_cb = progress_cb;
    ctx.progress_user = progress_user;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, download_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_info_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_easy_cleanup(curl);
    if (out_status) *out_status = (int)status;
    if (res != CURLE_OK) {
        if (out_error) *out_error = map_curl_error(res);
        return -1;
    }

    if (out_error) *out_error = NET_OK;
    return ctx.body_bytes;
}

const char *net_error_string(int error) {
    switch (error) {
        case NET_OK: return "sin error";
        case NET_ERR_DNS: return "fallo DNS";
        case NET_ERR_CONNECT: return "fallo de conexion o timeout";
        case NET_ERR_SSL: return "fallo TLS/SSL";
        case NET_ERR_HTTP: return "respuesta HTTP invalida";
        case NET_ERR_OOM: return "memoria insuficiente";
        case NET_ERR_WRITE: return "error escribiendo en la SD";
        case NET_ERR_REDIRECT: return "demasiados redirects HTTPS";
        case NET_ERR_URL: return "URL invalida";
        case NET_ERR_ABORTED: return "descarga cancelada";
        default: return "error de red desconocido";
    }
}

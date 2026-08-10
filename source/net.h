#ifndef PRELUDE_UPDATER_NET_H
#define PRELUDE_UPDATER_NET_H

#include <stddef.h>
#include <stdio.h>

#define NET_OK             0
#define NET_ERR_UNKNOWN   -1
#define NET_ERR_DNS       -2
#define NET_ERR_CONNECT   -3
#define NET_ERR_SSL       -4
#define NET_ERR_HTTP      -5
#define NET_ERR_OOM       -6
#define NET_ERR_WRITE     -7
#define NET_ERR_REDIRECT  -8
#define NET_ERR_URL       -9
#define NET_ERR_ABORTED  -10

typedef int (*NetProgressCallback)(long downloaded, long total, void *user);

/*
 * HTTPS helpers backed by devkitPro switch-curl.
 * libcurl handles redirects, buffering, TLS verification and streaming.
 */
unsigned char *net_https_get_url(const char *url, size_t *out_len,
                                 int *out_status, int *out_error);

long net_https_download_url(const char *url, FILE *out,
                            int *out_status, int *out_error,
                            NetProgressCallback progress_cb, void *progress_user);

const char *net_error_string(int error);

#endif

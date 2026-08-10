#ifndef PRELUDE_UPDATER_NET_H
#define PRELUDE_UPDATER_NET_H

#include <switch.h>
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

/*
 * HTTPS helpers for Nintendo Switch/libnx.
 * Both functions initialize and shut down socket + ssl services internally.
 * Redirects (301/302/303/307/308) are followed up to 5 hops.
 */
unsigned char *net_https_get_url(const char *url, size_t *out_len,
                                 int *out_status, int *out_error);

long net_https_download_url(const char *url, FILE *out,
                            int *out_status, int *out_error);

const char *net_error_string(int error);

#endif

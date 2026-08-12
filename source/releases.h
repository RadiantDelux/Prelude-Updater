#ifndef PRELUDE_UPDATER_RELEASES_H
#define PRELUDE_UPDATER_RELEASES_H

#include <stdbool.h>
#include <stddef.h>

#define RELEASE_LIMIT 16

typedef enum {
    RELEASE_TARGET_PRELUDE = 0,
    RELEASE_TARGET_UPDATER
} ReleaseTarget;

typedef struct {
    char tag[32];
    char name[96];
    char download_url[1024];
    long size;
} ReleaseEntry;

typedef struct {
    bool ok;
    size_t count;
    int http_status;
    int net_error;
    char error[160];
    ReleaseEntry entries[RELEASE_LIMIT];
} ReleaseList;

ReleaseList releases_fetch(ReleaseTarget target);
int releases_compare_tags(const char *a, const char *b);
const char *releases_target_name(ReleaseTarget target);
const char *releases_asset_name(ReleaseTarget target);

#endif

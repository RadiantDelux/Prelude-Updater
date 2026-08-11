/*
 * GitHub release discovery for Prelude Updater.
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "releases.h"

#define PRELUDE_RELEASES_URL \
    "https://api.github.com/repos/NextendoNetwork/Prelude-Nro/releases?per_page=20"
#define UPDATER_RELEASES_URL \
    "https://api.github.com/repos/RadiantDelux/Prelude-Updater/releases?per_page=20"

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) ++p;
    return p;
}

static const char *find_key(const char *start, const char *end, const char *key) {
    char needle[96];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return NULL;

    const char *p = start;
    while (p && p < end) {
        p = strstr(p, needle);
        if (!p || p >= end) return NULL;
        return p;
    }
    return NULL;
}

static bool parse_json_string(const char *key_pos, const char *end,
                              char *out, size_t cap) {
    const char *p = strchr(key_pos, ':');
    if (!p || p >= end) return false;
    p = skip_ws(p + 1, end);
    if (p >= end || *p != '"') return false;
    ++p;

    size_t len = 0;
    bool escape = false;
    while (p < end && *p) {
        char c = *p++;
        if (!escape && c == '"') {
            if (!len || len >= cap) return false;
            out[len] = '\0';
            return true;
        }
        if (!escape && c == '\\') {
            escape = true;
            continue;
        }
        if (len + 1 >= cap) return false;
        if (escape) {
            switch (c) {
                case 'n': out[len++] = '\n'; break;
                case 'r': out[len++] = '\r'; break;
                case 't': out[len++] = '\t'; break;
                case '\\':
                case '"':
                case '/': out[len++] = c; break;
                default: return false;
            }
            escape = false;
        } else {
            out[len++] = c;
        }
    }
    return false;
}

static bool parse_json_long(const char *key_pos, const char *end, long *value) {
    const char *p = strchr(key_pos, ':');
    if (!p || p >= end) return false;
    p = skip_ws(p + 1, end);
    if (p >= end) return false;

    char *tail = NULL;
    long parsed = strtol(p, &tail, 10);
    if (tail == p || tail > end) return false;
    *value = parsed;
    return true;
}

static bool parse_json_bool(const char *key_pos, const char *end, bool *value) {
    const char *p = strchr(key_pos, ':');
    if (!p || p >= end) return false;
    p = skip_ws(p + 1, end);
    if (p + 4 <= end && strncmp(p, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (p + 5 <= end && strncmp(p, "false", 5) == 0) {
        *value = false;
        return true;
    }
    return false;
}

static bool parse_semver(const char *tag, int *major, int *minor, int *patch) {
    if (!tag) return false;
    if (*tag == 'v' || *tag == 'V') ++tag;

    char extra = '\0';
    int count = sscanf(tag, "%d.%d.%d%c", major, minor, patch, &extra);
    return count == 3 && *major >= 0 && *minor >= 0 && *patch >= 0;
}

int releases_compare_tags(const char *a, const char *b) {
    int a_major, a_minor, a_patch;
    int b_major, b_minor, b_patch;

    if (!parse_semver(a, &a_major, &a_minor, &a_patch) ||
        !parse_semver(b, &b_major, &b_minor, &b_patch)) {
        return 0;
    }

    if (a_major != b_major) return a_major > b_major ? 1 : -1;
    if (a_minor != b_minor) return a_minor > b_minor ? 1 : -1;
    if (a_patch != b_patch) return a_patch > b_patch ? 1 : -1;
    return 0;
}

const char *releases_target_name(ReleaseTarget target) {
    return target == RELEASE_TARGET_UPDATER ? "Prelude Updater" : "Prelude";
}

const char *releases_asset_name(ReleaseTarget target) {
    return target == RELEASE_TARGET_UPDATER ? "prelude-updater.nro" : "nextendo.nro";
}

static const char *releases_url(ReleaseTarget target) {
    return target == RELEASE_TARGET_UPDATER ? UPDATER_RELEASES_URL : PRELUDE_RELEASES_URL;
}

static const char *next_release(const char *p, const char *end) {
    const char *next = strstr(p, "\"tag_name\"");
    return next && next < end ? next : end;
}

static bool find_asset(const char *start, const char *end, const char *wanted,
                       char *url, size_t url_cap, long *size) {
    const char *cursor = start;
    while (cursor < end) {
        const char *name_key = find_key(cursor, end, "name");
        if (!name_key) return false;

        char name[128];
        if (parse_json_string(name_key, end, name, sizeof(name)) &&
            strcmp(name, wanted) == 0) {
            const char *next_name = find_key(name_key + 6, end, "name");
            const char *asset_end = next_name ? next_name : end;
            const char *url_key = find_key(name_key, asset_end, "browser_download_url");
            const char *size_key = find_key(name_key, asset_end, "size");
            if (!url_key || !size_key) return false;
            if (!parse_json_string(url_key, asset_end, url, url_cap)) return false;
            if (!parse_json_long(size_key, asset_end, size)) return false;
            return *size > 4096;
        }
        cursor = name_key + 6;
    }
    return false;
}

static bool parse_release(const char *start, const char *end, ReleaseTarget target,
                          ReleaseEntry *entry) {
    bool draft = false;
    bool prerelease = false;

    const char *draft_key = find_key(start, end, "draft");
    const char *pre_key = find_key(start, end, "prerelease");
    if (draft_key) parse_json_bool(draft_key, end, &draft);
    if (pre_key) parse_json_bool(pre_key, end, &prerelease);
    if (draft || prerelease) return false;

    const char *tag_key = find_key(start, end, "tag_name");
    if (!tag_key || !parse_json_string(tag_key, end, entry->tag, sizeof(entry->tag))) {
        return false;
    }

    int major, minor, patch;
    if (!parse_semver(entry->tag, &major, &minor, &patch)) return false;

    const char *name_key = find_key(start, end, "name");
    if (!name_key || !parse_json_string(name_key, end, entry->name, sizeof(entry->name))) {
        snprintf(entry->name, sizeof(entry->name), "%s", entry->tag);
    }

    return find_asset(start, end, releases_asset_name(target),
                      entry->download_url, sizeof(entry->download_url), &entry->size);
}

ReleaseList releases_fetch(ReleaseTarget target) {
    ReleaseList list;
    memset(&list, 0, sizeof(list));

    size_t length = 0;
    int status = 0;
    int net_error = NET_ERR_UNKNOWN;
    unsigned char *body = net_https_get_url(releases_url(target), &length, &status, &net_error);

    list.http_status = status;
    list.net_error = net_error;

    if (!body) {
        snprintf(list.error, sizeof(list.error), "Could not query GitHub: %s",
                 net_error_string(net_error));
        return list;
    }
    if (status != 200) {
        snprintf(list.error, sizeof(list.error), "GitHub returned HTTP %d", status);
        free(body);
        return list;
    }

    const char *json = (const char *)body;
    const char *end = json + length;
    const char *cursor = strstr(json, "\"tag_name\"");

    while (cursor && cursor < end && list.count < RELEASE_LIMIT) {
        const char *release_end = next_release(cursor + 10, end);
        ReleaseEntry entry;
        memset(&entry, 0, sizeof(entry));

        if (parse_release(cursor, release_end, target, &entry)) {
            list.entries[list.count++] = entry;
        }

        cursor = release_end < end ? release_end : NULL;
    }

    free(body);

    if (!list.count) {
        snprintf(list.error, sizeof(list.error), "No compatible releases were found.");
        return list;
    }

    list.ok = true;
    return list;
}

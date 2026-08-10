/*
 * Prelude Updater
 * Copyright (C) 2026 RadiantDelux.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "net.h"
#include "updater.h"

#define RELEASE_API_URL "https://api.github.com/repos/NextendoNetwork/Prelude-Nro/releases/latest"
#define RELEASE_URL_FMT "https://github.com/NextendoNetwork/Prelude-Nro/releases/download/%s/nextendo.nro"
#define TMP_PATH         UPDATER_DATA_DIR "/nextendo.nro.new"
#define BACKUP_PATH      UPDATER_DATA_DIR "/nextendo.nro.bak"
#define MIN_NRO_SIZE     4096
#define COPY_BUFFER_SIZE (1024 * 1024)

static bool file_size(const char *path, long *out_size) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *out_size = (long)st.st_size;
    return true;
}

static bool ensure_dirs(void) {
    if (mkdir("sdmc:/switch", 0777) != 0 && errno != EEXIST) return false;
    if (mkdir(UPDATER_DATA_DIR, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

static bool read_state(char *tag, size_t cap) {
    FILE *f = fopen(UPDATER_STATE_PATH, "rb");
    if (!f) return false;
    if (!fgets(tag, (int)cap, f)) { fclose(f); return false; }
    fclose(f);

    tag[strcspn(tag, "\r\n")] = '\0';
    return tag[0] != '\0';
}

static bool write_state(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", UPDATER_STATE_PATH);

    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    if (fprintf(f, "%s\n", tag) < 0) { fclose(f); remove(tmp); return false; }
    if (fflush(f) != 0) { fclose(f); remove(tmp); return false; }
    fclose(f);
    fsdevCommitDevice("sdmc");

    remove(UPDATER_STATE_PATH);
    if (rename(tmp, UPDATER_STATE_PATH) != 0) {
        FILE *src = fopen(tmp, "rb");
        FILE *dst = fopen(UPDATER_STATE_PATH, "wb");
        if (!src || !dst) {
            if (src) fclose(src);
            if (dst) fclose(dst);
            remove(tmp);
            return false;
        }
        char buf[256];
        size_t n;
        bool ok = true;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
            if (fwrite(buf, 1, n, dst) != n) { ok = false; break; }
        }
        fclose(src);
        fclose(dst);
        remove(tmp);
        if (!ok) return false;
    }
    fsdevCommitDevice("sdmc");
    return true;
}

static const char *json_value_string(const char *key_pos) {
    const char *p = strchr(key_pos, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return NULL;
    return p + 1;
}

static bool json_copy_string(const char *key_pos, char *out, size_t cap) {
    const char *p = json_value_string(key_pos);
    if (!p) return false;
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len == 0 || len >= cap) return false;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static bool parse_semver(const char *tag, int *maj, int *min, int *patch) {
    if (!tag) return false;
    if (*tag == 'v' || *tag == 'V') tag++;
    char extra = '\0';
    int n = sscanf(tag, "%d.%d.%d%c", maj, min, patch, &extra);
    return n == 3 && *maj >= 0 && *min >= 0 && *patch >= 0;
}

static int semver_cmp(const char *a, const char *b) {
    int amaj, amin, apat, bmaj, bmin, bpat;
    if (!parse_semver(a, &amaj, &amin, &apat)) return -2;
    if (!parse_semver(b, &bmaj, &bmin, &bpat)) return -2;
    if (amaj != bmaj) return amaj > bmaj ? 1 : -1;
    if (amin != bmin) return amin > bmin ? 1 : -1;
    if (apat != bpat) return apat > bpat ? 1 : -1;
    return 0;
}

static bool parse_release_json(const char *json, char *tag, size_t tag_cap, long *asset_size) {
    const char *tag_key = strstr(json, "\"tag_name\"");
    if (!tag_key || !json_copy_string(tag_key, tag, tag_cap)) return false;

    int maj, min, patch;
    if (!parse_semver(tag, &maj, &min, &patch)) return false;

    const char *p = json;
    const char *asset_name_pos = NULL;
    while ((p = strstr(p, "\"name\"")) != NULL) {
        char name[128];
        if (json_copy_string(p, name, sizeof(name)) && strcmp(name, "nextendo.nro") == 0) {
            asset_name_pos = p;
            break;
        }
        p += 6;
    }
    if (!asset_name_pos) return false;

    const char *size_key = strstr(asset_name_pos, "\"size\"");
    const char *next_name = strstr(asset_name_pos + 6, "\"name\"");
    if (!size_key || (next_name && size_key > next_name)) return false;

    const char *colon = strchr(size_key, ':');
    if (!colon) return false;
    long size = strtol(colon + 1, NULL, 10);
    if (size < MIN_NRO_SIZE) return false;

    *asset_size = size;
    return true;
}

static bool copy_file(const char *src_path, const char *dst_path) {
    FILE *src = fopen(src_path, "rb");
    if (!src) return false;
    FILE *dst = fopen(dst_path, "wb");
    if (!dst) { fclose(src); return false; }

    unsigned char *buf = (unsigned char *)malloc(COPY_BUFFER_SIZE);
    if (!buf) { fclose(src); fclose(dst); return false; }
    setvbuf(src, NULL, _IOFBF, COPY_BUFFER_SIZE);
    setvbuf(dst, NULL, _IOFBF, COPY_BUFFER_SIZE);

    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, COPY_BUFFER_SIZE, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = false; break; }
    }
    if (ferror(src)) ok = false;
    if (fflush(dst) != 0) ok = false;

    free(buf);
    fclose(src);
    fclose(dst);
    fsdevCommitDevice("sdmc");
    return ok;
}

UpdateInfo updater_check(void) {
    UpdateInfo info;
    memset(&info, 0, sizeof(info));
    strcpy(info.installed_tag, "unknown");

    info.target_exists = file_size(PRELUDE_TARGET_PATH, &info.local_size);
    info.installed_version_known = read_state(info.installed_tag, sizeof(info.installed_tag));

    size_t json_len = 0;
    int status = 0;
    int net_error = NET_ERR_UNKNOWN;
    unsigned char *body = net_https_get_url(RELEASE_API_URL, &json_len, &status, &net_error);
    info.http_status = status;
    info.net_error = net_error;

    if (!body) {
        snprintf(info.error, sizeof(info.error), "Could not query GitHub: %s", net_error_string(net_error));
        return info;
    }
    if (status != 200) {
        snprintf(info.error, sizeof(info.error), "GitHub returned HTTP %d", status);
        free(body);
        return info;
    }

    long remote_size = 0;
    if (!parse_release_json((const char *)body, info.latest_tag, sizeof(info.latest_tag), &remote_size)) {
        snprintf(info.error, sizeof(info.error), "Could not parse the release or nextendo.nro is missing");
        free(body);
        return info;
    }
    free(body);

    info.remote_size = remote_size;
    int n = snprintf(info.download_url, sizeof(info.download_url), RELEASE_URL_FMT, info.latest_tag);
    if (n <= 0 || (size_t)n >= sizeof(info.download_url)) {
        snprintf(info.error, sizeof(info.error), "Download URL is too long");
        return info;
    }

    if (!info.target_exists) {
        info.update_available = true;
    } else if (!info.installed_version_known) {
        info.update_available = true;
    } else {
        int cmp = semver_cmp(info.latest_tag, info.installed_tag);
        if (cmp == -2) {
            info.update_available = true;
        } else if (cmp > 0) {
            info.update_available = true;
        } else if (cmp == 0 && info.local_size != info.remote_size) {
            info.update_available = true;
        } else {
            info.update_available = false;
        }
    }

    info.ok = true;
    return info;
}

typedef struct {
    UpdaterProgressCallback cb;
    void *user;
    long expected;
} ProgressBridge;

static int progress_bridge(long downloaded, long total, void *user) {
    ProgressBridge *bridge = (ProgressBridge *)user;
    if (!bridge || !bridge->cb) return 0;
    long effective_total = total > 0 ? total : bridge->expected;
    return bridge->cb(downloaded, effective_total, bridge->user) ? 0 : 1;
}

UpdateResult updater_install(const UpdateInfo *info,
                             UpdaterProgressCallback progress_cb,
                             void *progress_user) {
    if (!info || !info->ok || !info->latest_tag[0] || !info->download_url[0]) return UPDATE_ERR_HTTP;
    if (!ensure_dirs()) return UPDATE_ERR_SD;

    remove(TMP_PATH);
    FILE *out = fopen(TMP_PATH, "wb");
    if (!out) return UPDATE_ERR_SD;
    setvbuf(out, NULL, _IOFBF, COPY_BUFFER_SIZE);

    ProgressBridge bridge = { progress_cb, progress_user, info->remote_size };
    int status = 0;
    int net_error = NET_ERR_UNKNOWN;
    long bytes = net_https_download_url(info->download_url, out, &status, &net_error,
                                        progress_cb ? progress_bridge : NULL,
                                        progress_cb ? &bridge : NULL);
    bool flush_ok = fflush(out) == 0;
    fclose(out);
    fsdevCommitDevice("sdmc");

    if (bytes < 0 || net_error != NET_OK) {
        remove(TMP_PATH);
        if (net_error == NET_ERR_ABORTED) return UPDATE_ERR_CANCELLED;
        return net_error == NET_ERR_WRITE ? UPDATE_ERR_SD : UPDATE_ERR_NETWORK;
    }
    if (status != 200) {
        remove(TMP_PATH);
        return UPDATE_ERR_HTTP;
    }
    if (!flush_ok || bytes < MIN_NRO_SIZE || bytes != info->remote_size) {
        remove(TMP_PATH);
        return UPDATE_ERR_SIZE;
    }

    long tmp_size = 0;
    if (!file_size(TMP_PATH, &tmp_size) || tmp_size != info->remote_size) {
        remove(TMP_PATH);
        return UPDATE_ERR_SIZE;
    }

    /*
     * Fast path: move files inside the same FAT filesystem instead of copying
     * 20+ MiB two extra times. Some FAT implementations/cards reject rename(),
     * so every move has a safe copy fallback.
     */
    bool had_target = false;
    bool backup_was_move = false;
    long old_size = 0;
    if (file_size(PRELUDE_TARGET_PATH, &old_size)) {
        had_target = true;
        remove(BACKUP_PATH);
        if (rename(PRELUDE_TARGET_PATH, BACKUP_PATH) == 0) {
            backup_was_move = true;
        } else if (!copy_file(PRELUDE_TARGET_PATH, BACKUP_PATH)) {
            remove(TMP_PATH);
            remove(BACKUP_PATH);
            return UPDATE_ERR_BACKUP;
        }
    }

    /* If backup was copied, remove old target now that recovery is guaranteed. */
    if (had_target && !backup_was_move) {
        if (remove(PRELUDE_TARGET_PATH) != 0 && errno != ENOENT) {
            remove(TMP_PATH);
            return UPDATE_ERR_INSTALL;
        }
        fsdevCommitDevice("sdmc");
    }

    bool installed_by_move = false;
    if (rename(TMP_PATH, PRELUDE_TARGET_PATH) == 0) {
        installed_by_move = true;
    } else if (!copy_file(TMP_PATH, PRELUDE_TARGET_PATH)) {
        remove(PRELUDE_TARGET_PATH);
        if (had_target) {
            if (rename(BACKUP_PATH, PRELUDE_TARGET_PATH) != 0)
                copy_file(BACKUP_PATH, PRELUDE_TARGET_PATH);
        }
        remove(TMP_PATH);
        return UPDATE_ERR_INSTALL;
    }

    long installed_size = 0;
    if (!file_size(PRELUDE_TARGET_PATH, &installed_size) || installed_size != info->remote_size) {
        remove(PRELUDE_TARGET_PATH);
        if (had_target) {
            if (rename(BACKUP_PATH, PRELUDE_TARGET_PATH) != 0)
                copy_file(BACKUP_PATH, PRELUDE_TARGET_PATH);
        }
        if (!installed_by_move) remove(TMP_PATH);
        return UPDATE_ERR_INSTALL;
    }

    fsdevCommitDevice("sdmc");

    if (!write_state(info->latest_tag)) {
        if (!installed_by_move) remove(TMP_PATH);
        remove(BACKUP_PATH);
        fsdevCommitDevice("sdmc");
        return UPDATE_ERR_STATE;
    }

    if (!installed_by_move) remove(TMP_PATH);
    remove(BACKUP_PATH);
    fsdevCommitDevice("sdmc");
    return UPDATE_OK;
}

const char *updater_result_string(UpdateResult result) {
    switch (result) {
        case UPDATE_OK: return "Update installed successfully.";
        case UPDATE_ERR_NETWORK: return "Network failure during the download.";
        case UPDATE_ERR_HTTP: return "GitHub returned an unexpected response.";
        case UPDATE_ERR_SIZE: return "The download is incomplete or does not match the published asset.";
        case UPDATE_ERR_SD: return "Could not write to the SD card.";
        case UPDATE_ERR_BACKUP: return "Could not create a backup of the currently installed Prelude.";
        case UPDATE_ERR_INSTALL: return "Could not replace nextendo.nro; recovery of the previous copy was attempted.";
        case UPDATE_ERR_STATE: return "Prelude was updated, but the installed version state could not be saved.";
        case UPDATE_ERR_CANCELLED: return "Download cancelled.";
        default: return "Unknown error.";
    }
}

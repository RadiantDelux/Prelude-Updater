/*
 * Release installer for Prelude and Prelude Updater.
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <switch.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "net.h"
#include "settings.h"
#include "updater.h"
#include "version.h"

#define COPY_BUFFER_SIZE (1024 * 1024)
#define MIN_NRO_SIZE 4096L

typedef struct {
    UpdaterProgressCallback callback;
    void *user;
    long expected;
} ProgressBridge;

static bool file_size(const char *path, long *out_size) {
    struct stat st;
    if (!path || stat(path, &st) != 0) return false;
    if (out_size) *out_size = (long)st.st_size;
    return true;
}

static bool read_state(char *tag, size_t cap) {
    FILE *file = fopen(PRELUDE_STATE_PATH, "rb");
    if (!file) return false;

    bool ok = fgets(tag, (int)cap, file) != NULL;
    fclose(file);
    if (!ok) return false;

    tag[strcspn(tag, "\r\n")] = '\0';
    return tag[0] != '\0';
}

static bool write_state(const char *tag) {
    char temp[256];
    snprintf(temp, sizeof(temp), "%s.tmp", PRELUDE_STATE_PATH);

    FILE *file = fopen(temp, "wb");
    if (!file) return false;

    bool ok = fprintf(file, "%s\n", tag) >= 0 && fflush(file) == 0;
    fclose(file);
    if (!ok) {
        remove(temp);
        return false;
    }

    fsdevCommitDevice("sdmc");
    remove(PRELUDE_STATE_PATH);
    if (rename(temp, PRELUDE_STATE_PATH) != 0) {
        remove(temp);
        return false;
    }
    fsdevCommitDevice("sdmc");
    return true;
}

static bool copy_file(const char *source_path, const char *dest_path) {
    FILE *source = fopen(source_path, "rb");
    if (!source) return false;

    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        fclose(source);
        return false;
    }

    unsigned char *buffer = malloc(COPY_BUFFER_SIZE);
    if (!buffer) {
        fclose(source);
        fclose(dest);
        return false;
    }

    setvbuf(source, NULL, _IOFBF, COPY_BUFFER_SIZE);
    setvbuf(dest, NULL, _IOFBF, COPY_BUFFER_SIZE);

    bool ok = true;
    size_t read;
    while ((read = fread(buffer, 1, COPY_BUFFER_SIZE, source)) > 0) {
        if (fwrite(buffer, 1, read, dest) != read) {
            ok = false;
            break;
        }
    }
    if (ferror(source) || fflush(dest) != 0) ok = false;

    free(buffer);
    fclose(source);
    fclose(dest);
    fsdevCommitDevice("sdmc");
    return ok;
}

static bool move_or_copy(const char *source, const char *dest, bool *moved) {
    if (moved) *moved = false;
    remove(dest);

    if (rename(source, dest) == 0) {
        if (moved) *moved = true;
        fsdevCommitDevice("sdmc");
        return true;
    }

    return copy_file(source, dest);
}

static void restore_backup(const char *backup, const char *target) {
    remove(target);
    if (rename(backup, target) != 0) copy_file(backup, target);
    fsdevCommitDevice("sdmc");
}

static int progress_bridge(long downloaded, long total, void *user) {
    ProgressBridge *bridge = user;
    if (!bridge || !bridge->callback) return 0;
    long effective_total = total > 0 ? total : bridge->expected;
    return bridge->callback(downloaded, effective_total, bridge->user) ? 0 : 1;
}

static const char *target_path_for(ReleaseTarget target, const char *self_path) {
    if (target == RELEASE_TARGET_PRELUDE) return PRELUDE_TARGET_PATH;
    if (self_path && strncmp(self_path, "sdmc:/", 6) == 0) return self_path;
    return SELF_TARGET_FALLBACK;
}

static void transaction_paths(ReleaseTarget target, char *temp, size_t temp_cap,
                              char *backup, size_t backup_cap) {
    const char *base = target == RELEASE_TARGET_PRELUDE ? "nextendo" : "prelude-updater";
    snprintf(temp, temp_cap, "%s/%s.nro.new", UPDATER_DATA_DIR, base);
    snprintf(backup, backup_cap, "%s/%s.nro.bak", UPDATER_DATA_DIR, base);
}

bool updater_current_version(ReleaseTarget target,
                             char *tag,
                             size_t tag_cap,
                             bool *target_exists,
                             const char *self_path) {
    if (!tag || !tag_cap) return false;

    const char *path = target_path_for(target, self_path);
    bool exists = file_size(path, NULL);
    if (target_exists) *target_exists = exists;

    if (target == RELEASE_TARGET_UPDATER) {
        snprintf(tag, tag_cap, "%s", PRELUDE_UPDATER_TAG);
        return true;
    }

    if (read_state(tag, tag_cap)) return true;
    snprintf(tag, tag_cap, "%s", exists ? "unknown" : "not installed");
    return false;
}

const char *updater_action_for(const ReleaseEntry *release,
                               const char *current_tag,
                               bool target_exists) {
    if (!release) return "Install";
    if (!target_exists) return "Install";
    if (!current_tag || !*current_tag || strcmp(current_tag, "unknown") == 0) return "Replace";

    int cmp = releases_compare_tags(release->tag, current_tag);
    if (cmp > 0) return "Update";
    if (cmp < 0) return "Downgrade";
    return "Reinstall";
}

UpdateResult updater_install_release(ReleaseTarget target,
                                     const ReleaseEntry *release,
                                     const char *self_path,
                                     UpdaterProgressCallback progress_cb,
                                     void *progress_user) {
    if (!release || !release->download_url[0] || release->size < MIN_NRO_SIZE) {
        return UPDATE_ERR_HTTP;
    }
    if (!settings_prepare_dirs()) return UPDATE_ERR_SD;

    const char *target_path = target_path_for(target, self_path);
    char temp_path[256];
    char backup_path[256];
    transaction_paths(target, temp_path, sizeof(temp_path), backup_path, sizeof(backup_path));

    remove(temp_path);
    FILE *out = fopen(temp_path, "wb");
    if (!out) return UPDATE_ERR_SD;
    setvbuf(out, NULL, _IOFBF, COPY_BUFFER_SIZE);

    ProgressBridge bridge = { progress_cb, progress_user, release->size };
    int status = 0;
    int net_error = NET_ERR_UNKNOWN;
    long bytes = net_https_download_url(release->download_url,
                                        out,
                                        &status,
                                        &net_error,
                                        progress_cb ? progress_bridge : NULL,
                                        progress_cb ? &bridge : NULL);
    bool flushed = fflush(out) == 0;
    fclose(out);
    fsdevCommitDevice("sdmc");

    if (bytes < 0 || net_error != NET_OK) {
        remove(temp_path);
        return net_error == NET_ERR_ABORTED ? UPDATE_ERR_CANCELLED :
               net_error == NET_ERR_WRITE ? UPDATE_ERR_SD : UPDATE_ERR_NETWORK;
    }
    if (status != 200) {
        remove(temp_path);
        return UPDATE_ERR_HTTP;
    }
    if (!flushed || bytes != release->size || bytes < MIN_NRO_SIZE) {
        remove(temp_path);
        return UPDATE_ERR_SIZE;
    }

    long downloaded_size = 0;
    if (!file_size(temp_path, &downloaded_size) || downloaded_size != release->size) {
        remove(temp_path);
        return UPDATE_ERR_SIZE;
    }

    bool had_target = file_size(target_path, NULL);
    bool backup_moved = false;
    if (had_target && !move_or_copy(target_path, backup_path, &backup_moved)) {
        remove(temp_path);
        remove(backup_path);
        return UPDATE_ERR_BACKUP;
    }

    if (had_target && !backup_moved) {
        if (remove(target_path) != 0 && errno != ENOENT) {
            remove(temp_path);
            return UPDATE_ERR_INSTALL;
        }
        fsdevCommitDevice("sdmc");
    }

    bool install_moved = false;
    if (!move_or_copy(temp_path, target_path, &install_moved)) {
        if (had_target) restore_backup(backup_path, target_path);
        remove(temp_path);
        return UPDATE_ERR_INSTALL;
    }

    long installed_size = 0;
    if (!file_size(target_path, &installed_size) || installed_size != release->size) {
        if (had_target) restore_backup(backup_path, target_path);
        else remove(target_path);
        if (!install_moved) remove(temp_path);
        return UPDATE_ERR_INSTALL;
    }

    if (target == RELEASE_TARGET_PRELUDE && !write_state(release->tag)) {
        if (!install_moved) remove(temp_path);
        remove(backup_path);
        return UPDATE_ERR_STATE;
    }

    if (!install_moved) remove(temp_path);
    remove(backup_path);
    fsdevCommitDevice("sdmc");
    return UPDATE_OK;
}

const char *updater_result_string(UpdateResult result) {
    switch (result) {
        case UPDATE_OK: return "Installed successfully.";
        case UPDATE_ERR_NETWORK: return "Network failure during the download.";
        case UPDATE_ERR_HTTP: return "GitHub returned an unexpected response.";
        case UPDATE_ERR_SIZE: return "The downloaded file does not match the published asset.";
        case UPDATE_ERR_SD: return "Could not write to the SD card.";
        case UPDATE_ERR_BACKUP: return "Could not back up the currently installed NRO.";
        case UPDATE_ERR_INSTALL: return "Could not replace the installed NRO; recovery was attempted.";
        case UPDATE_ERR_STATE: return "Installed, but the local version state could not be saved.";
        case UPDATE_ERR_CANCELLED: return "Download cancelled.";
        default: return "Unknown error.";
    }
}

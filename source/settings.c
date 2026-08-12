/*
 * Persistent settings for Prelude Updater.
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <switch.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "settings.h"

static void trim_line(char *line) {
    line[strcspn(line, "\r\n")] = '\0';
}

static void copy_setting(char *dst, size_t cap, const char *value) {
    snprintf(dst, cap, "%s", value ? value : "");
}

bool settings_prepare_dirs(void) {
    if (mkdir("sdmc:/switch", 0777) != 0 && errno != EEXIST) return false;
    if (mkdir(UPDATER_DATA_DIR, 0777) != 0 && errno != EEXIST) return false;
    if (mkdir(UPDATER_MUSIC_DIR, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

void settings_load(AppSettings *settings) {
    if (!settings) return;
    memset(settings, 0, sizeof(*settings));

    FILE *file = fopen(UPDATER_SETTINGS_PATH, "rb");
    if (!file) return;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        trim_line(line);
        char *equals = strchr(line, '=');
        if (!equals) continue;
        *equals++ = '\0';

        if (strcmp(line, "background") == 0) {
            copy_setting(settings->background, sizeof(settings->background), equals);
        } else if (strcmp(line, "finish_sound") == 0) {
            copy_setting(settings->finish_sound, sizeof(settings->finish_sound), equals);
        }
    }

    fclose(file);
}

bool settings_save(const AppSettings *settings) {
    if (!settings || !settings_prepare_dirs()) return false;

    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", UPDATER_SETTINGS_PATH);

    FILE *file = fopen(tmp_path, "wb");
    if (!file) return false;

    bool ok = fprintf(file,
                      "background=%s\n"
                      "finish_sound=%s\n",
                      settings->background,
                      settings->finish_sound) >= 0;
    if (ok) ok = fflush(file) == 0;
    fclose(file);

    if (!ok) {
        remove(tmp_path);
        return false;
    }

    fsdevCommitDevice("sdmc");
    remove(UPDATER_SETTINGS_PATH);
    if (rename(tmp_path, UPDATER_SETTINGS_PATH) != 0) {
        remove(tmp_path);
        return false;
    }
    fsdevCommitDevice("sdmc");
    return true;
}

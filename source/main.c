/*
 * Prelude Updater
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <switch.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "releases.h"
#include "settings.h"
#include "ui.h"
#include "updater.h"

#define FRAME_NS 16666667L

typedef struct {
    PadState *pad;
    const char *eyebrow;
    const char *title;
} ProgressUi;

static void sleep_frame(void) {
    svcSleepThread(FRAME_NS);
}

static u64 buttons_down(PadState *pad) {
    ui_pump();
    padUpdate(pad);
    return padGetButtonsDown(pad);
}

static int wrap_index(int value, int count) {
    if (count <= 0) return 0;
    if (value < 0) return count - 1;
    if (value >= count) return 0;
    return value;
}

static bool wait_back_or_exit(PadState *pad) {
    while (appletMainLoop()) {
        u64 down = buttons_down(pad);
        if (down & HidNpadButton_Plus) return false;
        if (down & (HidNpadButton_B | HidNpadButton_A)) return true;
        sleep_frame();
    }
    return false;
}

static bool confirm_action(PadState *pad, const char *title, const char *message) {
    ui_draw_confirm(title, message);
    while (appletMainLoop()) {
        u64 down = buttons_down(pad);
        if (down & HidNpadButton_A) return true;
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return false;
        sleep_frame();
    }
    return false;
}

static bool progress_callback(long downloaded, long total, void *user) {
    ProgressUi *progress = user;
    if (!progress || !progress->pad) return false;

    u64 down = buttons_down(progress->pad);
    if (down & HidNpadButton_B) return false;
    if (!appletMainLoop()) return false;

    ui_draw_progress(progress->eyebrow, progress->title,
                     downloaded, total, true, false);
    return true;
}

static const char *current_audio_file(const AppSettings *settings, AudioSlot slot) {
    return slot == AUDIO_SLOT_FINISH ? settings->finish_sound : settings->background;
}

static size_t filter_tracks(const AudioCatalog *catalog,
                            AudioSlot slot,
                            const AudioTrack **out,
                            size_t cap) {
    size_t count = 0;
    if (!catalog) return 0;

    for (size_t i = 0; i < catalog->count && count < cap; ++i) {
        if (audio_track_supports(&catalog->tracks[i], slot)) {
            out[count++] = &catalog->tracks[i];
        }
    }
    return count;
}

static bool choose_audio_track(PadState *pad,
                               AppSettings *settings,
                               AudioCatalog *catalog,
                               AudioSlot slot) {
    const AudioTrack *tracks[AUDIO_TRACK_LIMIT];
    size_t count = filter_tracks(catalog, slot, tracks, AUDIO_TRACK_LIMIT);
    int selected = 0;

    const char *current = current_audio_file(settings, slot);
    for (size_t i = 0; i < count; ++i) {
        if (current[0] && strcmp(current, tracks[i]->local_name) == 0) {
            selected = (int)i + 1;
            break;
        }
    }

    while (appletMainLoop()) {
        ui_draw_track_list(slot, tracks, count, selected, current_audio_file(settings, slot));
        u64 down = buttons_down(pad);

        if (down & HidNpadButton_Plus) return false;
        if (down & HidNpadButton_B) return true;
        if (down & HidNpadButton_Up) selected = wrap_index(selected - 1, (int)count + 1);
        if (down & HidNpadButton_Down) selected = wrap_index(selected + 1, (int)count + 1);

        if (down & HidNpadButton_A) {
            if (selected == 0) {
                audio_clear_slot(settings, slot);
                audio_apply_settings(settings);
                continue;
            }

            const AudioTrack *track = tracks[selected - 1];
            ProgressUi progress = { pad, "Downloading audio", track->title };
            ui_draw_progress(progress.eyebrow, progress.title, 0, 0, true, true);

            char error[160];
            if (!audio_install_track(track, progress_callback, &progress, error, sizeof(error))) {
                ui_draw_notice("Audio download failed", error, true);
                if (!wait_back_or_exit(pad)) return false;
                continue;
            }

            if (!audio_select_track(settings, slot, track)) {
                ui_draw_notice("Settings error", "The track was downloaded, but the selection could not be saved.", true);
                if (!wait_back_or_exit(pad)) return false;
                continue;
            }

            audio_apply_settings(settings);
        }

        sleep_frame();
    }
    return false;
}

static bool music_menu(PadState *pad, AppSettings *settings) {
    int selected = 0;
    AudioCatalog catalog;
    memset(&catalog, 0, sizeof(catalog));

    ui_draw_checking("Music & sounds", "Loading catalog from GitHub...");
    catalog = audio_catalog_fetch();

    while (appletMainLoop()) {
        ui_draw_music_menu(settings, selected);
        u64 down = buttons_down(pad);

        if (down & HidNpadButton_Plus) return false;
        if (down & HidNpadButton_B) return true;
        if (down & HidNpadButton_Up) selected = wrap_index(selected - 1, 4);
        if (down & HidNpadButton_Down) selected = wrap_index(selected + 1, 4);

        if (down & HidNpadButton_A) {
            if (selected == 3) return true;
            if (selected == 2) {
                ui_draw_checking("Music & sounds", "Refreshing catalog from GitHub...");
                catalog = audio_catalog_fetch();
                continue;
            }

            if (!catalog.ok) {
                ui_draw_notice("Music catalog unavailable", catalog.error, true);
                if (!wait_back_or_exit(pad)) return false;
                continue;
            }

            AudioSlot slot = selected == 0 ? AUDIO_SLOT_BACKGROUND : AUDIO_SLOT_FINISH;
            if (!choose_audio_track(pad, settings, &catalog, slot)) return false;
        }
        sleep_frame();
    }
    return false;
}

static bool release_menu(PadState *pad,
                         ReleaseTarget target,
                         const char *self_path) {
    ui_draw_checking(releases_target_name(target), "Loading release history from GitHub...");
    ReleaseList releases = releases_fetch(target);
    if (!releases.ok) {
        ui_draw_notice("Could not load releases", releases.error, true);
        return wait_back_or_exit(pad);
    }

    char current_tag[32];
    bool target_exists = false;
    updater_current_version(target, current_tag, sizeof(current_tag), &target_exists, self_path);

    int selected = 0;
    while (appletMainLoop()) {
        ui_draw_release_list(target, &releases, selected, current_tag, target_exists);
        u64 down = buttons_down(pad);

        if (down & HidNpadButton_Plus) return false;
        if (down & HidNpadButton_B) return true;
        if (down & HidNpadButton_Up) selected = wrap_index(selected - 1, (int)releases.count);
        if (down & HidNpadButton_Down) selected = wrap_index(selected + 1, (int)releases.count);

        if (down & HidNpadButton_A) {
            const ReleaseEntry *release = &releases.entries[selected];
            const char *action = updater_action_for(release, current_tag, target_exists);

            char message[256];
            snprintf(message, sizeof(message), "%s %s to %s?",
                     action, releases_target_name(target), release->tag);
            if (!confirm_action(pad, action, message)) continue;

            ProgressUi progress = { pad, action, release->tag };
            ui_draw_progress(action, release->tag, 0, release->size, true, true);
            UpdateResult result = updater_install_release(target,
                                                          release,
                                                          self_path,
                                                          progress_callback,
                                                          &progress);

            if (result == UPDATE_SELF_RESTART) {
                ui_draw_notice("Updater downloaded", "Restarting through hbloader to finish the self-update safely.", false);
                svcSleepThread(600000000L);
                return false;
            }

            if (result == UPDATE_OK || result == UPDATE_ERR_STATE) audio_play_finish();
            ui_draw_operation_result(target, release, action, result);

            if (!wait_back_or_exit(pad)) return false;
            updater_current_version(target, current_tag, sizeof(current_tag),
                                    &target_exists, self_path);
        }
        sleep_frame();
    }
    return false;
}

static int console_fallback(PadState *pad) {
    consoleInit(NULL);
    printf("Prelude Updater\nby RadiantDelux\n\n");
    printf("The graphical interface could not be started.\n");
    printf("Press + to exit.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
        sleep_frame();
    }
    consoleExit(NULL);
    return 0;
}

static int self_update_failure_console(PadState *pad) {
    consoleInit(NULL);
    printf("Prelude Updater\nby RadiantDelux\n\n");
    printf("The staged self-update could not be completed.\n");
    printf("The original updater was preserved when possible.\n\n");
    printf("Press + to return to hbmenu.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
        sleep_frame();
    }
    consoleExit(NULL);
    return 1;
}

int main(int argc, char **argv) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    const char *self_path = (argc > 0 && argv && argv[0]) ? argv[0] : SELF_TARGET_FALLBACK;

    if (strcmp(self_path, SELF_STAGE_PATH) == 0) {
        if (updater_finish_self_update(self_path)) return 0;
        return self_update_failure_console(&pad);
    }

    updater_cleanup_self_update();

    if (!ui_init()) return console_fallback(&pad);

    AppSettings settings;
    settings_prepare_dirs();
    settings_load(&settings);
    audio_init(&settings);

    int selected = 0;
    bool running = true;

    while (running && appletMainLoop()) {
        ui_draw_main_menu(selected);
        u64 down = buttons_down(&pad);

        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_Up) selected = wrap_index(selected - 1, 4);
        if (down & HidNpadButton_Down) selected = wrap_index(selected + 1, 4);

        if (down & HidNpadButton_A) {
            switch (selected) {
                case 0:
                    running = release_menu(&pad, RELEASE_TARGET_PRELUDE, self_path);
                    break;
                case 1:
                    running = release_menu(&pad, RELEASE_TARGET_UPDATER, self_path);
                    break;
                case 2:
                    running = music_menu(&pad, &settings);
                    break;
                case 3:
                    running = false;
                    break;
            }
        }
        sleep_frame();
    }

    audio_exit();
    ui_exit();
    return 0;
}

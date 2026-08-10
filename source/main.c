/*
 * Prelude Updater
 * Copyright (C) 2026 RadiantDelux.
 * AGPL-3.0-or-later
 */

#include <switch.h>
#include <stdbool.h>
#include <stdio.h>

#include "ui.h"
#include "updater.h"

#define FRAME_NS 16666667L

typedef struct {
    PadState *pad;
    const char *tag;
    long expected_total;
} DownloadUiContext;

static void sleep_frame(void) {
    svcSleepThread(FRAME_NS);
}

static bool wait_exit(PadState *pad) {
    while (appletMainLoop()) {
        ui_pump();
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) return true;
        sleep_frame();
    }
    return false;
}

static bool download_progress(long downloaded, long total, void *user) {
    DownloadUiContext *ctx = (DownloadUiContext *)user;
    if (!ctx || !ctx->pad) return true;

    ui_pump();
    padUpdate(ctx->pad);
    if (padGetButtonsDown(ctx->pad) & HidNpadButton_B) return false;
    if (!appletMainLoop()) return false;

    long effective_total = total > 0 ? total : ctx->expected_total;
    ui_draw_download_progress(ctx->tag, downloaded, effective_total, false);
    return true;
}

static int console_fallback(PadState *pad) {
    consoleInit(NULL);
    printf("Prelude Updater\n");
    printf("by RadiantDelux\n\n");
    printf("The graphical interface could not be started.\n");
    printf("Make sure the NRO was built with SDL2 and SDL2_ttf.\n\n");
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

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    if (!ui_init()) return console_fallback(&pad);

    ui_draw_checking();
    UpdateInfo info = updater_check();

    if (!info.ok) {
        ui_draw_error(info.error[0] ? info.error : "Could not query GitHub.", info.http_status);
        wait_exit(&pad);
        ui_exit();
        return 0;
    }

    ui_draw_info(&info);

    bool install = false;
    while (appletMainLoop()) {
        ui_pump();
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus) break;
        if (info.update_available && (down & HidNpadButton_A)) {
            install = true;
            break;
        }
        if (info.update_available && (down & HidNpadButton_B)) break;
        if (!info.update_available && (down & HidNpadButton_X)) {
            install = true;
            break;
        }
        sleep_frame();
    }

    if (!install) {
        ui_exit();
        return 0;
    }

    ui_draw_download_begin(info.latest_tag, info.remote_size);
    DownloadUiContext progress_ctx = { &pad, info.latest_tag, info.remote_size };
    UpdateResult result = updater_install(&info, download_progress, &progress_ctx);

    ui_draw_result(result, info.latest_tag);
    wait_exit(&pad);
    ui_exit();
    return 0;
}

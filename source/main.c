/*
 * Prelude Updater
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
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

static bool wait_for_exit(PadState *pad) {
    while (appletMainLoop()) {
        ui_pump();
        padUpdate(pad);

        if (padGetButtonsDown(pad) & HidNpadButton_Plus) return true;
        sleep_frame();
    }
    return false;
}

static bool on_download_progress(long downloaded, long total, void *user) {
    DownloadUiContext *ctx = user;
    if (!ctx || !ctx->pad) return true;

    ui_pump();
    padUpdate(ctx->pad);

    if (!appletMainLoop()) return false;
    if (padGetButtonsDown(ctx->pad) & HidNpadButton_B) return false;

    long effective_total = total > 0 ? total : ctx->expected_total;
    ui_draw_download_progress(ctx->tag, downloaded, effective_total, false);
    return true;
}

static int run_console_fallback(PadState *pad) {
    consoleInit(NULL);
    printf("Prelude Updater\n");
    printf("by RadiantDelux\n\n");
    printf("Unable to start the graphical interface.\n\n");
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

static bool wait_for_action(PadState *pad, const UpdateInfo *info) {
    while (appletMainLoop()) {
        ui_pump();
        padUpdate(pad);

        u64 down = padGetButtonsDown(pad);
        if (down & HidNpadButton_Plus) return false;
        if (info->update_available && (down & HidNpadButton_B)) return false;
        if (info->update_available && (down & HidNpadButton_A)) return true;
        if (!info->update_available && (down & HidNpadButton_X)) return true;

        sleep_frame();
    }

    return false;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    if (!ui_init()) return run_console_fallback(&pad);

    ui_draw_checking();
    UpdateInfo info = updater_check();

    if (!info.ok) {
        ui_draw_error(info.error[0] ? info.error : "Could not query GitHub.", info.http_status);
        wait_for_exit(&pad);
        ui_exit();
        return 0;
    }

    ui_draw_info(&info);
    if (!wait_for_action(&pad, &info)) {
        ui_exit();
        return 0;
    }

    ui_draw_download_begin(info.latest_tag, info.remote_size);

    DownloadUiContext progress = {
        .pad = &pad,
        .tag = info.latest_tag,
        .expected_total = info.remote_size,
    };

    UpdateResult result = updater_install(&info, on_download_progress, &progress);
    ui_draw_result(result, info.latest_tag);
    wait_for_exit(&pad);

    ui_exit();
    return 0;
}

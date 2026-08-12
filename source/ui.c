/*
 * Prelude Updater UI
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "version.h"

#define SCREEN_W 1280
#define SCREEN_H 720
#define LOGO_PATH "romfs:/logo.bmp"
#define LIST_ROWS 7

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *logo;
    TTF_Font *xl, *lg, *md, *sm, *key;
    PlFontData font_data;
    bool pl_ready, romfs_ready, ready;
    Uint32 progress_tick, speed_tick;
    long speed_bytes;
    double speed_mib;
} UiState;

static UiState ui;

static const SDL_Color BG = {14,16,24,255};
static const SDL_Color PANEL = {25,28,40,255};
static const SDL_Color PANEL_2 = {31,35,49,255};
static const SDL_Color SELECTED = {47,50,70,255};
static const SDL_Color KEY_DARK = {62,68,88,255};
static const SDL_Color TEXT = {242,244,250,255};
static const SDL_Color MUTED = {158,166,188,255};
static const SDL_Color ACCENT = {119,101,255,255};
static const SDL_Color GREEN = {84,214,150,255};
static const SDL_Color YELLOW = {246,194,81,255};
static const SDL_Color RED = {245,102,115,255};
static const SDL_Color WHITE = {255,255,255,255};

static void color(SDL_Color c) {
    SDL_SetRenderDrawColor(ui.renderer, c.r, c.g, c.b, c.a);
}

static void circle(int cx, int cy, int r, SDL_Color c) {
    color(c);
    for (int y = -r; y <= r; ++y) {
        int x = (int)sqrt((double)(r * r - y * y));
        SDL_RenderDrawLine(ui.renderer, cx - x, cy + y, cx + x, cy + y);
    }
}

static void round_rect(int x, int y, int w, int h, int r, SDL_Color c) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    color(c);
    SDL_Rect a = {x + r, y, w - r * 2, h};
    SDL_Rect b = {x, y + r, w, h - r * 2};
    SDL_RenderFillRect(ui.renderer, &a);
    SDL_RenderFillRect(ui.renderer, &b);
    circle(x + r, y + r, r, c);
    circle(x + w - r - 1, y + r, r, c);
    circle(x + r, y + h - r - 1, r, c);
    circle(x + w - r - 1, y + h - r - 1, r, c);
}

static SDL_Surface *text_surface(TTF_Font *font, const char *text, SDL_Color c) {
    return font && text && *text ? TTF_RenderUTF8_Blended(font, text, c) : NULL;
}

static void blit(SDL_Surface *surface, int x, int y) {
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(ui.renderer, surface);
    if (texture) {
        SDL_Rect dst = {x, y, surface->w, surface->h};
        SDL_RenderCopy(ui.renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void text(TTF_Font *font, const char *value, int x, int y, SDL_Color c) {
    blit(text_surface(font, value, c), x, y);
}

static void text_right(TTF_Font *font, const char *value, int right, int y, SDL_Color c) {
    SDL_Surface *s = text_surface(font, value, c);
    if (!s) return;
    blit(s, right - s->w, y);
}

static void text_center(TTF_Font *font, const char *value, int cx, int y, SDL_Color c) {
    SDL_Surface *s = text_surface(font, value, c);
    if (!s) return;
    blit(s, cx - s->w / 2, y);
}

static void text_center_xy(TTF_Font *font, const char *value, int cx, int cy, SDL_Color c) {
    SDL_Surface *s = text_surface(font, value, c);
    if (!s) return;
    blit(s, cx - s->w / 2, cy - s->h / 2);
}

static void text_wrapped(const char *value, int y) {
    if (!value || !*value) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended_Wrapped(ui.lg, value, TEXT, 940);
    if (!s) return;
    blit(s, 640 - s->w / 2, y);
}

static TTF_Font *system_font(int size) {
    SDL_RWops *rw = SDL_RWFromConstMem(ui.font_data.address, (int)ui.font_data.size);
    return rw ? TTF_OpenFontRW(rw, 1, size) : NULL;
}

static bool inside_round(int x, int y, int w, int h, int r) {
    if ((x >= r && x < w - r) || (y >= r && y < h - r)) return true;
    int cx = x < r ? r : w - r - 1;
    int cy = y < r ? r : h - r - 1;
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static SDL_Texture *load_logo(void) {
    SDL_Surface *src = SDL_LoadBMP(LOGO_PATH);
    if (!src) return NULL;
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(src);
    if (!rgba) return NULL;

    int r = (rgba->w < rgba->h ? rgba->w : rgba->h) / 5;
    if (!SDL_MUSTLOCK(rgba) || SDL_LockSurface(rgba) == 0) {
        for (int y = 0; y < rgba->h; ++y) {
            Uint32 *row = (Uint32 *)((Uint8 *)rgba->pixels + y * rgba->pitch);
            for (int x = 0; x < rgba->w; ++x) {
                if (inside_round(x, y, rgba->w, rgba->h, r)) continue;
                Uint8 rr, gg, bb, aa;
                SDL_GetRGBA(row[x], rgba->format, &rr, &gg, &bb, &aa);
                row[x] = SDL_MapRGBA(rgba->format, rr, gg, bb, 0);
            }
        }
        if (SDL_MUSTLOCK(rgba)) SDL_UnlockSurface(rgba);
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ui.renderer, rgba);
    SDL_FreeSurface(rgba);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

static void logo(int x, int y, int size) {
    round_rect(x - 4, y - 4, size + 8, size + 8, size / 4, PANEL_2);
    if (ui.logo) {
        SDL_Rect dst = {x, y, size, size};
        SDL_RenderCopy(ui.renderer, ui.logo, NULL, &dst);
    } else {
        round_rect(x, y, size, size, size / 5, ACCENT);
        text_center_xy(ui.lg, "P", x + size / 2, y + size / 2, WHITE);
    }
}

static void frame_begin(void) {
    color(BG);
    SDL_RenderClear(ui.renderer);
    color(ACCENT);
    SDL_Rect top = {0, 0, SCREEN_W, 5};
    SDL_RenderFillRect(ui.renderer, &top);
}

static void frame_end(void) {
    SDL_RenderPresent(ui.renderer);
}

static void header(const char *subtitle) {
    logo(64, 34, 76);
    text(ui.lg, "Prelude Updater", 164, 37, TEXT);
    text(ui.sm, subtitle ? subtitle : "Version manager", 166, 78, MUTED);
    round_rect(1062, 37, 154, 34, 17, PANEL_2);
    text_center_xy(ui.sm, PRELUDE_UPDATER_TAG, 1139, 54, TEXT);
    text_right(ui.sm, "by " PRELUDE_UPDATER_AUTHOR, 1214, 82, MUTED);
}

static void button(int x, const char *key, const char *label, SDL_Color c) {
    const int cy = 665;
    circle(x, cy, 18, c);
    if (strcmp(key, "+") == 0) {
        color(WHITE);
        SDL_Rect h = {x - 8, cy - 2, 16, 4};
        SDL_Rect v = {x - 2, cy - 8, 4, 16};
        SDL_RenderFillRect(ui.renderer, &h);
        SDL_RenderFillRect(ui.renderer, &v);
    } else {
        text_center_xy(ui.key, key, x, cy, WHITE);
    }
    text(ui.sm, label, x + 31, 651, MUTED);
}

static void menu_row(int y, const char *title, const char *detail, bool selected) {
    round_rect(96, y, 1088, 86, 20, selected ? SELECTED : PANEL_2);
    if (selected) round_rect(96, y, 6, 86, 3, ACCENT);
    text(ui.md, title, 128, y + 13, TEXT);
    if (detail && *detail) text(ui.sm, detail, 128, y + 49, MUTED);
}

static int list_start(int selected, int count) {
    if (count <= LIST_ROWS) return 0;
    int start = selected - LIST_ROWS / 2;
    if (start < 0) start = 0;
    if (start > count - LIST_ROWS) start = count - LIST_ROWS;
    return start;
}

bool ui_init(void) {
    memset(&ui, 0, sizeof(ui));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 || TTF_Init() != 0) return false;
    if (R_FAILED(plInitialize(PlServiceType_User))) return false;
    ui.pl_ready = true;
    if (R_FAILED(plGetSharedFontByType(&ui.font_data, PlSharedFontType_Standard))) return false;
    ui.romfs_ready = R_SUCCEEDED(romfsInit());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    ui.window = SDL_CreateWindow("Prelude Updater", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!ui.window) return false;
    ui.renderer = SDL_CreateRenderer(ui.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui.renderer) return false;
    SDL_RenderSetLogicalSize(ui.renderer, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(ui.renderer, SDL_BLENDMODE_BLEND);

    ui.xl = system_font(48);
    ui.lg = system_font(32);
    ui.md = system_font(24);
    ui.sm = system_font(19);
    ui.key = system_font(18);
    if (!ui.xl || !ui.lg || !ui.md || !ui.sm || !ui.key) return false;

    if (ui.romfs_ready) ui.logo = load_logo();
    ui.ready = true;
    return true;
}

void ui_exit(void) {
    if (ui.logo) SDL_DestroyTexture(ui.logo);
    if (ui.xl) TTF_CloseFont(ui.xl);
    if (ui.lg) TTF_CloseFont(ui.lg);
    if (ui.md) TTF_CloseFont(ui.md);
    if (ui.sm) TTF_CloseFont(ui.sm);
    if (ui.key) TTF_CloseFont(ui.key);
    if (ui.renderer) SDL_DestroyRenderer(ui.renderer);
    if (ui.window) SDL_DestroyWindow(ui.window);
    if (ui.romfs_ready) romfsExit();
    if (ui.pl_ready) plExit();
    TTF_Quit();
    SDL_Quit();
    memset(&ui, 0, sizeof(ui));
}

void ui_pump(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
}

void ui_draw_main_menu(int selected) {
    static const char *title[] = {"Prelude versions", "Updater versions", "Music & sounds", "Exit"};
    static const char *detail[] = {
        "Update, downgrade or reinstall Nextendo Prelude",
        "Update, downgrade or reinstall Prelude Updater",
        "Choose background music and update notification sounds",
        "Return to hbmenu"
    };
    frame_begin();
    header("Version manager and music settings");
    round_rect(64, 132, 1152, 455, 28, PANEL);
    for (int i = 0; i < 4; ++i) menu_row(160 + i * 98, title[i], detail[i], selected == i);
    button(88, "A", "Select", GREEN);
    button(1080, "+", "Exit", KEY_DARK);
    frame_end();
}

void ui_draw_checking(const char *title, const char *subtitle) {
    frame_begin();
    header(title ? title : "Loading");
    round_rect(64, 138, 1152, 450, 28, PANEL);
    logo(574, 188, 132);
    text_center(ui.lg, subtitle ? subtitle : "Connecting to GitHub...", 640, 365, TEXT);
    text_center(ui.sm, "Please keep the console connected to the internet.", 640, 413, MUTED);
    frame_end();
}

void ui_draw_release_list(ReleaseTarget target, const ReleaseList *list, int selected,
                          const char *current_tag, bool target_exists) {
    char subtitle[128];
    snprintf(subtitle, sizeof(subtitle), "%s releases - installed: %s",
             releases_target_name(target), current_tag ? current_tag : "unknown");
    frame_begin();
    header(subtitle);
    round_rect(64, 132, 1152, 455, 28, PANEL);

    int count = (int)list->count;
    int start = list_start(selected, count);
    for (int row = 0; row < LIST_ROWS; ++row) {
        int index = start + row;
        if (index >= count) break;
        const ReleaseEntry *release = &list->entries[index];
        const char *action = updater_action_for(release, current_tag, target_exists);
        int y = 154 + row * 58;
        round_rect(92, y, 1096, 50, 14, index == selected ? SELECTED : PANEL_2);
        if (index == selected) round_rect(92, y, 5, 50, 3, ACCENT);
        text(ui.md, release->tag, 120, y + 9, TEXT);
        text_right(ui.sm, action, 1152, y + 13, strcmp(action, "Downgrade") == 0 ? YELLOW : MUTED);
    }
    button(88, "A", "Install selected", GREEN);
    button(320, "B", "Back", RED);
    button(1080, "+", "Exit", KEY_DARK);
    frame_end();
}

void ui_draw_music_menu(const AppSettings *settings, int selected) {
    const char *title[] = {"Background music", "Start sound", "Completion sound", "Refresh catalog", "Back"};
    const char *detail[] = {
        settings->background[0] ? settings->background : "Off",
        settings->start_sound[0] ? settings->start_sound : "Off",
        settings->finish_sound[0] ? settings->finish_sound : "Off",
        "Reload music/catalog.txt from GitHub",
        "Return to the main menu"
    };
    frame_begin();
    header("Music & sounds");
    round_rect(64, 132, 1152, 455, 28, PANEL);
    for (int i = 0; i < 5; ++i) menu_row(148 + i * 82, title[i], detail[i], selected == i);
    button(88, "A", "Select", GREEN);
    button(320, "B", "Back", RED);
    button(1080, "+", "Exit", KEY_DARK);
    frame_end();
}

void ui_draw_track_list(AudioSlot slot, const AudioTrack *const *tracks, size_t track_count,
                        int selected, const char *current_file) {
    frame_begin();
    header(audio_slot_name(slot));
    round_rect(64, 132, 1152, 455, 28, PANEL);

    int total = (int)track_count + 1;
    int start = list_start(selected, total);
    for (int row = 0; row < LIST_ROWS; ++row) {
        int item = start + row;
        if (item >= total) break;
        int y = 154 + row * 58;
        round_rect(92, y, 1096, 50, 14, item == selected ? SELECTED : PANEL_2);
        if (item == selected) round_rect(92, y, 5, 50, 3, ACCENT);
        if (item == 0) {
            text(ui.md, "Off", 120, y + 9, TEXT);
            if (!current_file || !*current_file) text_right(ui.sm, "Selected", 1152, y + 13, GREEN);
        } else {
            const AudioTrack *track = tracks[item - 1];
            text(ui.md, track->title, 120, y + 9, TEXT);
            text(ui.sm, track->artist, 530, y + 13, MUTED);
            if (current_file && strcmp(current_file, track->local_name) == 0)
                text_right(ui.sm, "Selected", 1152, y + 13, GREEN);
        }
    }
    button(88, "A", "Choose", GREEN);
    button(320, "B", "Back", RED);
    button(1080, "+", "Exit", KEY_DARK);
    frame_end();
}

void ui_draw_progress(const char *eyebrow, const char *title, long downloaded, long total,
                      bool cancellable, bool force) {
    Uint32 now = SDL_GetTicks();
    if (!force && ui.progress_tick && now - ui.progress_tick < 100) return;
    if (!ui.speed_tick || force) {
        ui.speed_tick = now;
        ui.speed_bytes = downloaded;
        ui.speed_mib = 0.0;
    } else if (now - ui.speed_tick >= 350) {
        Uint32 ms = now - ui.speed_tick;
        long delta = downloaded - ui.speed_bytes;
        if (delta >= 0 && ms) ui.speed_mib = ((double)delta / 1048576.0) / ((double)ms / 1000.0);
        ui.speed_tick = now;
        ui.speed_bytes = downloaded;
    }
    ui.progress_tick = now;

    double ratio = total > 0 ? (double)downloaded / (double)total : 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;

    frame_begin();
    header(eyebrow ? eyebrow : "Downloading");
    round_rect(64, 132, 1152, 455, 28, PANEL);
    text(ui.xl, title ? title : "Download", 110, 204, TEXT);

    char pct[32], detail[128], speed[64];
    snprintf(pct, sizeof(pct), "%d%%", (int)(ratio * 100.0 + 0.5));
    text_right(ui.xl, pct, 1160, 204, TEXT);
    round_rect(110, 318, 1050, 34, 17, PANEL_2);
    int width = (int)(1050.0 * ratio);
    if (width > 0) round_rect(110, 318, width, 34, 17, ACCENT);

    if (total > 0) snprintf(detail, sizeof(detail), "%.1f / %.1f MiB", downloaded / 1048576.0, total / 1048576.0);
    else snprintf(detail, sizeof(detail), "%.1f MiB downloaded", downloaded / 1048576.0);
    text(ui.md, detail, 110, 385, TEXT);
    if (ui.speed_mib > 0.01) snprintf(speed, sizeof(speed), "%.2f MiB/s", ui.speed_mib);
    else snprintf(speed, sizeof(speed), "Connecting...");
    text_right(ui.md, speed, 1160, 385, MUTED);
    if (cancellable) button(88, "B", "Cancel", RED);
    frame_end();
}

void ui_draw_operation_result(ReleaseTarget target, const ReleaseEntry *release,
                              const char *action, UpdateResult result) {
    bool ok = result == UPDATE_OK || result == UPDATE_ERR_STATE;
    frame_begin();
    header(ok ? "Operation complete" : "Operation failed");
    round_rect(64, 132, 1152, 455, 28, PANEL);
    circle(640, 270, 62, ok ? GREEN : (result == UPDATE_ERR_CANCELLED ? YELLOW : RED));
    text_center_xy(ui.xl, ok ? "OK" : "!", 640, 271, BG);

    char message[200];
    if (ok) snprintf(message, sizeof(message), "%s %s to %s", releases_target_name(target), action, release->tag);
    else snprintf(message, sizeof(message), "%s", updater_result_string(result));
    text_wrapped(message, 360);
    if (ok && target == RELEASE_TARGET_UPDATER)
        text_center(ui.sm, "Restart the app to run the selected updater version.", 640, 430, MUTED);
    else if (ok)
        text_center(ui.sm, "/switch/nextendo.nro is ready.", 640, 430, MUTED);
    button(1080, "+", "Continue", KEY_DARK);
    frame_end();
}

void ui_draw_confirm(const char *title, const char *message) {
    frame_begin();
    header(title ? title : "Confirm");
    round_rect(64, 132, 1152, 455, 28, PANEL);
    text_wrapped(message, 275);
    button(88, "A", "Confirm", GREEN);
    button(320, "B", "Cancel", RED);
    button(1080, "+", "Exit", KEY_DARK);
    frame_end();
}

void ui_draw_notice(const char *title, const char *message, bool back_allowed) {
    frame_begin();
    header(title ? title : "Prelude Updater");
    round_rect(64, 132, 1152, 455, 28, PANEL);
    text_wrapped(message, 275);
    if (back_allowed) button(88, "B", "Back", RED);
    button(1080, "+", back_allowed ? "Exit" : "Continue", KEY_DARK);
    frame_end();
}

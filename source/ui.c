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

#define SCREEN_W 1280
#define SCREEN_H 720
#define APP_VERSION "1.2.0"
#define APP_AUTHOR  "RadiantDelux"
#define LOGO_PATH   "romfs:/logo.bmp"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *logo;
    TTF_Font *font_xl;
    TTF_Font *font_lg;
    TTF_Font *font_md;
    TTF_Font *font_sm;
    TTF_Font *font_key;
    PlFontData font_data;
    bool pl_ready;
    bool romfs_ready;
    bool ready;
    Uint32 progress_last_draw;
    Uint32 speed_last_tick;
    long speed_last_bytes;
    double speed_mib;
} UiState;

static UiState ui;

static const SDL_Color BG       = { 14, 16, 24, 255 };
static const SDL_Color PANEL    = { 25, 28, 40, 255 };
static const SDL_Color PANEL_2  = { 31, 35, 49, 255 };
static const SDL_Color KEY_DARK = { 62, 68, 88, 255 };
static const SDL_Color TEXT     = { 242, 244, 250, 255 };
static const SDL_Color MUTED    = { 158, 166, 188, 255 };
static const SDL_Color ACCENT   = { 119, 101, 255, 255 };
static const SDL_Color ACCENT_2 = { 88, 183, 255, 255 };
static const SDL_Color GREEN    = { 84, 214, 150, 255 };
static const SDL_Color YELLOW   = { 246, 194, 81, 255 };
static const SDL_Color RED      = { 245, 102, 115, 255 };
static const SDL_Color WHITE    = { 255, 255, 255, 255 };

static void set_color(SDL_Color color) {
    SDL_SetRenderDrawColor(ui.renderer, color.r, color.g, color.b, color.a);
}

static void fill_circle(int cx, int cy, int radius, SDL_Color color) {
    set_color(color);
    for (int y = -radius; y <= radius; ++y) {
        int x = (int)sqrt((double)(radius * radius - y * y));
        SDL_RenderDrawLine(ui.renderer, cx - x, cy + y, cx + x, cy + y);
    }
}

static void draw_circle_outline(int cx, int cy, int radius, SDL_Color color) {
    const double step = 3.14159265358979323846 / 180.0;
    set_color(color);
    for (int angle = 0; angle < 360; ++angle) {
        double radians = angle * step;
        SDL_RenderDrawPoint(ui.renderer,
                            cx + (int)(cos(radians) * radius),
                            cy + (int)(sin(radians) * radius));
    }
}

static void fill_round_rect(int x, int y, int w, int h, int radius, SDL_Color color) {
    if (w <= 0 || h <= 0) return;

    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    set_color(color);
    if (radius <= 0) {
        SDL_Rect rect = { x, y, w, h };
        SDL_RenderFillRect(ui.renderer, &rect);
        return;
    }

    SDL_Rect horizontal = { x + radius, y, w - 2 * radius, h };
    SDL_Rect vertical = { x, y + radius, w, h - 2 * radius };
    SDL_RenderFillRect(ui.renderer, &horizontal);
    SDL_RenderFillRect(ui.renderer, &vertical);

    fill_circle(x + radius, y + radius, radius, color);
    fill_circle(x + w - radius - 1, y + radius, radius, color);
    fill_circle(x + radius, y + h - radius - 1, radius, color);
    fill_circle(x + w - radius - 1, y + h - radius - 1, radius, color);
}

static SDL_Surface *make_text_surface(TTF_Font *font, const char *text, SDL_Color color) {
    if (!font || !text || !*text) return NULL;
    return TTF_RenderUTF8_Blended(font, text, color);
}

static void blit_text_surface(SDL_Surface *surface, int x, int y) {
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ui.renderer, surface);
    if (texture) {
        SDL_Rect dst = { x, y, surface->w, surface->h };
        SDL_RenderCopy(ui.renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void draw_text(TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    blit_text_surface(make_text_surface(font, text, color), x, y);
}

static void draw_text_right(TTF_Font *font, const char *text, int right_x, int y, SDL_Color color) {
    SDL_Surface *surface = make_text_surface(font, text, color);
    if (!surface) return;
    blit_text_surface(surface, right_x - surface->w, y);
}

static void draw_text_center(TTF_Font *font, const char *text, int center_x, int y, SDL_Color color) {
    SDL_Surface *surface = make_text_surface(font, text, color);
    if (!surface) return;
    blit_text_surface(surface, center_x - surface->w / 2, y);
}

static void draw_text_center_xy(TTF_Font *font, const char *text, int center_x, int center_y,
                                SDL_Color color) {
    SDL_Surface *surface = make_text_surface(font, text, color);
    if (!surface) return;
    blit_text_surface(surface,
                      center_x - surface->w / 2,
                      center_y - surface->h / 2);
}

static void draw_text_wrapped_center(TTF_Font *font, const char *text, int center_x, int y,
                                     int max_width, SDL_Color color) {
    if (!font || !text || !*text) return;

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, text, color, (Uint32)max_width);
    if (!surface) return;
    blit_text_surface(surface, center_x - surface->w / 2, y);
}

static TTF_Font *open_system_font(int size) {
    SDL_RWops *rw = SDL_RWFromConstMem(ui.font_data.address, (int)ui.font_data.size);
    return rw ? TTF_OpenFontRW(rw, 1, size) : NULL;
}

static bool inside_rounded_rect(int x, int y, int w, int h, int radius) {
    if ((x >= radius && x < w - radius) ||
        (y >= radius && y < h - radius)) {
        return true;
    }

    int cx = x < radius ? radius : w - radius - 1;
    int cy = y < radius ? radius : h - radius - 1;
    int dx = x - cx;
    int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

static bool round_surface_corners(SDL_Surface *surface) {
    if (!surface) return false;

    int radius = (surface->w < surface->h ? surface->w : surface->h) / 5;
    if (radius < 1) return true;

    if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) return false;

    for (int y = 0; y < surface->h; ++y) {
        Uint32 *row = (Uint32 *)((Uint8 *)surface->pixels + y * surface->pitch);
        for (int x = 0; x < surface->w; ++x) {
            if (inside_rounded_rect(x, y, surface->w, surface->h, radius)) continue;

            Uint8 r, g, b, a;
            SDL_GetRGBA(row[x], surface->format, &r, &g, &b, &a);
            row[x] = SDL_MapRGBA(surface->format, r, g, b, 0);
        }
    }

    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    return true;
}

static SDL_Texture *load_logo(void) {
    SDL_Surface *source = SDL_LoadBMP(LOGO_PATH);
    if (!source) return NULL;

    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(source);
    if (!rgba) return NULL;

    round_surface_corners(rgba);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(ui.renderer, rgba);
    SDL_FreeSurface(rgba);

    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

static bool load_fonts(void) {
    ui.font_xl = open_system_font(48);
    ui.font_lg = open_system_font(32);
    ui.font_md = open_system_font(24);
    ui.font_sm = open_system_font(19);
    ui.font_key = open_system_font(18);
    return ui.font_xl && ui.font_lg && ui.font_md && ui.font_sm && ui.font_key;
}

static void draw_logo(int x, int y, int size) {
    int frame_radius = size / 4;
    fill_round_rect(x - 4, y - 4, size + 8, size + 8, frame_radius, PANEL_2);

    if (ui.logo) {
        SDL_Rect dst = { x, y, size, size };
        SDL_RenderCopy(ui.renderer, ui.logo, NULL, &dst);
        return;
    }

    fill_round_rect(x, y, size, size, size / 5, ACCENT);
    draw_text_center_xy(ui.font_lg, "P", x + size / 2, y + size / 2, WHITE);
}

static void begin_frame(void) {
    set_color(BG);
    SDL_RenderClear(ui.renderer);

    SDL_Rect left = { 0, 0, SCREEN_W / 2, 5 };
    SDL_Rect right = { SCREEN_W / 2, 0, SCREEN_W / 2, 5 };
    set_color(ACCENT);
    SDL_RenderFillRect(ui.renderer, &left);
    set_color(ACCENT_2);
    SDL_RenderFillRect(ui.renderer, &right);
}

static void end_frame(void) {
    SDL_RenderPresent(ui.renderer);
}

static void draw_header(const char *subtitle) {
    draw_logo(64, 34, 76);
    draw_text(ui.font_lg, "Prelude Updater", 164, 37, TEXT);
    draw_text(ui.font_sm, subtitle ? subtitle : "Updater for Prelude", 166, 78, MUTED);

    fill_round_rect(1062, 37, 154, 34, 17, PANEL_2);
    draw_text_center_xy(ui.font_sm, "v" APP_VERSION, 1139, 54, TEXT);
    draw_text_right(ui.font_sm, "by " APP_AUTHOR, 1214, 82, MUTED);
}

static void draw_plus_glyph(int cx, int cy) {
    SDL_Rect horizontal = { cx - 8, cy - 2, 16, 4 };
    SDL_Rect vertical = { cx - 2, cy - 8, 4, 16 };
    set_color(WHITE);
    SDL_RenderFillRect(ui.renderer, &horizontal);
    SDL_RenderFillRect(ui.renderer, &vertical);
}

static void draw_footer_button(int x, const char *key, const char *label, SDL_Color key_color) {
    const int cy = 665;

    fill_circle(x, cy, 18, key_color);
    if (key && strcmp(key, "+") == 0) {
        draw_circle_outline(x, cy, 18, MUTED);
        draw_plus_glyph(x, cy);
    } else {
        draw_text_center_xy(ui.font_key, key, x, cy, WHITE);
    }
    draw_text(ui.font_sm, label, x + 31, 651, MUTED);
}

static void draw_version_box(int x, int y, const char *label, const char *value, SDL_Color accent) {
    fill_round_rect(x, y, 500, 112, 22, PANEL_2);
    fill_round_rect(x + 18, y + 18, 5, 76, 3, accent);
    draw_text(ui.font_sm, label, x + 42, y + 18, MUTED);
    draw_text(ui.font_lg, value, x + 42, y + 48, TEXT);
}

static void format_size(long bytes, char *out, size_t cap) {
    if (bytes < 0) {
        snprintf(out, cap, "--");
    } else if (bytes >= 1024L * 1024L) {
        snprintf(out, cap, "%.1f MiB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024L) {
        snprintf(out, cap, "%.1f KiB", (double)bytes / 1024.0);
    } else {
        snprintf(out, cap, "%ld B", bytes);
    }
}

bool ui_init(void) {
    memset(&ui, 0, sizeof(ui));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return false;
    if (TTF_Init() != 0) {
        SDL_Quit();
        return false;
    }

    if (R_FAILED(plInitialize(PlServiceType_User))) {
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    ui.pl_ready = true;

    if (R_FAILED(plGetSharedFontByType(&ui.font_data, PlSharedFontType_Standard))) {
        ui_exit();
        return false;
    }

    ui.romfs_ready = R_SUCCEEDED(romfsInit());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    ui.window = SDL_CreateWindow("Prelude Updater",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 SCREEN_W,
                                 SCREEN_H,
                                 SDL_WINDOW_FULLSCREEN);
    if (!ui.window) {
        ui_exit();
        return false;
    }

    ui.renderer = SDL_CreateRenderer(ui.window, -1,
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui.renderer) {
        ui_exit();
        return false;
    }

    SDL_RenderSetLogicalSize(ui.renderer, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(ui.renderer, SDL_BLENDMODE_BLEND);

    if (!load_fonts()) {
        ui_exit();
        return false;
    }

    if (ui.romfs_ready) ui.logo = load_logo();
    ui.ready = true;
    return true;
}

void ui_exit(void) {
    if (ui.logo) SDL_DestroyTexture(ui.logo);
    ui.logo = NULL;

    if (ui.font_xl) TTF_CloseFont(ui.font_xl);
    if (ui.font_lg) TTF_CloseFont(ui.font_lg);
    if (ui.font_md) TTF_CloseFont(ui.font_md);
    if (ui.font_sm) TTF_CloseFont(ui.font_sm);
    if (ui.font_key) TTF_CloseFont(ui.font_key);
    ui.font_xl = NULL;
    ui.font_lg = NULL;
    ui.font_md = NULL;
    ui.font_sm = NULL;
    ui.font_key = NULL;

    if (ui.renderer) SDL_DestroyRenderer(ui.renderer);
    if (ui.window) SDL_DestroyWindow(ui.window);
    ui.renderer = NULL;
    ui.window = NULL;

    if (ui.romfs_ready) romfsExit();
    if (ui.pl_ready) plExit();
    ui.romfs_ready = false;
    ui.pl_ready = false;
    ui.ready = false;

    TTF_Quit();
    SDL_Quit();
}

void ui_pump(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
}

void ui_draw_checking(void) {
    if (!ui.ready) return;

    begin_frame();
    draw_header("Checking GitHub Releases");
    fill_round_rect(64, 138, 1152, 450, 28, PANEL);
    draw_logo(574, 188, 132);
    draw_text_center(ui.font_lg, "Checking for the latest version", 640, 365, TEXT);
    draw_text_center(ui.font_sm, "Connecting securely to GitHub...", 640, 413, MUTED);
    fill_round_rect(515, 474, 250, 7, 3, PANEL_2);
    fill_round_rect(515, 474, 122, 7, 3, ACCENT);
    end_frame();
}

void ui_draw_info(const UpdateInfo *info) {
    if (!ui.ready || !info) return;

    begin_frame();
    draw_header(info->update_available ? "A new Prelude release is available" : "Prelude is up to date");
    fill_round_rect(64, 132, 1152, 455, 28, PANEL);

    const char *local = info->target_exists ? info->installed_tag : "Not installed";
    draw_version_box(90, 176, "INSTALLED VERSION", local, ACCENT_2);
    draw_version_box(690, 176, "LATEST VERSION", info->latest_tag, ACCENT);

    char remote_size[64];
    char local_size[64];
    format_size(info->remote_size, remote_size, sizeof(remote_size));
    format_size(info->local_size, local_size, sizeof(local_size));

    draw_text(ui.font_sm, "Target file", 112, 326, MUTED);
    draw_text(ui.font_md, "/switch/nextendo.nro", 112, 354, TEXT);
    draw_text(ui.font_sm, "Download size", 760, 326, MUTED);
    draw_text(ui.font_md, remote_size, 760, 354, TEXT);

    if (info->target_exists) {
        draw_text(ui.font_sm, "Local size", 112, 410, MUTED);
        draw_text(ui.font_md, local_size, 112, 438, TEXT);
    }

    SDL_Color status_color = info->update_available ? YELLOW : GREEN;
    const char *status = info->update_available ? "UPDATE AVAILABLE" : "UP TO DATE";
    fill_round_rect(760, 419, 330, 48, 18, status_color);
    draw_text_center_xy(ui.font_sm, status, 925, 443, BG);

    if (info->update_available) {
        draw_text(ui.font_sm,
                  "The new NRO is downloaded and verified before the installed copy is replaced.",
                  112, 516, MUTED);
        draw_footer_button(88, "A", "Update", GREEN);
        draw_footer_button(236, "B", "Cancel", RED);
    } else {
        draw_text(ui.font_sm,
                  "Everything is current. You can reinstall the latest release to verify the local file.",
                  112, 516, MUTED);
        draw_footer_button(88, "X", "Reinstall", ACCENT_2);
    }

    draw_footer_button(1080, "+", "Exit", KEY_DARK);
    end_frame();
}

void ui_draw_download_begin(const char *tag, long total_bytes) {
    ui.progress_last_draw = 0;
    ui.speed_last_tick = SDL_GetTicks();
    ui.speed_last_bytes = 0;
    ui.speed_mib = 0.0;
    ui_draw_download_progress(tag, 0, total_bytes, true);
}

void ui_draw_download_progress(const char *tag, long downloaded, long total_bytes, bool force) {
    if (!ui.ready) return;

    Uint32 now = SDL_GetTicks();
    if (!force && ui.progress_last_draw && now - ui.progress_last_draw < 100) return;

    if (ui.speed_last_tick && now > ui.speed_last_tick && now - ui.speed_last_tick >= 350) {
        long delta_bytes = downloaded - ui.speed_last_bytes;
        Uint32 delta_ms = now - ui.speed_last_tick;
        if (delta_bytes >= 0 && delta_ms > 0) {
            ui.speed_mib = ((double)delta_bytes / (1024.0 * 1024.0)) /
                           ((double)delta_ms / 1000.0);
        }
        ui.speed_last_bytes = downloaded;
        ui.speed_last_tick = now;
    }
    ui.progress_last_draw = now;

    double ratio = total_bytes > 0 ? (double)downloaded / (double)total_bytes : 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    int percent = (int)(ratio * 100.0 + 0.5);

    begin_frame();
    draw_header("Downloading from GitHub");
    fill_round_rect(64, 132, 1152, 455, 28, PANEL);

    draw_text(ui.font_sm, "INSTALLING", 110, 180, MUTED);
    draw_text(ui.font_xl, tag && *tag ? tag : "Prelude", 110, 211, TEXT);

    char pct[32];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    draw_text_right(ui.font_xl, pct, 1160, 211, TEXT);

    fill_round_rect(110, 318, 1050, 34, 17, PANEL_2);
    int progress_w = (int)(1050.0 * ratio);
    if (progress_w > 0) fill_round_rect(110, 318, progress_w, 34, 17, ACCENT);

    char current[64];
    char total[64];
    char detail[160];
    format_size(downloaded, current, sizeof(current));
    format_size(total_bytes, total, sizeof(total));
    snprintf(detail, sizeof(detail), "%s of %s", current, total);
    draw_text(ui.font_md, detail, 110, 385, TEXT);

    char speed[64];
    if (ui.speed_mib > 0.01) {
        snprintf(speed, sizeof(speed), "%.2f MiB/s", ui.speed_mib);
    } else {
        snprintf(speed, sizeof(speed), "Connecting...");
    }
    draw_text_right(ui.font_md, speed, 1160, 385, MUTED);

    draw_text(ui.font_sm,
              "Do not remove the SD card during installation. You can cancel while downloading.",
              110, 470, MUTED);
    draw_footer_button(88, "B", "Cancel download", RED);
    end_frame();
}

void ui_draw_installing(const char *tag) {
    if (!ui.ready) return;

    begin_frame();
    draw_header("Verifying and installing");
    fill_round_rect(64, 132, 1152, 455, 28, PANEL);
    draw_logo(578, 190, 124);
    draw_text_center(ui.font_lg, "Applying the update", 640, 365, TEXT);
    draw_text_center(ui.font_md, tag ? tag : "Prelude", 640, 407, ACCENT_2);
    draw_text_center(ui.font_sm,
                     "Verifying the file, creating a backup and replacing nextendo.nro...",
                     640, 460, MUTED);
    end_frame();
}

void ui_draw_result(UpdateResult result, const char *tag) {
    if (!ui.ready) return;

    bool ok = result == UPDATE_OK || result == UPDATE_ERR_STATE;
    SDL_Color accent = ok ? GREEN : (result == UPDATE_ERR_CANCELLED ? YELLOW : RED);

    begin_frame();
    draw_header(ok ? "Update complete" : "The operation did not complete");
    fill_round_rect(64, 132, 1152, 455, 28, PANEL);
    fill_circle(640, 275, 62, accent);
    draw_text_center_xy(ui.font_xl, ok ? "OK" : "!", 640, 276, BG);

    if (ok) {
        char title[96];
        snprintf(title, sizeof(title), "Prelude %s installed", tag ? tag : "");
        draw_text_center(ui.font_lg, title, 640, 367, TEXT);
    } else {
        draw_text_wrapped_center(ui.font_lg, updater_result_string(result), 640, 357, 900, TEXT);
    }

    draw_text_wrapped_center(ui.font_sm,
                             ok ? "/switch/nextendo.nro is ready."
                                : "The previous Prelude copy is preserved whenever recovery is possible.",
                             640, 430, 920, MUTED);
    draw_footer_button(1080, "+", "Exit", KEY_DARK);
    end_frame();
}

void ui_draw_error(const char *message, int http_status) {
    if (!ui.ready) return;

    begin_frame();
    draw_header("Could not check for updates");
    fill_round_rect(64, 132, 1152, 455, 28, PANEL);
    fill_circle(640, 272, 62, RED);
    draw_text_center_xy(ui.font_xl, "!", 640, 273, BG);
    draw_text_wrapped_center(ui.font_lg, message ? message : "Unknown error", 640, 357, 900, TEXT);

    if (http_status) {
        char http[64];
        snprintf(http, sizeof(http), "HTTP %d", http_status);
        draw_text_center(ui.font_sm, http, 640, 438, MUTED);
    }

    draw_footer_button(1080, "+", "Exit", KEY_DARK);
    end_frame();
}

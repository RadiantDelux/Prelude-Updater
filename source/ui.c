/*
 * Prelude Updater graphical UI.
 * Copyright (C) 2026 RadiantDelux.
 * AGPL-3.0-or-later
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

static UiState g_ui;

static const SDL_Color C_BG        = { 14, 16, 24, 255 };
static const SDL_Color C_PANEL     = { 25, 28, 40, 255 };
static const SDL_Color C_PANEL_2   = { 31, 35, 49, 255 };
static const SDL_Color C_KEY_DARK  = { 62, 68, 88, 255 };
static const SDL_Color C_TEXT      = { 242, 244, 250, 255 };
static const SDL_Color C_MUTED     = { 158, 166, 188, 255 };
static const SDL_Color C_ACCENT    = { 119, 101, 255, 255 };
static const SDL_Color C_ACCENT_2  = { 88, 183, 255, 255 };
static const SDL_Color C_GREEN     = { 84, 214, 150, 255 };
static const SDL_Color C_YELLOW    = { 246, 194, 81, 255 };
static const SDL_Color C_RED       = { 245, 102, 115, 255 };
static const SDL_Color C_WHITE     = { 255, 255, 255, 255 };

static void set_color(SDL_Color c) {
    SDL_SetRenderDrawColor(g_ui.renderer, c.r, c.g, c.b, c.a);
}

static void fill_circle(int cx, int cy, int radius, SDL_Color color) {
    set_color(color);
    for (int y = -radius; y <= radius; y++) {
        int x = (int)sqrt((double)(radius * radius - y * y));
        SDL_RenderDrawLine(g_ui.renderer, cx - x, cy + y, cx + x, cy + y);
    }
}

static void draw_circle_outline(int cx, int cy, int radius, SDL_Color color) {
    set_color(color);
    for (int a = 0; a < 360; a++) {
        double r = (double)a * 3.14159265358979323846 / 180.0;
        int x = cx + (int)(cos(r) * radius);
        int y = cy + (int)(sin(r) * radius);
        SDL_RenderDrawPoint(g_ui.renderer, x, y);
    }
}

static void fill_round_rect(int x, int y, int w, int h, int r, SDL_Color color) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) {
        SDL_Rect rect = {x, y, w, h};
        set_color(color);
        SDL_RenderFillRect(g_ui.renderer, &rect);
        return;
    }

    SDL_Rect mid = {x + r, y, w - 2 * r, h};
    SDL_Rect side = {x, y + r, w, h - 2 * r};
    set_color(color);
    SDL_RenderFillRect(g_ui.renderer, &mid);
    SDL_RenderFillRect(g_ui.renderer, &side);
    fill_circle(x + r, y + r, r, color);
    fill_circle(x + w - r - 1, y + r, r, color);
    fill_circle(x + r, y + h - r - 1, r, color);
    fill_circle(x + w - r - 1, y + h - r - 1, r, color);
}

static void draw_text(TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    if (!font || !text || !*text) return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(g_ui.renderer, surface);
    if (texture) {
        SDL_Rect dst = {x, y, surface->w, surface->h};
        SDL_RenderCopy(g_ui.renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void draw_text_right(TTF_Font *font, const char *text, int right_x, int y, SDL_Color color) {
    int w = 0, h = 0;
    if (font && text && TTF_SizeUTF8(font, text, &w, &h) == 0)
        draw_text(font, text, right_x - w, y, color);
}

static void draw_text_center(TTF_Font *font, const char *text, int center_x, int y, SDL_Color color) {
    int w = 0, h = 0;
    if (font && text && TTF_SizeUTF8(font, text, &w, &h) == 0)
        draw_text(font, text, center_x - w / 2, y, color);
}

/* Center by the rendered glyph surface, not by a guessed baseline. */
static void draw_text_center_xy(TTF_Font *font, const char *text, int center_x, int center_y, SDL_Color color) {
    if (!font || !text || !*text) return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(g_ui.renderer, surface);
    if (texture) {
        SDL_Rect dst = {
            center_x - surface->w / 2,
            center_y - surface->h / 2,
            surface->w,
            surface->h
        };
        SDL_RenderCopy(g_ui.renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void draw_text_wrapped_center(TTF_Font *font, const char *text, int center_x, int y,
                                     int max_width, SDL_Color color) {
    if (!font || !text || !*text) return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, text, color, (Uint32)max_width);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(g_ui.renderer, surface);
    if (texture) {
        SDL_Rect dst = {center_x - surface->w / 2, y, surface->w, surface->h};
        SDL_RenderCopy(g_ui.renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static TTF_Font *open_system_font(int size) {
    SDL_RWops *rw = SDL_RWFromConstMem(g_ui.font_data.address, (int)g_ui.font_data.size);
    if (!rw) return NULL;
    return TTF_OpenFontRW(rw, 1, size);
}

static void draw_logo(int x, int y, int size) {
    fill_round_rect(x - 4, y - 4, size + 8, size + 8, 23, C_PANEL_2);
    if (g_ui.logo) {
        SDL_Rect dst = {x, y, size, size};
        SDL_RenderCopy(g_ui.renderer, g_ui.logo, NULL, &dst);
    } else {
        fill_round_rect(x, y, size, size, 19, C_ACCENT);
        draw_text_center_xy(g_ui.font_lg, "P", x + size / 2, y + size / 2, C_WHITE);
    }
}

static void begin_frame(void) {
    set_color(C_BG);
    SDL_RenderClear(g_ui.renderer);

    set_color(C_ACCENT);
    SDL_Rect a = {0, 0, SCREEN_W / 2, 5};
    SDL_RenderFillRect(g_ui.renderer, &a);
    set_color(C_ACCENT_2);
    SDL_Rect b = {SCREEN_W / 2, 0, SCREEN_W / 2, 5};
    SDL_RenderFillRect(g_ui.renderer, &b);
}

static void end_frame(void) {
    SDL_RenderPresent(g_ui.renderer);
}

static void draw_header(const char *eyebrow) {
    draw_logo(64, 34, 76);

    draw_text(g_ui.font_lg, "Prelude Updater", 164, 37, C_TEXT);
    draw_text(g_ui.font_sm, eyebrow ? eyebrow : "Updater for Prelude", 166, 78, C_MUTED);

    fill_round_rect(1062, 37, 154, 34, 17, C_PANEL_2);
    draw_text_center_xy(g_ui.font_sm, "v" APP_VERSION, 1139, 54, C_TEXT);
    draw_text_right(g_ui.font_sm, "by " APP_AUTHOR, 1214, 82, C_MUTED);
}

static void draw_plus_glyph(int cx, int cy) {
    SDL_Rect h = {cx - 8, cy - 2, 16, 4};
    SDL_Rect v = {cx - 2, cy - 8, 4, 16};
    set_color(C_WHITE);
    SDL_RenderFillRect(g_ui.renderer, &h);
    SDL_RenderFillRect(g_ui.renderer, &v);
}

static void draw_footer_button(int x, const char *key, const char *label, SDL_Color key_color) {
    const int cy = 665;
    fill_circle(x, cy, 18, key_color);
    if (key && strcmp(key, "+") == 0) {
        draw_circle_outline(x, cy, 18, C_MUTED);
        draw_plus_glyph(x, cy);
    } else {
        draw_text_center_xy(g_ui.font_key, key, x, cy + 1, C_WHITE);
    }
    draw_text(g_ui.font_sm, label, x + 31, 651, C_MUTED);
}

static void draw_version_box(int x, int y, const char *label, const char *value, SDL_Color accent) {
    fill_round_rect(x, y, 500, 112, 22, C_PANEL_2);
    fill_round_rect(x + 18, y + 18, 5, 76, 3, accent);
    draw_text(g_ui.font_sm, label, x + 42, y + 18, C_MUTED);
    draw_text(g_ui.font_lg, value, x + 42, y + 48, C_TEXT);
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
    memset(&g_ui, 0, sizeof(g_ui));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return false;
    if (TTF_Init() != 0) { SDL_Quit(); return false; }

    if (R_FAILED(plInitialize(PlServiceType_User))) {
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    g_ui.pl_ready = true;

    if (R_FAILED(plGetSharedFontByType(&g_ui.font_data, PlSharedFontType_Standard))) {
        ui_exit();
        return false;
    }

    if (R_SUCCEEDED(romfsInit())) g_ui.romfs_ready = true;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    g_ui.window = SDL_CreateWindow("Prelude Updater", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!g_ui.window) { ui_exit(); return false; }

    g_ui.renderer = SDL_CreateRenderer(g_ui.window, -1,
                                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ui.renderer) { ui_exit(); return false; }
    SDL_RenderSetLogicalSize(g_ui.renderer, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(g_ui.renderer, SDL_BLENDMODE_BLEND);

    g_ui.font_xl = open_system_font(48);
    g_ui.font_lg = open_system_font(32);
    g_ui.font_md = open_system_font(24);
    g_ui.font_sm = open_system_font(19);
    g_ui.font_key = open_system_font(18);
    if (!g_ui.font_xl || !g_ui.font_lg || !g_ui.font_md || !g_ui.font_sm || !g_ui.font_key) {
        ui_exit();
        return false;
    }

    if (g_ui.romfs_ready) {
        SDL_Surface *logo_surface = SDL_LoadBMP(LOGO_PATH);
        if (logo_surface) {
            g_ui.logo = SDL_CreateTextureFromSurface(g_ui.renderer, logo_surface);
            SDL_FreeSurface(logo_surface);
        }
    }

    g_ui.ready = true;
    return true;
}

void ui_exit(void) {
    if (g_ui.logo) SDL_DestroyTexture(g_ui.logo);
    g_ui.logo = NULL;

    if (g_ui.font_xl) TTF_CloseFont(g_ui.font_xl);
    if (g_ui.font_lg) TTF_CloseFont(g_ui.font_lg);
    if (g_ui.font_md) TTF_CloseFont(g_ui.font_md);
    if (g_ui.font_sm) TTF_CloseFont(g_ui.font_sm);
    if (g_ui.font_key) TTF_CloseFont(g_ui.font_key);
    g_ui.font_xl = g_ui.font_lg = g_ui.font_md = g_ui.font_sm = g_ui.font_key = NULL;

    if (g_ui.renderer) SDL_DestroyRenderer(g_ui.renderer);
    if (g_ui.window) SDL_DestroyWindow(g_ui.window);
    g_ui.renderer = NULL;
    g_ui.window = NULL;

    if (g_ui.romfs_ready) romfsExit();
    g_ui.romfs_ready = false;
    if (g_ui.pl_ready) plExit();
    g_ui.pl_ready = false;
    TTF_Quit();
    SDL_Quit();
    g_ui.ready = false;
}

void ui_pump(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* libnx PadState owns controller input; SDL events are only pumped. */
    }
}

void ui_draw_checking(void) {
    if (!g_ui.ready) return;
    begin_frame();
    draw_header("Checking GitHub Releases");

    fill_round_rect(64, 138, 1152, 450, 28, C_PANEL);
    draw_logo(574, 188, 132);
    draw_text_center(g_ui.font_lg, "Checking for the latest version", 640, 365, C_TEXT);
    draw_text_center(g_ui.font_sm, "Connecting securely to GitHub...", 640, 413, C_MUTED);

    fill_round_rect(515, 474, 250, 7, 3, C_PANEL_2);
    fill_round_rect(515, 474, 122, 7, 3, C_ACCENT);
    end_frame();
}

void ui_draw_info(const UpdateInfo *info) {
    if (!g_ui.ready || !info) return;
    begin_frame();
    draw_header(info->update_available ? "A new Prelude release is available" : "Prelude is up to date");

    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);

    const char *local = info->target_exists ? info->installed_tag : "Not installed";
    draw_version_box(90, 176, "INSTALLED VERSION", local, C_ACCENT_2);
    draw_version_box(690, 176, "LATEST VERSION", info->latest_tag, C_ACCENT);

    char remote_size[64], local_size[64];
    format_size(info->remote_size, remote_size, sizeof(remote_size));
    format_size(info->local_size, local_size, sizeof(local_size));

    draw_text(g_ui.font_sm, "Target file", 112, 326, C_MUTED);
    draw_text(g_ui.font_md, "/switch/nextendo.nro", 112, 354, C_TEXT);
    draw_text(g_ui.font_sm, "Download size", 760, 326, C_MUTED);
    draw_text(g_ui.font_md, remote_size, 760, 354, C_TEXT);

    if (info->target_exists) {
        draw_text(g_ui.font_sm, "Local size", 112, 410, C_MUTED);
        draw_text(g_ui.font_md, local_size, 112, 438, C_TEXT);
    }

    SDL_Color status_color = info->update_available ? C_YELLOW : C_GREEN;
    const char *status = info->update_available ? "UPDATE AVAILABLE" : "UP TO DATE";
    fill_round_rect(760, 419, 330, 48, 18, status_color);
    draw_text_center_xy(g_ui.font_sm, status, 925, 443, C_BG);

    if (info->update_available) {
        draw_text(g_ui.font_sm, "The new NRO is downloaded and verified before the installed copy is replaced.",
                  112, 516, C_MUTED);
        draw_footer_button(88, "A", "Update", C_GREEN);
        draw_footer_button(236, "B", "Cancel", C_RED);
    } else {
        draw_text(g_ui.font_sm, "Everything is current. You can reinstall the latest release to verify the local file.",
                  112, 516, C_MUTED);
        draw_footer_button(88, "X", "Reinstall", C_ACCENT_2);
    }
    draw_footer_button(1080, "+", "Exit", C_KEY_DARK);
    end_frame();
}

void ui_draw_download_begin(const char *tag, long total_bytes) {
    g_ui.progress_last_draw = 0;
    g_ui.speed_last_tick = SDL_GetTicks();
    g_ui.speed_last_bytes = 0;
    g_ui.speed_mib = 0.0;
    ui_draw_download_progress(tag, 0, total_bytes, true);
}

void ui_draw_download_progress(const char *tag, long downloaded, long total_bytes, bool force) {
    if (!g_ui.ready) return;
    Uint32 now = SDL_GetTicks();
    if (!force && g_ui.progress_last_draw && now - g_ui.progress_last_draw < 100) return;

    if (g_ui.speed_last_tick && now > g_ui.speed_last_tick && now - g_ui.speed_last_tick >= 350) {
        long delta_b = downloaded - g_ui.speed_last_bytes;
        Uint32 delta_ms = now - g_ui.speed_last_tick;
        if (delta_b >= 0 && delta_ms > 0)
            g_ui.speed_mib = ((double)delta_b / (1024.0 * 1024.0)) / ((double)delta_ms / 1000.0);
        g_ui.speed_last_bytes = downloaded;
        g_ui.speed_last_tick = now;
    }
    g_ui.progress_last_draw = now;

    double ratio = (total_bytes > 0) ? (double)downloaded / (double)total_bytes : 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    int percent = (int)(ratio * 100.0 + 0.5);

    begin_frame();
    draw_header("Downloading from GitHub");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);

    draw_text(g_ui.font_sm, "INSTALLING", 110, 180, C_MUTED);
    draw_text(g_ui.font_xl, tag && *tag ? tag : "Prelude", 110, 211, C_TEXT);

    char pct[32];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    draw_text_right(g_ui.font_xl, pct, 1160, 211, C_TEXT);

    fill_round_rect(110, 318, 1050, 34, 17, C_PANEL_2);
    int progress_w = (int)(1050.0 * ratio);
    if (progress_w > 0) fill_round_rect(110, 318, progress_w, 34, 17, C_ACCENT);

    char current[64], total[64], detail[160];
    format_size(downloaded, current, sizeof(current));
    format_size(total_bytes, total, sizeof(total));
    snprintf(detail, sizeof(detail), "%s of %s", current, total);
    draw_text(g_ui.font_md, detail, 110, 385, C_TEXT);

    char speed[64];
    if (g_ui.speed_mib > 0.01) snprintf(speed, sizeof(speed), "%.2f MiB/s", g_ui.speed_mib);
    else snprintf(speed, sizeof(speed), "Connecting...");
    draw_text_right(g_ui.font_md, speed, 1160, 385, C_MUTED);

    draw_text(g_ui.font_sm, "Do not remove the SD card during installation. You can cancel while downloading.",
              110, 470, C_MUTED);
    draw_footer_button(88, "B", "Cancel download", C_RED);
    end_frame();
}

void ui_draw_installing(const char *tag) {
    if (!g_ui.ready) return;
    begin_frame();
    draw_header("Verifying and installing");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);
    draw_logo(578, 190, 124);
    draw_text_center(g_ui.font_lg, "Applying the update", 640, 365, C_TEXT);
    draw_text_center(g_ui.font_md, tag ? tag : "Prelude", 640, 407, C_ACCENT_2);
    draw_text_center(g_ui.font_sm, "Verifying the file, creating a backup and replacing nextendo.nro...", 640, 460, C_MUTED);
    end_frame();
}

void ui_draw_result(UpdateResult result, const char *tag) {
    if (!g_ui.ready) return;
    bool ok = result == UPDATE_OK || result == UPDATE_ERR_STATE;
    SDL_Color accent = ok ? C_GREEN : (result == UPDATE_ERR_CANCELLED ? C_YELLOW : C_RED);

    begin_frame();
    draw_header(ok ? "Update complete" : "The operation did not complete");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);
    fill_circle(640, 275, 62, accent);
    draw_text_center_xy(g_ui.font_xl, ok ? "OK" : "!", 640, 276, C_BG);

    if (ok) {
        char title[96];
        snprintf(title, sizeof(title), "Prelude %s installed", tag ? tag : "");
        draw_text_center(g_ui.font_lg, title, 640, 367, C_TEXT);
    } else {
        draw_text_wrapped_center(g_ui.font_lg, updater_result_string(result), 640, 357, 900, C_TEXT);
    }
    draw_text_wrapped_center(g_ui.font_sm,
                             ok ? "/switch/nextendo.nro is ready."
                                : "The previous Prelude copy is preserved whenever recovery is possible.",
                             640, 430, 920, C_MUTED);
    draw_footer_button(1080, "+", "Exit", C_KEY_DARK);
    end_frame();
}

void ui_draw_error(const char *message, int http_status) {
    if (!g_ui.ready) return;
    begin_frame();
    draw_header("Could not check for updates");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);
    fill_circle(640, 272, 62, C_RED);
    draw_text_center_xy(g_ui.font_xl, "!", 640, 273, C_BG);
    draw_text_wrapped_center(g_ui.font_lg, message ? message : "Unknown error", 640, 357, 900, C_TEXT);
    if (http_status) {
        char http[64];
        snprintf(http, sizeof(http), "HTTP %d", http_status);
        draw_text_center(g_ui.font_sm, http, 640, 438, C_MUTED);
    }
    draw_footer_button(1080, "+", "Exit", C_KEY_DARK);
    end_frame();
}

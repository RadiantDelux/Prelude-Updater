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
#define APP_VERSION "1.1.0"
#define APP_AUTHOR  "RadiantDelux"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font_xl;
    TTF_Font *font_lg;
    TTF_Font *font_md;
    TTF_Font *font_sm;
    PlFontData font_data;
    bool pl_ready;
    bool ready;
    Uint32 progress_last_draw;
    Uint32 speed_last_tick;
    long speed_last_bytes;
    double speed_mib;
} UiState;

static UiState g_ui;

static const SDL_Color C_BG       = { 14, 16, 24, 255 };
static const SDL_Color C_PANEL    = { 25, 28, 40, 255 };
static const SDL_Color C_PANEL_2  = { 31, 35, 49, 255 };
static const SDL_Color C_TEXT     = { 242, 244, 250, 255 };
static const SDL_Color C_MUTED    = { 158, 166, 188, 255 };
static const SDL_Color C_ACCENT   = { 119, 101, 255, 255 };
static const SDL_Color C_ACCENT_2 = { 88, 183, 255, 255 };
static const SDL_Color C_GREEN    = { 84, 214, 150, 255 };
static const SDL_Color C_YELLOW   = { 246, 194, 81, 255 };
static const SDL_Color C_RED      = { 245, 102, 115, 255 };
static const SDL_Color C_WHITE    = { 255, 255, 255, 255 };

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

static TTF_Font *open_system_font(int size) {
    SDL_RWops *rw = SDL_RWFromConstMem(g_ui.font_data.address, (int)g_ui.font_data.size);
    if (!rw) return NULL;
    return TTF_OpenFontRW(rw, 1, size);
}

static void begin_frame(void) {
    set_color(C_BG);
    SDL_RenderClear(g_ui.renderer);

    /* Soft top accent. */
    set_color(C_ACCENT);
    SDL_Rect a = {0, 0, SCREEN_W, 5};
    SDL_RenderFillRect(g_ui.renderer, &a);
}

static void end_frame(void) {
    SDL_RenderPresent(g_ui.renderer);
}

static void draw_header(const char *eyebrow) {
    fill_round_rect(64, 43, 58, 58, 18, C_ACCENT);
    draw_text_center(g_ui.font_lg, "P", 93, 48, C_WHITE);

    draw_text(g_ui.font_lg, "Prelude Updater", 142, 42, C_TEXT);
    draw_text(g_ui.font_sm, eyebrow ? eyebrow : "Actualizador para Prelude", 144, 82, C_MUTED);

    draw_text_right(g_ui.font_sm, "v" APP_VERSION, 1210, 49, C_MUTED);
    draw_text_right(g_ui.font_sm, "por " APP_AUTHOR, 1210, 78, C_MUTED);
}

static void draw_footer_button(int x, const char *key, const char *label, SDL_Color key_color) {
    fill_circle(x, 665, 18, key_color);
    draw_text_center(g_ui.font_sm, key, x, 651, C_BG);
    draw_text(g_ui.font_sm, label, x + 30, 649, C_MUTED);
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
    if (!g_ui.font_xl || !g_ui.font_lg || !g_ui.font_md || !g_ui.font_sm) {
        ui_exit();
        return false;
    }

    g_ui.ready = true;
    return true;
}

void ui_exit(void) {
    if (g_ui.font_xl) TTF_CloseFont(g_ui.font_xl);
    if (g_ui.font_lg) TTF_CloseFont(g_ui.font_lg);
    if (g_ui.font_md) TTF_CloseFont(g_ui.font_md);
    if (g_ui.font_sm) TTF_CloseFont(g_ui.font_sm);
    g_ui.font_xl = g_ui.font_lg = g_ui.font_md = g_ui.font_sm = NULL;

    if (g_ui.renderer) SDL_DestroyRenderer(g_ui.renderer);
    if (g_ui.window) SDL_DestroyWindow(g_ui.window);
    g_ui.renderer = NULL;
    g_ui.window = NULL;

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
    draw_header("Comprobando GitHub Releases");

    fill_round_rect(64, 138, 1152, 450, 28, C_PANEL);
    fill_circle(640, 315, 54, C_PANEL_2);
    fill_circle(640, 315, 38, C_ACCENT);
    draw_text_center(g_ui.font_xl, "...", 640, 277, C_WHITE);
    draw_text_center(g_ui.font_lg, "Buscando la ultima version", 640, 397, C_TEXT);
    draw_text_center(g_ui.font_sm, "La comprobacion suele tardar solo unos segundos.", 640, 443, C_MUTED);
    end_frame();
}

void ui_draw_info(const UpdateInfo *info) {
    if (!g_ui.ready || !info) return;
    begin_frame();
    draw_header(info->update_available ? "Nueva version disponible" : "Prelude esta al dia");

    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);

    const char *local = info->target_exists ? info->installed_tag : "No instalado";
    draw_version_box(90, 176, "VERSION INSTALADA", local, C_ACCENT_2);
    draw_version_box(690, 176, "ULTIMA VERSION", info->latest_tag, C_ACCENT);

    char remote_size[64], local_size[64];
    format_size(info->remote_size, remote_size, sizeof(remote_size));
    format_size(info->local_size, local_size, sizeof(local_size));

    draw_text(g_ui.font_sm, "Archivo", 112, 326, C_MUTED);
    draw_text(g_ui.font_md, "/switch/nextendo.nro", 112, 354, C_TEXT);
    draw_text(g_ui.font_sm, "Descarga", 760, 326, C_MUTED);
    draw_text(g_ui.font_md, remote_size, 760, 354, C_TEXT);

    if (info->target_exists) {
        draw_text(g_ui.font_sm, "Tamano local", 112, 410, C_MUTED);
        draw_text(g_ui.font_md, local_size, 112, 438, C_TEXT);
    }

    SDL_Color status_color = info->update_available ? C_YELLOW : C_GREEN;
    const char *status = info->update_available ? "ACTUALIZACION DISPONIBLE" : "TODO ACTUALIZADO";
    fill_round_rect(760, 419, 330, 48, 18, status_color);
    draw_text_center(g_ui.font_sm, status, 925, 430, C_BG);

    if (info->update_available) {
        draw_text(g_ui.font_sm, "Se descargara primero a un archivo temporal y se verificara antes de reemplazar Prelude.",
                  112, 516, C_MUTED);
        draw_footer_button(88, "A", "Actualizar", C_GREEN);
        draw_footer_button(248, "B", "Cancelar", C_RED);
    } else {
        draw_text(g_ui.font_sm, "No hay nada que hacer. Puedes forzar una reinstalacion si quieres comprobar el archivo.",
                  112, 516, C_MUTED);
        draw_footer_button(88, "X", "Reinstalar", C_ACCENT_2);
    }
    draw_footer_button(1080, "+", "Salir", C_PANEL_2);
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
    draw_header("Descargando desde GitHub");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);

    draw_text(g_ui.font_sm, "INSTALANDO", 110, 180, C_MUTED);
    draw_text(g_ui.font_xl, tag && *tag ? tag : "Prelude", 110, 211, C_TEXT);
    draw_text_right(g_ui.font_xl, "", 1160, 211, C_TEXT);

    char pct[32];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    draw_text_right(g_ui.font_xl, pct, 1160, 211, C_TEXT);

    fill_round_rect(110, 318, 1050, 34, 17, C_PANEL_2);
    int progress_w = (int)(1050.0 * ratio);
    if (progress_w > 0) fill_round_rect(110, 318, progress_w, 34, 17, C_ACCENT);

    char current[64], total[64], detail[160];
    format_size(downloaded, current, sizeof(current));
    format_size(total_bytes, total, sizeof(total));
    snprintf(detail, sizeof(detail), "%s de %s", current, total);
    draw_text(g_ui.font_md, detail, 110, 385, C_TEXT);

    char speed[64];
    if (g_ui.speed_mib > 0.01) snprintf(speed, sizeof(speed), "%.2f MiB/s", g_ui.speed_mib);
    else snprintf(speed, sizeof(speed), "Conectando...");
    draw_text_right(g_ui.font_md, speed, 1160, 385, C_MUTED);

    draw_text(g_ui.font_sm, "No retires la SD durante la instalacion. Puedes cancelar mientras se descarga.",
              110, 470, C_MUTED);
    draw_footer_button(88, "B", "Cancelar descarga", C_RED);
    end_frame();
}

void ui_draw_installing(const char *tag) {
    if (!g_ui.ready) return;
    begin_frame();
    draw_header("Verificando e instalando");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);
    fill_circle(640, 300, 56, C_ACCENT);
    draw_text_center(g_ui.font_xl, "OK", 640, 266, C_WHITE);
    draw_text_center(g_ui.font_lg, "Aplicando " , 570, 400, C_TEXT);
    draw_text(g_ui.font_lg, tag ? tag : "Prelude", 675, 400, C_TEXT);
    draw_text_center(g_ui.font_sm, "Verificando tamano, creando backup y reemplazando nextendo.nro...", 640, 458, C_MUTED);
    end_frame();
}

void ui_draw_result(UpdateResult result, const char *tag) {
    if (!g_ui.ready) return;
    bool ok = result == UPDATE_OK || result == UPDATE_ERR_STATE;
    SDL_Color accent = ok ? C_GREEN : (result == UPDATE_ERR_CANCELLED ? C_YELLOW : C_RED);

    begin_frame();
    draw_header(ok ? "Actualizacion completada" : "La operacion no se completo");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);
    fill_circle(640, 280, 64, accent);
    draw_text_center(g_ui.font_xl, ok ? "OK" : "!", 640, 243, C_BG);

    if (ok) {
        char title[96];
        snprintf(title, sizeof(title), "Prelude %s instalado", tag ? tag : "");
        draw_text_center(g_ui.font_lg, title, 640, 382, C_TEXT);
    } else {
        draw_text_center(g_ui.font_lg, updater_result_string(result), 640, 382, C_TEXT);
    }
    draw_text_center(g_ui.font_sm, ok ? "/switch/nextendo.nro esta listo." : "El Prelude anterior se conserva siempre que fue posible.",
                     640, 435, C_MUTED);
    draw_footer_button(1080, "+", "Salir", C_PANEL_2);
    end_frame();
}

void ui_draw_error(const char *message, int http_status) {
    if (!g_ui.ready) return;
    begin_frame();
    draw_header("No se pudo comprobar la actualizacion");
    fill_round_rect(64, 132, 1152, 455, 28, C_PANEL);
    fill_circle(640, 278, 64, C_RED);
    draw_text_center(g_ui.font_xl, "!", 640, 241, C_BG);
    draw_text_center(g_ui.font_lg, message ? message : "Error desconocido", 640, 382, C_TEXT);
    if (http_status) {
        char http[64];
        snprintf(http, sizeof(http), "HTTP %d", http_status);
        draw_text_center(g_ui.font_sm, http, 640, 434, C_MUTED);
    }
    draw_footer_button(1080, "+", "Salir", C_PANEL_2);
    end_frame();
}

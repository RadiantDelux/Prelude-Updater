/*
 * Music and notification sounds for Prelude Updater.
 * Copyright (C) 2026 RadiantDelux
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <switch.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio.h"
#include "net.h"

#define CATALOG_URL \
    "https://raw.githubusercontent.com/RadiantDelux/Prelude-Updater/main/music/catalog.txt"
#define MIN_AUDIO_SIZE 128L

static Mix_Music *background_music;
static Mix_Chunk *start_sound;
static Mix_Chunk *finish_sound;
static bool mixer_ready;

typedef struct {
    AudioProgressCallback callback;
    void *user;
} AudioProgressBridge;

static bool is_safe_id(const char *id) {
    if (!id || !*id) return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p) {
        if (!isalnum(*p) && *p != '-' && *p != '_') return false;
    }
    return true;
}

static const char *audio_extension(const char *url) {
    if (!url) return NULL;
    const char *dot = strrchr(url, '.');
    if (!dot) return NULL;
    if (strcmp(dot, ".ogg") == 0 || strcmp(dot, ".mp3") == 0 || strcmp(dot, ".wav") == 0) {
        return dot;
    }
    return NULL;
}

static bool make_local_name(const char *id, const char *url, char *out, size_t cap) {
    const char *ext = audio_extension(url);
    if (!is_safe_id(id) || !ext) return false;
    int n = snprintf(out, cap, "%s%s", id, ext);
    return n > 0 && (size_t)n < cap;
}

static void make_local_path(const char *name, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", UPDATER_MUSIC_DIR, name ? name : "");
}

static bool file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && st.st_size > 0;
}

static unsigned parse_roles(const char *text) {
    unsigned roles = 0;
    if (!text) return roles;

    char copy[96];
    snprintf(copy, sizeof(copy), "%s", text);

    char *save = NULL;
    for (char *token = strtok_r(copy, ",", &save); token; token = strtok_r(NULL, ",", &save)) {
        while (*token == ' ' || *token == '\t') ++token;
        if (strcmp(token, "background") == 0) roles |= AUDIO_ROLE_BACKGROUND;
        else if (strcmp(token, "start") == 0) roles |= AUDIO_ROLE_START;
        else if (strcmp(token, "finish") == 0) roles |= AUDIO_ROLE_FINISH;
        else if (strcmp(token, "all") == 0) {
            roles |= AUDIO_ROLE_BACKGROUND | AUDIO_ROLE_START | AUDIO_ROLE_FINISH;
        }
    }
    return roles;
}

static void trim(char *text) {
    if (!text) return;
    text[strcspn(text, "\r\n")] = '\0';
    size_t len = strlen(text);
    while (len && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
    char *start = text;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1);
}

static bool parse_catalog_line(char *line, AudioTrack *track) {
    char *fields[5] = {0};
    char *save = NULL;
    size_t count = 0;

    for (char *token = strtok_r(line, "|", &save);
         token && count < 5;
         token = strtok_r(NULL, "|", &save)) {
        trim(token);
        fields[count++] = token;
    }

    if (count != 5 || !is_safe_id(fields[0]) || strncmp(fields[4], "https://", 8) != 0) {
        return false;
    }

    memset(track, 0, sizeof(*track));
    snprintf(track->id, sizeof(track->id), "%s", fields[0]);
    track->roles = parse_roles(fields[1]);
    snprintf(track->title, sizeof(track->title), "%s", fields[2]);
    snprintf(track->artist, sizeof(track->artist), "%s", fields[3]);
    snprintf(track->url, sizeof(track->url), "%s", fields[4]);

    return track->roles && make_local_name(track->id, track->url,
                                           track->local_name, sizeof(track->local_name));
}

static void free_audio_objects(void) {
    if (background_music) Mix_FreeMusic(background_music);
    if (start_sound) Mix_FreeChunk(start_sound);
    if (finish_sound) Mix_FreeChunk(finish_sound);
    background_music = NULL;
    start_sound = NULL;
    finish_sound = NULL;
}

static Mix_Music *load_music_file(const char *name) {
    if (!name || !*name) return NULL;
    char path[256];
    make_local_path(name, path, sizeof(path));
    return file_exists(path) ? Mix_LoadMUS(path) : NULL;
}

static Mix_Chunk *load_sound_file(const char *name) {
    if (!name || !*name) return NULL;
    char path[256];
    make_local_path(name, path, sizeof(path));
    return file_exists(path) ? Mix_LoadWAV(path) : NULL;
}

bool audio_init(const AppSettings *settings) {
    if (mixer_ready) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;

    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3);
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) != 0) {
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    Mix_AllocateChannels(8);
    Mix_VolumeMusic(MIX_MAX_VOLUME / 2);
    mixer_ready = true;
    audio_apply_settings(settings);
    return true;
}

void audio_exit(void) {
    if (!mixer_ready) return;
    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    free_audio_objects();
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    mixer_ready = false;
}

void audio_apply_settings(const AppSettings *settings) {
    if (!mixer_ready || !settings) return;

    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    free_audio_objects();

    background_music = load_music_file(settings->background);
    start_sound = load_sound_file(settings->start_sound);
    finish_sound = load_sound_file(settings->finish_sound);

    if (background_music) Mix_PlayMusic(background_music, -1);
}

void audio_play_start(void) {
    if (mixer_ready && start_sound) Mix_PlayChannel(-1, start_sound, 0);
}

void audio_play_finish(void) {
    if (mixer_ready && finish_sound) Mix_PlayChannel(-1, finish_sound, 0);
}

AudioCatalog audio_catalog_fetch(void) {
    AudioCatalog catalog;
    memset(&catalog, 0, sizeof(catalog));

    size_t length = 0;
    int status = 0;
    int net_error = NET_ERR_UNKNOWN;
    unsigned char *body = net_https_get_url(CATALOG_URL, &length, &status, &net_error);

    catalog.http_status = status;
    catalog.net_error = net_error;

    if (!body) {
        snprintf(catalog.error, sizeof(catalog.error), "Could not load the music catalog: %s",
                 net_error_string(net_error));
        return catalog;
    }
    if (status != 200) {
        snprintf(catalog.error, sizeof(catalog.error), "Music catalog returned HTTP %d", status);
        free(body);
        return catalog;
    }

    char *cursor = (char *)body;
    char *end = cursor + length;
    while (cursor < end && catalog.count < AUDIO_TRACK_LIMIT) {
        char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!line_end) line_end = end;

        size_t line_len = (size_t)(line_end - cursor);
        if (line_len > 0 && line_len < 1400) {
            char line[1400];
            memcpy(line, cursor, line_len);
            line[line_len] = '\0';
            trim(line);

            if (*line && *line != '#') {
                AudioTrack track;
                if (parse_catalog_line(line, &track)) catalog.tracks[catalog.count++] = track;
            }
        }
        cursor = line_end < end ? line_end + 1 : end;
    }

    free(body);
    catalog.ok = true;
    return catalog;
}

bool audio_track_supports(const AudioTrack *track, AudioSlot slot) {
    if (!track) return false;
    unsigned role = AUDIO_ROLE_BACKGROUND;
    if (slot == AUDIO_SLOT_START) role = AUDIO_ROLE_START;
    if (slot == AUDIO_SLOT_FINISH) role = AUDIO_ROLE_FINISH;
    return (track->roles & role) != 0;
}

static int audio_progress_bridge(long downloaded, long total, void *user) {
    AudioProgressBridge *bridge = (AudioProgressBridge *)user;
    if (!bridge || !bridge->callback) return 0;
    return bridge->callback(downloaded, total, bridge->user) ? 0 : 1;
}

bool audio_install_track(const AudioTrack *track,
                         AudioProgressCallback progress_cb,
                         void *progress_user,
                         char *error,
                         size_t error_cap) {
    if (error && error_cap) error[0] = '\0';
    if (!track || !track->url[0] || !track->local_name[0] || !settings_prepare_dirs()) {
        if (error && error_cap) snprintf(error, error_cap, "Invalid track or SD path.");
        return false;
    }

    char final_path[256];
    char temp_path[272];
    make_local_path(track->local_name, final_path, sizeof(final_path));
    snprintf(temp_path, sizeof(temp_path), "%s.part", final_path);

    if (file_exists(final_path)) return true;
    remove(temp_path);
    FILE *file = fopen(temp_path, "wb");
    if (!file) {
        if (error && error_cap) snprintf(error, error_cap, "Could not create the music file on the SD card.");
        return false;
    }

    AudioProgressBridge bridge = { progress_cb, progress_user };
    int status = 0;
    int net_error = NET_ERR_UNKNOWN;
    long bytes = net_https_download_url(track->url, file, &status, &net_error,
                                        progress_cb ? audio_progress_bridge : NULL,
                                        progress_cb ? &bridge : NULL);
    bool flushed = fflush(file) == 0;
    fclose(file);

    if (bytes < MIN_AUDIO_SIZE || status != 200 || net_error != NET_OK || !flushed) {
        remove(temp_path);
        if (error && error_cap) {
            if (net_error == NET_ERR_ABORTED) snprintf(error, error_cap, "Download cancelled.");
            else if (status && status != 200) snprintf(error, error_cap, "Music download returned HTTP %d.", status);
            else snprintf(error, error_cap, "Music download failed: %s", net_error_string(net_error));
        }
        return false;
    }

    remove(final_path);
    if (rename(temp_path, final_path) != 0) {
        remove(temp_path);
        if (error && error_cap) snprintf(error, error_cap, "Could not install the downloaded music file.");
        return false;
    }
    fsdevCommitDevice("sdmc");
    return true;
}

bool audio_select_track(AppSettings *settings, AudioSlot slot, const AudioTrack *track) {
    if (!settings || !track || !audio_track_supports(track, slot)) return false;

    char *dst = settings->background;
    size_t cap = sizeof(settings->background);
    if (slot == AUDIO_SLOT_START) {
        dst = settings->start_sound;
        cap = sizeof(settings->start_sound);
    } else if (slot == AUDIO_SLOT_FINISH) {
        dst = settings->finish_sound;
        cap = sizeof(settings->finish_sound);
    }

    snprintf(dst, cap, "%s", track->local_name);
    return settings_save(settings);
}

void audio_clear_slot(AppSettings *settings, AudioSlot slot) {
    if (!settings) return;
    if (slot == AUDIO_SLOT_BACKGROUND) settings->background[0] = '\0';
    else if (slot == AUDIO_SLOT_START) settings->start_sound[0] = '\0';
    else settings->finish_sound[0] = '\0';
    settings_save(settings);
}

const char *audio_slot_name(AudioSlot slot) {
    switch (slot) {
        case AUDIO_SLOT_BACKGROUND: return "Background music";
        case AUDIO_SLOT_START: return "Start sound";
        case AUDIO_SLOT_FINISH: return "Completion sound";
        default: return "Audio";
    }
}

#ifndef PRELUDE_UPDATER_AUDIO_H
#define PRELUDE_UPDATER_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

#include "settings.h"

#define AUDIO_TRACK_LIMIT 32

#define AUDIO_ROLE_BACKGROUND (1u << 0)
#define AUDIO_ROLE_START      (1u << 1)
#define AUDIO_ROLE_FINISH     (1u << 2)

typedef enum {
    AUDIO_SLOT_BACKGROUND = 0,
    AUDIO_SLOT_START,
    AUDIO_SLOT_FINISH
} AudioSlot;

typedef struct {
    char id[48];
    char title[96];
    char artist[96];
    char url[1024];
    char local_name[96];
    unsigned roles;
} AudioTrack;

typedef struct {
    bool ok;
    size_t count;
    int http_status;
    int net_error;
    char error[160];
    AudioTrack tracks[AUDIO_TRACK_LIMIT];
} AudioCatalog;

typedef bool (*AudioProgressCallback)(long downloaded, long total, void *user);

bool audio_init(const AppSettings *settings);
void audio_exit(void);
void audio_apply_settings(const AppSettings *settings);
void audio_play_start(void);
void audio_play_finish(void);

AudioCatalog audio_catalog_fetch(void);
bool audio_track_supports(const AudioTrack *track, AudioSlot slot);
bool audio_install_track(const AudioTrack *track,
                         AudioProgressCallback progress_cb,
                         void *progress_user,
                         char *error,
                         size_t error_cap);
bool audio_select_track(AppSettings *settings, AudioSlot slot, const AudioTrack *track);
void audio_clear_slot(AppSettings *settings, AudioSlot slot);
const char *audio_slot_name(AudioSlot slot);

#endif

#ifndef PRELUDE_UPDATER_SETTINGS_H
#define PRELUDE_UPDATER_SETTINGS_H

#include <stdbool.h>

#define UPDATER_DATA_DIR      "sdmc:/switch/Prelude-Updater"
#define UPDATER_MUSIC_DIR     UPDATER_DATA_DIR "/music"
#define UPDATER_SETTINGS_PATH UPDATER_DATA_DIR "/settings.ini"

typedef struct {
    char background[96];
    char start_sound[96];
    char finish_sound[96];
} AppSettings;

bool settings_prepare_dirs(void);
void settings_load(AppSettings *settings);
bool settings_save(const AppSettings *settings);

#endif

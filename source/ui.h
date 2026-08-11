#ifndef PRELUDE_UPDATER_UI_H
#define PRELUDE_UPDATER_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "audio.h"
#include "releases.h"
#include "settings.h"
#include "updater.h"

bool ui_init(void);
void ui_exit(void);
void ui_pump(void);

void ui_draw_main_menu(int selected);
void ui_draw_checking(const char *title, const char *subtitle);
void ui_draw_release_list(ReleaseTarget target,
                          const ReleaseList *list,
                          int selected,
                          const char *current_tag,
                          bool target_exists);
void ui_draw_music_menu(const AppSettings *settings, int selected);
void ui_draw_track_list(AudioSlot slot,
                        const AudioTrack *const *tracks,
                        size_t track_count,
                        int selected,
                        const char *current_file);
void ui_draw_progress(const char *eyebrow,
                      const char *title,
                      long downloaded,
                      long total,
                      bool cancellable,
                      bool force);
void ui_draw_operation_result(ReleaseTarget target,
                              const ReleaseEntry *release,
                              const char *action,
                              UpdateResult result);
void ui_draw_confirm(const char *title, const char *message);
void ui_draw_notice(const char *title, const char *message, bool back_allowed);

#endif

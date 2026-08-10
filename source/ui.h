#ifndef PRELUDE_UPDATER_UI_H
#define PRELUDE_UPDATER_UI_H

#include <stdbool.h>
#include "updater.h"

bool ui_init(void);
void ui_exit(void);
void ui_pump(void);

void ui_draw_checking(void);
void ui_draw_info(const UpdateInfo *info);
void ui_draw_download_begin(const char *tag, long total_bytes);
void ui_draw_download_progress(const char *tag, long downloaded, long total_bytes, bool force);
void ui_draw_installing(const char *tag);
void ui_draw_result(UpdateResult result, const char *tag);
void ui_draw_error(const char *message, int http_status);

#endif

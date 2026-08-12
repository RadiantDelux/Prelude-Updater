#ifndef PRELUDE_UPDATER_INSTALLER_H
#define PRELUDE_UPDATER_INSTALLER_H

#include <stdbool.h>
#include <stddef.h>

#include "releases.h"

#define PRELUDE_TARGET_PATH  "sdmc:/switch/nextendo.nro"
#define SELF_TARGET_FALLBACK "sdmc:/switch/Prelude-Updater/prelude-updater.nro"
#define PRELUDE_STATE_PATH   "sdmc:/switch/Prelude-Updater/installed_version.txt"

typedef enum {
    UPDATE_OK = 0,
    UPDATE_ERR_NETWORK,
    UPDATE_ERR_HTTP,
    UPDATE_ERR_SIZE,
    UPDATE_ERR_SD,
    UPDATE_ERR_BACKUP,
    UPDATE_ERR_INSTALL,
    UPDATE_ERR_STATE,
    UPDATE_ERR_CANCELLED
} UpdateResult;

typedef bool (*UpdaterProgressCallback)(long downloaded, long total, void *user);

bool updater_current_version(ReleaseTarget target,
                             char *tag,
                             size_t tag_cap,
                             bool *target_exists,
                             const char *self_path);

const char *updater_action_for(const ReleaseEntry *release,
                               const char *current_tag,
                               bool target_exists);

UpdateResult updater_install_release(ReleaseTarget target,
                                     const ReleaseEntry *release,
                                     const char *self_path,
                                     UpdaterProgressCallback progress_cb,
                                     void *progress_user);

const char *updater_result_string(UpdateResult result);

#endif

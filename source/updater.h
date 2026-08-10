#ifndef PRELUDE_UPDATER_H
#define PRELUDE_UPDATER_H

#include <switch.h>

#define PRELUDE_TARGET_PATH "sdmc:/switch/nextendo.nro"
#define UPDATER_DATA_DIR    "sdmc:/switch/Prelude-Updater"
#define UPDATER_STATE_PATH  UPDATER_DATA_DIR "/installed_version.txt"

typedef struct {
    bool ok;
    bool target_exists;
    bool installed_version_known;
    bool update_available;
    char installed_tag[32];
    char latest_tag[32];
    char download_url[1024];
    long local_size;
    long remote_size;
    int http_status;
    int net_error;
    char error[160];
} UpdateInfo;

typedef enum {
    UPDATE_OK = 0,
    UPDATE_ERR_NETWORK,
    UPDATE_ERR_HTTP,
    UPDATE_ERR_SIZE,
    UPDATE_ERR_SD,
    UPDATE_ERR_BACKUP,
    UPDATE_ERR_INSTALL,
    UPDATE_ERR_STATE
} UpdateResult;

UpdateInfo updater_check(void);
UpdateResult updater_install(const UpdateInfo *info);
const char *updater_result_string(UpdateResult result);

#endif

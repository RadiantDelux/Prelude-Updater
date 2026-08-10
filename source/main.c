/*
 * Prelude Updater
 * Copyright (C) 2026 Prelude Updater contributors.
 * AGPL-3.0-or-later
 */

#include <switch.h>
#include <stdio.h>
#include <string.h>

#include "updater.h"

#define UPDATER_VERSION "1.0.0"

static void clear_screen(void) {
    printf("\x1b[2J\x1b[1;1H");
}

static void print_header(void) {
    printf("========================================\n");
    printf("          PRELUDE UPDATER v%s\n", UPDATER_VERSION);
    printf("========================================\n\n");
}

static void wait_for_exit(PadState *pad) {
    printf("\n[+] Salir\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    clear_screen();
    print_header();
    printf("Consultando la ultima release de Prelude...\n");
    consoleUpdate(NULL);

    UpdateInfo info = updater_check();

    clear_screen();
    print_header();

    if (!info.ok) {
        printf("ERROR\n\n%s\n", info.error[0] ? info.error : "No se pudo comprobar la actualizacion.");
        if (info.http_status) printf("HTTP: %d\n", info.http_status);
        wait_for_exit(&pad);
        consoleExit(NULL);
        return 0;
    }

    printf("Destino:       /switch/nextendo.nro\n");
    printf("Instalada:     %s\n", info.target_exists ? info.installed_tag : "no instalada");
    printf("Ultima GitHub: %s\n", info.latest_tag);
    printf("Tamano remoto: %ld bytes\n", info.remote_size);
    if (info.target_exists) printf("Tamano local:  %ld bytes\n", info.local_size);
    printf("\n");

    if (!info.update_available) {
        printf("Prelude ya esta actualizado.\n\n");
        printf("[X] Reinstalar %s\n", info.latest_tag);
        printf("[+] Salir\n");
        consoleUpdate(NULL);

        bool reinstall = false;
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 down = padGetButtonsDown(&pad);
            if (down & HidNpadButton_Plus) break;
            if (down & HidNpadButton_X) { reinstall = true; break; }
            consoleUpdate(NULL);
        }
        if (!reinstall) {
            consoleExit(NULL);
            return 0;
        }
    } else {
        if (!info.installed_version_known && info.target_exists) {
            printf("La version local no fue instalada por este updater,\n");
            printf("asi que se ofrece instalar la release actual.\n\n");
        } else if (!info.target_exists) {
            printf("Prelude no esta instalado.\n\n");
        } else if (strcmp(info.installed_tag, info.latest_tag) == 0 && info.local_size != info.remote_size) {
            printf("La version coincide, pero el tamano del NRO no.\n");
            printf("Se recomienda reinstalar.\n\n");
        } else {
            printf("Hay una actualizacion disponible.\n\n");
        }

        printf("[A] Instalar %s\n", info.latest_tag);
        printf("[B] Cancelar\n");
        printf("[+] Salir\n");
        consoleUpdate(NULL);

        bool install = false;
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 down = padGetButtonsDown(&pad);
            if (down & HidNpadButton_A) { install = true; break; }
            if (down & HidNpadButton_B) break;
            if (down & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        if (!install) {
            consoleExit(NULL);
            return 0;
        }
    }

    clear_screen();
    print_header();
    printf("Descargando %s...\n", info.latest_tag);
    printf("No apagues la consola ni retires la SD.\n\n");
    consoleUpdate(NULL);

    UpdateResult result = updater_install(&info);

    if (result == UPDATE_OK || result == UPDATE_ERR_STATE) {
        printf("%s\n", updater_result_string(result));
        printf("\nPrelude: /switch/nextendo.nro\n");
        if (result == UPDATE_OK) printf("Version registrada: %s\n", info.latest_tag);
    } else {
        printf("ERROR\n\n%s\n", updater_result_string(result));
    }

    wait_for_exit(&pad);
    consoleExit(NULL);
    return 0;
}

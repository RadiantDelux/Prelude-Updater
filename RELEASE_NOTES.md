# Prelude Updater v1.4.0

Test release for validating the staged self-update flow introduced in the corrected v1.3.0.

## Test goal

The main purpose of this release is to test updating Prelude Updater itself from v1.3.0 to v1.4.0 on real Nintendo Switch hardware.

The updater should:

1. Find v1.4.0 from the updater release list.
2. Download the `prelude-updater.nro` release asset to `prelude-updater.stage.nro`.
3. Exit and chainload the staged NRO through hbloader.
4. Replace the installed updater only after the original NRO is no longer running.
5. Validate the installed file and chainload the final updater NRO.
6. Start again reporting v1.4.0 and remove the temporary stage, pending state and backup files.

This release is compiled and packaged by GitHub Actions, but the staged self-update still requires the real-hardware test described above.

## Installation

For a normal manual installation, download `Prelude-Updater-v1.4.0.zip` and extract it directly to the root of the SD card.

The package includes:

```text
switch/
└── Prelude-Updater/
    ├── prelude-updater.nro
    └── music/
        ├── Catalogue Orders - Miitopia.mp3
        ├── Settings - Wii Sports Resort.mp3
        ├── Super Mario 64 - Dire, Dire Docks.mp3
        ├── Super Mario 64 - Staff Roll.mp3
        ├── Wave Race 64 - Config.mp3
        └── scizzie - aquatic ambience.mp3
```

## Release assets

- `Prelude-Updater-v1.4.0.zip` — SD-ready package for manual installation.
- `prelude-updater.nro` — standalone asset required by Prelude Updater's current self-update mechanism.

## Audio behavior

- Background Music remains the normal app music slot.
- There is no separate Start Sound setting.
- Completion Sound does not play when it is selected.
- Completion Sound plays only after a completed Prelude install, update, downgrade or reinstall.
- Background music stops before Completion Sound begins.

## SD card paths

- Prelude: `/switch/nextendo.nro`
- Prelude Updater: `/switch/Prelude-Updater/prelude-updater.nro`
- Staged self-update: `/switch/Prelude-Updater/prelude-updater.stage.nro`
- Updater settings/music/cache: `/switch/Prelude-Updater/`

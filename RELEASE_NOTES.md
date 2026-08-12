# Prelude Updater v1.4.0

Audio behavior and polish update.

## Installation

Download `Prelude-Updater-v1.4.0.zip` and extract it directly to the root of your SD card.

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

## Changes

- Removed the separate Start Sound setting.
- Background Music is now the only music that starts during normal app use.
- Selecting a Completion Sound no longer previews or plays it immediately.
- Completion Sound only plays after a successful update, downgrade or reinstall.
- When Completion Sound begins, the current background music is stopped first so the tracks never overlap.
- Old `start` catalog roles are treated as background music for compatibility.
- Existing v1.3.0 settings files are migrated automatically by ignoring the old `start_sound` value.
- Self-update, downgrade and reinstall support remains available for Prelude Updater itself.

## SD card paths

- Prelude: `/switch/nextendo.nro`
- Prelude Updater: `/switch/Prelude-Updater/prelude-updater.nro`
- Updater settings/music/cache: `/switch/Prelude-Updater/`

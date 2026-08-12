# Prelude Updater v1.3.0

Corrected first public release of Prelude Updater.

## Installation

Download `Prelude-Updater-v1.3.0.zip` and extract it directly to the root of your SD card.

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

## Important self-update fix

Self-update no longer attempts to replace the NRO while that same NRO is still running.

Updater self-update, downgrade and reinstall now use a staged two-step process:

1. Download and validate the selected updater NRO as `prelude-updater.stage.nro`.
2. Ask hbloader to launch the staged NRO with `envSetNextLoad()` and exit the current updater.
3. The staged NRO backs up the installed updater and copies itself to the original updater path after the old process has stopped.
4. The staged NRO validates the installed file and asks hbloader to launch the final updater NRO.
5. The final updater launch removes the temporary stage, pending state and backup files.

If installation fails after the backup is created, recovery of the previous updater is attempted before returning to hbmenu.

## Audio behavior

- Background Music is the normal app music slot.
- There is no separate Start Sound setting.
- Selecting a Completion Sound does not preview it immediately.
- Completion Sound only plays after a completed Prelude install/update/downgrade/reinstall.
- Background music is stopped before Completion Sound begins, preventing overlap.
- Old `start` catalog roles are treated as background music for compatibility.

## SD card paths

- Prelude: `/switch/nextendo.nro`
- Prelude Updater: `/switch/Prelude-Updater/prelude-updater.nro`
- Staged self-update: `/switch/Prelude-Updater/prelude-updater.stage.nro`
- Updater settings/music/cache: `/switch/Prelude-Updater/`

Users running one of the withdrawn builds with the broken self-update should install this corrected v1.3.0 manually from the ZIP once. Future self-updates use the staged process described above.

Release build validated by GitHub Actions before publication.

# Prelude Updater

Nintendo Switch homebrew for installing and managing releases of [NextendoNetwork/Prelude-Nro].

## Features

- Lists published Prelude releases from GitHub.
- Update, downgrade and reinstall Prelude directly from the console.
- Lists Prelude Updater releases and can update/downgrade itself.
- Downloads to a temporary file, validates the published asset size, and preserves a backup during replacement.
- Graphical SDL2 interface with Nintendo Switch system fonts.
- Background music plus configurable start/completion sounds.
- Remote audio catalog so new music can be added without recompiling the updater.

## Audio catalog

The app reads `music/catalog.txt` from the `main` branch of this repository.

Each non-comment line uses this format:

```text
id|roles|title|artist|https-url
```

Roles:

```text
background
start
finish
all
```

Multiple roles can be combined with commas:

```text
background,start
```

Supported extensions are `.ogg`, `.mp3` and `.wav`.

When a track is selected for the first time, it is downloaded to:

```text
sdmc:/switch/Prelude-Updater/music/
```

Selections are stored in `settings.ini` in the updater data directory.

## Build

Requirements:

- devkitA64 / libnx
- switch-curl
- switch-sdl2
- switch-sdl2_ttf
- switch-sdl2_mixer

Build with:

```bash
make -j$(nproc)
```

The output is `prelude-updater.nro`.

GitHub Actions also builds the NRO using the devkitPro devkita64 container.

## Install

For normal installation, download `Prelude-Updater-v1.3.0.zip` from Releases and extract it directly to the root of the SD card. The package already contains:

```text
switch/
└── Prelude-Updater/
    ├── prelude-updater.nro
    └── music/
        └── bundled base tracks
```

Prelude itself is installed and managed at:

```text
/switch/nextendo.nro
```

The standalone `prelude-updater.nro` release asset is kept for the updater's built-in self-update system.

## License

Prelude Updater is released under the GNU Affero General Public License v3.0 or later.

Parts of the original updater/network behavior were derived from the AGPL-licensed Prelude project by Nextendo Network.

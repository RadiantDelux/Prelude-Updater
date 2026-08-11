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

Example:

```text
menu-night|background|Night Menu|RadiantDelux|https://raw.githubusercontent.com/RadiantDelux/Prelude-Updater/main/music/menu-night.ogg
update-start|start|Update Start|RadiantDelux|https://raw.githubusercontent.com/RadiantDelux/Prelude-Updater/main/music/update-start.ogg
update-done|finish|Update Complete|RadiantDelux|https://raw.githubusercontent.com/RadiantDelux/Prelude-Updater/main/music/update-done.ogg
```

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

Recommended layout:

```text
/switch/Prelude-Updater/prelude-updater.nro
```

Prelude itself is installed/managed at:

```text
/switch/nextendo.nro
```

## License

Prelude Updater is released under the GNU Affero General Public License v3.0 or later.

Parts of the original updater/network behavior were derived from the AGPL-licensed Prelude project by Nextendo Network.

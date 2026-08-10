# Prelude Updater

A standalone Nintendo Switch homebrew application that checks the latest public release of
[`NextendoNetwork/Prelude-Nro`](https://github.com/NextendoNetwork/Prelude-Nro) and installs its
`nextendo.nro` asset to:

```text
sdmc:/switch/nextendo.nro
```

## Features

- Graphical SDL2 interface with the Prelude Updater logo.
- NRO metadata for hbmenu: **Prelude Updater**, **RadiantDelux**, version **1.2.0**.
- GitHub Releases update checks over HTTPS.
- Download progress, percentage, transferred size and current speed.
- Safe temporary download and size validation before replacing Prelude.
- Fast rename-based installation on the SD card, with copy fallback for FAT filesystems.
- Temporary backup and recovery path if installation fails.
- `B` can cancel an active download.

## How it works

1. Queries GitHub `releases/latest` over HTTPS.
2. Reads the semver tag (`vX.Y.Z`) and the published size of `nextendo.nro`.
3. Compares the tag against `sdmc:/switch/Prelude-Updater/installed_version.txt`.
4. Downloads to `sdmc:/switch/Prelude-Updater/nextendo.nro.new`.
5. Follows GitHub HTTPS redirects to the release asset CDN.
6. Verifies that the downloaded size matches the GitHub release metadata.
7. Creates a temporary backup of the installed Prelude NRO when one exists.
8. Replaces `/switch/nextendo.nro` and validates the final file size.
9. Stores the installed tag for future update checks.

On the first run, if `nextendo.nro` already exists but no updater state file is present, the local
version is shown as `unknown` and the current release is offered for installation. After the first
installation performed by this updater, semver comparisons are automatic.

## Controls

- `A`: install an available update.
- `B`: cancel / cancel an active download.
- `X`: reinstall the latest release when already up to date.
- `+`: exit.

## Build requirements

Requires devkitPro with devkitA64/libnx and these Switch portlibs:

```text
switch-curl
switch-sdl2
switch-sdl2_ttf
switch-freetype
switch-harfbuzz
switch-zlib
```

Build with:

```sh
make -j$(nproc)
```

Output:

```text
prelude-updater.nro
```

The included GitHub Actions workflow installs the required packages and builds the NRO inside the
`devkitpro/devkita64` container.

## SD card layout

Recommended layout:

```text
/switch/Prelude-Updater/prelude-updater.nro
/switch/nextendo.nro
```

The updater data directory is created automatically when needed.

## NRO metadata

The Makefile generates a NACP with:

```text
Name:    Prelude Updater
Author:  RadiantDelux
Version: 1.2.0
```

The NACP and `icon.jpg` are explicitly embedded in `prelude-updater.nro`. The GitHub Actions build
also checks that the generated NACP and final NRO contain the expected name and author strings.

## Security and integrity

- Only HTTPS URLs are accepted.
- TLS certificate verification remains enabled in libcurl.
- Redirects are limited.
- Prelude is not replaced until the download has completed and passed size validation.
- A temporary backup is kept during replacement.
- The downloaded and installed file sizes are checked against GitHub Releases metadata.

## License and attribution

This project is licensed under AGPL-3.0-or-later because part of its update/transport design is based
on the public Prelude-Nro project, which uses that license.

Prelude-Nro: Copyright (C) 2026 Nextendo Network.

This project is not affiliated with Nintendo or Nextendo Network.

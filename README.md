# Prelude Updater

Actualizador grafico para Nintendo Switch creado por **RadiantDelux**. Comprueba la ultima release
publica de [`NextendoNetwork/Prelude-Nro`](https://github.com/NextendoNetwork/Prelude-Nro) e instala
su asset `nextendo.nro` en:

```text
sdmc:/switch/nextendo.nro
```

## Novedades de v1.1.0

- Interfaz grafica 1280x720 con SDL2.
- Usa la fuente compartida del sistema de Nintendo Switch; no incluye fuentes externas.
- Barra de progreso real, porcentaje, MiB descargados y velocidad aproximada.
- `B` permite cancelar durante la descarga.
- Transporte cambiado a `switch-curl` para redirects, TLS y streaming mas eficientes.
- Buffer de red de 256 KiB y escritura de SD bufferizada.
- Instalacion rapida mediante `rename()` dentro de la SD; copia completa solo como fallback.
- Icono propio para hbmenu.
- Metadatos NACP: `Prelude Updater`, autor `RadiantDelux`, version `1.1.0`.

## Funcionamiento

1. Consulta `releases/latest` de GitHub por HTTPS.
2. Lee el tag semver (`vX.Y.Z`) y el tamano publicado de `nextendo.nro`.
3. Compara el tag con `sdmc:/switch/Prelude-Updater/installed_version.txt`.
4. Descarga a `sdmc:/switch/Prelude-Updater/nextendo.nro.new`.
5. libcurl sigue los redirects HTTPS de GitHub hasta el CDN.
6. Verifica que el tamano descargado coincide con GitHub Releases.
7. Mueve temporalmente el Prelude anterior a un backup; si FAT no permite `rename()`, usa copia.
8. Mueve el NRO nuevo a `/switch/nextendo.nro`; tambien tiene fallback por copia.
9. Vuelve a verificar el tamano y guarda el tag instalado.

La primera vez, si ya existe `nextendo.nro` pero no hay archivo de estado, la version local se
muestra como `desconocida` y se ofrece instalar la release actual. A partir de la primera
instalacion hecha por este updater, la comparacion de versiones es automatica.

## Controles

- `A`: instalar una actualizacion disponible.
- `B`: cancelar / cancelar la descarga.
- `X`: reinstalar la release actual cuando ya esta actualizado.
- `+`: salir.

## Dependencias

Requiere devkitPro con:

```text
devkitA64
libnx
switch-curl
switch-sdl2
switch-sdl2_ttf
switch-freetype
switch-harfbuzz
switch-zlib
```

GitHub Actions instala automaticamente estas dependencias.

## Compilar

```sh
make -j$(nproc)
```

Salida:

```text
prelude-updater.nro
```

## Instalar en la SD

```text
/switch/Prelude-Updater/prelude-updater.nro
/switch/nextendo.nro
```

El directorio de datos `/switch/Prelude-Updater/` se crea automaticamente.

## Metadatos del NRO

```text
Titulo:   Prelude Updater
Autor:    RadiantDelux
Version:  1.1.0
Icono:    icon.jpg
```

## Seguridad e integridad

- Solo usa HTTPS.
- Mantiene activa la verificacion TLS de libcurl/libnx.
- Sigue como maximo 5 redirects.
- Descarga a un archivo temporal antes de tocar Prelude.
- Comprueba el tamano contra el asset publicado por GitHub.
- Conserva un backup recuperable durante la sustitucion.
- Si una operacion `rename()` falla en FAT, usa copia como fallback.

## Licencia y atribucion

AGPL-3.0-or-later.

El updater ya no reutiliza el cliente HTTPS de Prelude: v1.1.0 usa `switch-curl`. Se mantiene la
licencia AGPL del proyecto y la atribucion correspondiente a Prelude/Nextendo donde aplica.

Prelude-Nro: Copyright (C) 2026 Nextendo Network.

Este proyecto no esta afiliado con Nintendo ni con Nextendo Network.

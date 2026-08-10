# Prelude Updater

Homebrew independiente para Nintendo Switch que comprueba la ultima release publica de
[`NextendoNetwork/Prelude-Nro`](https://github.com/NextendoNetwork/Prelude-Nro) e instala su asset
`nextendo.nro` en:

```text
sdmc:/switch/nextendo.nro
```

## Funcionamiento

1. Consulta `releases/latest` de la API de GitHub por HTTPS.
2. Lee el tag semver (`vX.Y.Z`) y el tamano publicado del asset `nextendo.nro`.
3. Compara el tag con `sdmc:/switch/Prelude-Updater/installed_version.txt`.
4. Descarga a `sdmc:/switch/Prelude-Updater/nextendo.nro.new`.
5. Sigue redirects HTTPS de GitHub hasta el CDN del asset.
6. Verifica que el tamano descargado coincide con el publicado por GitHub.
7. Crea una copia de seguridad temporal del Prelude instalado.
8. Copia el nuevo NRO a `/switch/nextendo.nro` y vuelve a verificar su tamano.
9. Guarda el tag instalado para futuras comprobaciones.

La primera vez, si ya existe `nextendo.nro` pero no hay archivo de estado, la version local se
muestra como `desconocida` y se ofrece instalar la release actual. A partir de la primera
instalacion hecha por este updater, la comparacion de versiones es automatica.

## Controles

- `A`: instalar una actualizacion disponible.
- `B`: cancelar.
- `X`: reinstalar la release actual cuando ya esta actualizado.
- `+`: salir.

## Compilar

Requiere devkitPro con `devkitA64` y `libnx`.

```sh
make -j$(nproc)
```

Salida:

```text
prelude-updater.nro
```

Tambien puede compilarse con Docker:

```sh
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest \
  bash -lc 'make -j$(nproc)'
```

## Instalar en la SD

Una estructura recomendada es:

```text
/switch/Prelude-Updater/prelude-updater.nro
/switch/nextendo.nro
```

El directorio de datos `/switch/Prelude-Updater/` se crea automaticamente si no existe.

## Seguridad e integridad

- Solo acepta URLs `https://`.
- Mantiene activa la verificacion TLS del servicio SSL de la Switch.
- Sigue como maximo 5 redirects HTTPS.
- No reemplaza Prelude hasta haber terminado y validado la descarga.
- Conserva una copia temporal del NRO anterior durante la sustitucion.
- Verifica el tamano del archivo contra el valor publicado en GitHub Releases.

## Licencia y atribucion

Este proyecto es AGPL-3.0-or-later porque parte del transporte HTTPS esta derivado del codigo
publicado por Nextendo Network en Prelude-Nro, que usa esa licencia.

Prelude-Nro: Copyright (C) 2026 Nextendo Network.

Este proyecto no esta afiliado con Nintendo ni con Nextendo Network.
# Prelude-Updater

# Herramienta: de las ROM de MAME a las imágenes de PS1

Le das tus ROM de MAME de **World Rally** y **World Rally 2** y te saca los `.bin`/`.cue` de PS1:

- el disco con los dos juegos y el menú para elegir (`World Rally 1 y 2 PS1`);
- cada juego suelto (`World Rally PS1` y `World Rally 2 PS1`).

Hace todo el proceso de los ports desde cero en tu máquina: recompila el código de las placas, ejecuta las corridas
del banco de pruebas, prepara gráficos y sonido y monta las imágenes. Ni el repositorio ni la herramienta llevan
nada de las ROM ni de la BIOS. **Uso personal**: no distribuyas las imágenes que salen.

## Qué hace falta

- **Docker.** En Windows y Mac, [Docker Desktop](https://www.docker.com/products/docker-desktop/) abierto; en Linux,
  `docker` (o `podman`, con `DOCKER=podman`).
- **Unos 15 GB libres** (las corridas del banco del WR2 ocupan unos 10 GB) y **4 GB de RAM** para Docker.
- **Tiempo.** Unos 20 minutos con 4 núcleos (probado: 17). La primera vez, unos minutos más para preparar el
  contenedor.
- **Una carpeta con tus ROM**, en zip de MAME. Valen romsets merged, split y non-merged, porque cada ROM se reconoce
  por su CRC y no por su nombre:
  - `wrally.zip` y, si tu romset es split, `wrallyd.zip`. El port usa la versión 1.0, con checksum 0E56.
  - `wrally2.zip`, o `wrally2a.zip`, que trae los gráficos en 14 EPROM.
  - Los 7z hay que descomprimirlos antes.
- **Tu BIOS de PS1, opcional pero recomendada.** Por ejemplo `scph1001.bin` o `scph101.bin`, en la misma carpeta (se
  busca sola) o con `--bios`. Sirve para dos cosas:
  - **El logo de PlayStation al arrancar.** Sin él, las imágenes no arrancan en PSIO, XStation, PS1 PAL modernas ni
    japonesas. Sí arrancan en MiSTer y en consolas americanas.
  - **La música del WR2 en el menú del disco con los dos.** Se graba del port en un emulador que necesita la BIOS.

## Cómo se usa

1. Descarga el repositorio: con *Code → Download ZIP* en GitHub, o con `git clone`.
2. Lanza la herramienta.

   **Windows** (en una consola, desde la carpeta del repositorio):

       herramienta\crear.bat C:\ruta\a\mis\roms

   **Linux / Mac:**

       herramienta/crear.sh /ruta/a/mis/roms

3. Las imágenes quedan en la carpeta `salida`, junto a donde lo lanzaste. También se puede elegir otra carpeta como
   segundo argumento.

Si se corta, vuelve a lanzarlo y sigue donde iba: el trabajo se guarda en el volumen de Docker `wr-ps1-trabajo`.
Cuando acabes, puedes recuperar ese espacio con `docker volume rm wr-ps1-trabajo`.

## Qué sale

En `salida/` hay una carpeta por imagen, lista para copiar a la tarjeta (el nombre de los ficheros tiene que ser el
de la carpeta):

| Carpeta | Para |
|---|---|
| `World Rally 1 y 2 PS1 (USA)` | CD grabado, MiSTer y emuladores (`.bin` + `.cue`) |
| `World Rally 1 y 2 PS1 (USA) XStation` | XStation, con lecturas más rápidas en el WR2 |
| `World Rally 1 y 2 PS1 (USA) PSIO` | PSIO (`.bin` + `.cu2`) |
| `... (EUR) ...` | lo mismo con la licencia europea, para PS1 PAL |
| `World Rally PS1 (...)`, `World Rally 2 PS1 (...)` | cada juego en su disco |

Para grabar un CD: carga el `.cue` en el programa de grabación, en modo DAO / disc-at-once y a poca velocidad (4x-8x).

## Sin Docker (Linux)

`herramienta/crear_imagen.sh` es lo que corre dentro del contenedor. En un Linux con lo que instala el `Dockerfile`
(el compilador cruzado `mipsel-linux-gnu`, python3 con numpy, scipy y pillow, mkpsxiso, nugget, y mednafen, Xvfb y
ffmpeg para la música) también se puede lanzar directamente:

    NUGGET=/ruta/a/nugget MKPSXISO=/ruta/a/mkpsxiso herramienta/crear_imagen.sh \
        --bios scph1001.bin --salida salida --trabajo /tmp/wr-ps1 /ruta/a/mis/roms

Más opciones: `--regiones USA` (solo la versión americana; tarda menos) y `PROC=n` (procesos para las corridas).

## Cómo va por dentro

1. **`roms.py`** busca cada ROM por su CRC32 en los zip y carpetas que le des, también dentro de subcarpetas de zips
   merged, y las deja con el nombre que usan los ports. Si falta alguna, dice cuál, con su CRC.
2. **`wrally_ps1/construir.sh`** construye el WR1.
3. **`wrally2_ps1/construir.sh`** construye el WR2:
   - prepara el banco de pruebas;
   - convierte el sonido a la SPU;
   - hace las corridas con el piloto automático por los cuatro rallies y con los tres coches, de las que salen los
     paquetes de tiles y los bancos de ROM de cada tramo;
   - saca el perfil de memoria para el código recompilado;
   - compila el EXE.
4. **`recopilatorio/construir.sh`** monta el disco con los dos: portadas, música y menú.

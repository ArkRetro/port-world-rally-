# Herramienta: de las ROM de MAME a las imágenes de PS1

Le das tus ROM de MAME de **World Rally** y **World Rally 2** y te saca los `.bin`/`.cue` de PS1:

- el disco con los dos juegos y el menú para elegir (`World Rally 1 y 2 PS1`);
- cada juego suelto (`World Rally PS1` y `World Rally 2 PS1`).

Hace todo el proceso de los ports desde cero en tu máquina: recompila el código de las placas, ejecuta las corridas
del banco de pruebas, prepara gráficos y sonido y monta las imágenes. Ni el repositorio ni la herramienta llevan
nada de las ROM ni de la BIOS. **Uso personal**: no distribuyas las imágenes que salen.

## Windows: el programa (lo más sencillo)

`WorldRallyPS1-Creador.zip`: se descomprime y se abre **Crear imagenes PS1.exe**. En la ventana:

1. Eliges la carpeta de tus ROM.
2. Eliges dónde dejar las imágenes.
3. Pulsas **Crear imágenes**.

La primera vez descarga solo las herramientas libres que necesita (unos 200 MB). Todo trabaja en `C:\WRPS1`. Las
instrucciones para el usuario están en `windows/LEEME.txt`, que va dentro del zip.

El zip se genera con `windows/empaquetar.sh` (en Linux, con MinGW). Lleva tres cosas:

- **El programa** (`windows/creador.c`, Win32).
- **El Python embebido** de python.org.
- **`creador.py`** y una copia del código del port.

`creador.py` hace el trabajo:

- **Descarga, con su SHA-256:**
  - numpy, scipy y pillow;
  - w64devkit (gcc y el `sh` de busybox para Windows);
  - el gcc de MIPS de PSn00bSDK;
  - mkpsxiso;
  - los `.ld` de nugget.
- **Lanza** `crear_imagen.sh` con ese `sh`.

Las corridas del banco repiten las partidas grabadas del piloto automático (`tools/pilotos.txt` de cada port), porque
Windows no tiene `fork`.

## Qué ROM hacen falta

En zip de MAME. Valen romsets merged, split y non-merged, porque cada ROM se reconoce por su CRC y no por su nombre
(`roms.py`):

- `wrally.zip` y, si tu romset es split, `wrallyd.zip`. El port usa la versión 1.0, con checksum 0E56.
- `wrally2.zip`, o `wrally2a.zip`, que trae los gráficos en 14 EPROM.
- Los 7z hay que descomprimirlos antes.

Recomendado: **tu BIOS de PS1** (`scph1001.bin`, `scph101.bin`...) en la misma carpeta; se busca sola. Con ella las
imágenes llevan el logo de PlayStation al arrancar, que hace falta para PSIO, XStation, PS1 PAL modernas y japonesas.
Sin ella funcionan en MiSTer y en consolas americanas.

## Qué sale

Una carpeta por imagen, lista para copiar a la tarjeta (el nombre de los ficheros tiene que ser el de la carpeta):

| Carpeta | Para |
|---|---|
| `World Rally 1 y 2 PS1 (USA)` | CD grabado, MiSTer y emuladores (`.bin` + `.cue`) |
| `World Rally 1 y 2 PS1 (USA) XStation` | XStation, con lecturas más rápidas en el WR2 |
| `World Rally 1 y 2 PS1 (USA) PSIO` | PSIO (`.bin` + `.cu2`) |
| `... (EUR) ...` | lo mismo con la licencia europea, para PS1 PAL |
| `World Rally PS1 (...)`, `World Rally 2 PS1 (...)` | cada juego en su disco |

Para grabar un CD: carga el `.cue` en el programa de grabación, en modo DAO / disc-at-once y a poca velocidad (4x-8x).

## Otras formas

- **Docker (Windows, Linux, Mac):**

      herramienta/crear.sh /ruta/a/mis/roms      # o herramienta\crear.bat en Windows

  Las imágenes quedan en `salida/`. El trabajo se guarda en el volumen `wr-ps1-trabajo`; para recuperar el espacio,
  `docker volume rm wr-ps1-trabajo`.
- **Linux sin Docker:** lo que instala el `Dockerfile` es el compilador cruzado `mipsel-linux-gnu`, python3 con numpy,
  scipy y pillow, mkpsxiso y nugget. Con eso:

      NUGGET=/ruta/a/nugget MKPSXISO=/ruta/a/mkpsxiso herramienta/crear_imagen.sh \
          --bios scph1001.bin --salida salida --trabajo /tmp/wr-ps1 /ruta/a/mis/roms

  Opciones: `--regiones USA` (solo la versión americana; tarda menos).

Hacen falta unos 15 GB libres: las corridas del banco del WR2 ocupan unos 10.

## Cómo va por dentro

1. **`roms.py`** busca cada ROM por su CRC32 en los zip y carpetas que le des, también dentro de subcarpetas de zips
   merged, y las deja con el nombre que usan los ports. Si falta alguna, dice cuál, con su CRC.
2. **`wrally_ps1/construir.sh`** construye el WR1.
3. **`wrally2_ps1/construir.sh`** construye el WR2:
   - prepara el banco de pruebas;
   - convierte el sonido a la SPU;
   - hace las corridas con las partidas grabadas por los cuatro rallies y con los tres coches, de las que salen los
     paquetes de tiles y los bancos de ROM de cada tramo;
   - saca el perfil de memoria para el código recompilado;
   - compila el EXE.
4. **`recopilatorio/construir.sh`** monta el disco con los dos: portadas, música y menú. La música del WR2 se saca del
   port con `psxprof2`, sin emulador ni BIOS.

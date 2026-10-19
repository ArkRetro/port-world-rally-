# World Rally → Neo Geo (AES / MVS) — port `wrng`

> **Versión recomendada: [`../wrally_final/`](../wrally_final/)** — el port "WRally neogeo final" de arkretro
> (probado en MiSTer / NeoSD). Se compila con `wrally_final/compilar.sh <carpeta ROM wrally>`.
> `wrng` queda como port experimental.


Port de **World Rally** (Gaelco, 1993) a Neo Geo hecho desde cero: un programa Neo Geo limpio en
ensamblador 68000 que aloja la lógica original del juego arcade (reubicada y parcheada en tiempo
de construcción) sobre una capa de hardware nueva. Todo el código vive en P1, se respetan las
convenciones de la BIOS (vectores, cabecera, código de seguridad, USER/PLAYER_START, SYSTEM_IO,
watchdog) y el programa nunca escribe en el espacio de ROM, de modo que funciona igual en MVS, en
AES y en flashcarts.

## Estado

| Área | Estado |
|------|--------|
| Self-test, intro, título, atracción | Completo, a velocidad real |
| Partida (MVS: moneda + start; AES: start) | Funciona; volante/acelerador/marcha en el joystick |
| Gráficos | Tilemaps emulados con sprites-columna, sprites, sombras (dither), paleta convertida, prioridad de tiles de categoría 1 y de HUD/textos, fondo opaco de la capa 1 (columnas de color 0); VRAM escrita siempre en el VBlank (el trabajo largo de un frame es interrumpible) |
| Sonido | Driver Z80 propio sobre ADPCM-A (4 canales = 4 canales del OKI M6295), mismo secuenciador que el arcade |
| MCU DS5002FP | Reimplementado en 68000 (`src/mcu.s`: intérprete de scripts y autopiloto) |
| Pantalla | Ancho completo: los 368 píxeles del arcade se muestran en 320 encogiendo todos los sprites a 14 píxeles (shrink horizontal del LSPC, posiciones x 7/8; también corrige la proporción). Columnas de capa vacías con altura 0 para no gastar el límite de 96 sprites por línea |
| Protección | Además del MCU, el programa suma zonas de su propio código (FEF502, FEF520, FEF53C, FEF846, más FEF80C sobre los datos) y se sabotea si no cuadran: coche que gira solo, no choca con los márgenes, saltos en los que sale volando. `mkrom.py` sustituye esos bucles por las sumas originales |
| Pendiente | Presupuesto de CPU: en los tramos cargados el juego va algo por debajo de la velocidad del original (~96 % en la prueba de referencia) |

## Controles

| Neo Geo | Arcade |
|---------|--------|
| Joystick izquierda/derecha | Volante |
| A o C | Acelerador |
| B | Cambio de marcha (alterna; arranca en la misma posición que el arcade) |
| Start | Start (en MVS hace falta crédito; se usa el contador de créditos de la BIOS) |

## Construcción

Herramientas: binutils m68k (`m68k-linux-gnu-as/ld/objcopy/nm`), SDCC (`sdasz80`, `sdldz80`,
`makebin`), Python 3 con Pillow. Entrada que **no** está en el repositorio:

* `ARCADE=` directorio con el set `wrally` de MAME descomprimido (`worldr16.c22`, `worldr17.c23` en
  `wrallyd/`, gráficos `worldr18.i07`…`worldr21.i13`, OKI `worldr14.c01`/`worldr15.c03`,
  `wrdallas.bin`).

`src/seccode.bin` son los 122 bytes del código de seguridad estándar de la cabecera del cartucho
(los mismos que incluye cualquier cartucho; en la BIOS están en `0xC146A2`).

```sh
make game ARCADE=/ruta/a/wrally       # ROMs en build/ y set MAME en build/roms/wrng + build/hash/neogeo.xml
make neo  ARCADE=/ruta/a/wrally       # build/release/wrng.neo (NeoSD) y build/release/wrng.zip (set MAME)
make viewer DUMPS=/ruta/a/dumps       # programa de pruebas que muestra volcados de pantallas del arcade
```

Ejecución en MAME (MVS y AES):

```sh
mame neogeo wrng -rompath "/ruta/bios;$(pwd)/build/roms" -hashpath $(pwd)/build/hash
mame aes    wrng -rompath "/ruta/bios;$(pwd)/build/roms" -hashpath $(pwd)/build/hash
```

El `.neo` se copia tal cual a la tarjeta del NeoSD. Para Darksoft/BackBit puede usarse
`../tools/builds/pack_formats.py build/release/wrng.neo <dir> wrng "World Rally NG"`.

### Kit para distribuir (sin datos del arcade)

`make kit ARCADE=...` deja en `build/release/` el kit `wrng-kit.zip`, la herramienta `wrng_build.py` y
`wrng.html`.

**Para quien no use Python:** `wrng.html` es una página web con el kit y la herramienta dentro. Basta con
abrirla en el navegador, soltar `wrally.zip` (y `wrallyd.zip` si se tiene aparte) y pulsar «Generar wrng.neo».
Python se ejecuta en el propio navegador con Pyodide, que se descarga una vez de cdn.jsdelivr.net (unos
15 MB). Las ROMs no salen del ordenador. Tarda menos de un minuto y el resultado es idéntico al de la línea de
comandos.

**Línea de comandos:** El kit
lleva sólo el trabajo del port: el binario del runtime y el driver Z80 con a cero todas las zonas que salen de
las ROMs del arcade, la fuente del fix, los scripts de conversión y un manifiesto con el SHA-1 que cada zona
debe tener al reconstruirla. El usuario pone su propio set de MAME y obtiene el `.neo`:

```sh
python3 wrng_build.py wrng-kit.zip wrally.zip [wrallyd.zip] -o wrng.neo [--mame wrng.zip]
```

* Sólo necesita Python 3.8 o posterior, sin paquetes extra. Tarda unos 25 s.
* Vale el set `wrally` fusionado (un `wrally.zip` que incluye `wrallyd/`) o dividido (`wrally.zip` +
  `wrallyd.zip`). También valen carpetas. Los ficheros se buscan por CRC32, se llamen como se llamen.
* Las zonas reconstruidas se comprueban con el manifiesto. Si el set no es el esperado, la herramienta lo dice
  y no genera nada. Si todo cuadra, el `.neo` sale idéntico byte a byte al oficial, y la herramienta lo
  indica.
* `--mame` escribe además el set de MAME (`wrng.zip`) y su `neogeo.xml` al lado
  (`mame neogeo wrng -rompath <carpeta> -hashpath <carpeta>`).
* No hace falta la BIOS de Neo Geo: el `.neo` es sólo el cartucho. El NeoSD usa la BIOS de la consola, y
  MAME, su `neogeo.zip`.

## Estructura

```
src/header.s   vectores, cabecera del cartucho, código de seguridad, soft dips
src/main.s     USER, PLAYER_START, VBlank (SYSTEM_IO, watchdog, vídeo, sonido, hook del juego), errores
src/video.s    motor de columnas (tilemaps → sprites), sprites, paleta, write-through de VRAM
src/glue.s     arranque del arcade, E/S (joystick → volante), moneda/start, detours de bloque/borrado
src/mcu.s      reimplementación del DS5002FP (scripts de carrera, autopiloto)
src/decrypt.s  descifrado Gaelco de las palabras de tile escritas en VRAM
src/sound.s    hooks del secuenciador OKI del arcade → órdenes al Z80
src/fix.s      texto en la capa fix (depuración)
src/eye.s      pantalla de arranque (eyecatcher de la BIOS): logo GAELCO, MAX 330 MEGA / PRO-BLOSTE SPEC
src/viewer.s   programa de pruebas (visor de volcados)
z80/driver.s   driver de sonido Z80 (ADPCM-A, protocolo con ack, canal + volumen)
tools/mkrom.py parcheador del programa del arcade y ensamblado de la P-ROM de 2 MB
tools/gfx2crom.py, mkfont.py, mklogo.py, oki2adpcma.py, mksoftlist.py, mkneo.py
analysis/      informes y tablas del análisis estático del 68k y del MCU
```

### Cómo se integra el programa del arcade (`tools/mkrom.py`)

* Los datos cifrados (metatiles, imágenes, animaciones) se descifran de antemano; los datos de
  pista pasan a la segunda mitad de la P-ROM.
* Las referencias absolutas al hardware del arcade (RAM `FEC000`, paleta `200000`, sprites
  `440000`, E/S `700000`) se reubican a RAM de trabajo (`src/ram.i`).
* Las escrituras en VRAM se convierten en llamadas a `vram_store` (stubs `jsr` o `trap #1`), que
  escriben el tile ya descifrado en el sprite-columna correspondiente.
* Las rutinas de copia/borrado de bloques y las escrituras al OKI se desvían a versiones nativas.
* La firma del MCU se parchea y la petición por frame al MCU llama a `mcu_frame`.

## Pruebas

Las pruebas se han hecho en MAME 0.264 (`neogeo`, `aes`) con scripts Lua: atracción completa,
partida MVS/AES, comparación de VRAM/paleta/registros con el arcade (volcados en paso cerrado) y
comparación de la envolvente de audio con el arcade.

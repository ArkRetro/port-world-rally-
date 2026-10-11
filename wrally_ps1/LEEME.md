# World Rally (Gaelco, 1993) → PlayStation — código fuente (arkretro)

Port **para uso personal**. El paquete no incluye ninguna ROM, BIOS ni dato sacado de ellas.
Todo lo del juego (programa, MCU, gráficos, sonido) se genera en tu máquina a partir de **tus** ROM de MAME (`wrally`).
No distribuyas las imágenes de CD que salen.

## Requisitos
- Linux con `python3` (numpy, scipy), `gcc`, `mipsel-linux-gnu-gcc` (binutils/gcc cruzados para MIPS).
- **nugget** (de pcsx-redux, `src/mips`): `crt0.s`, `ps-exe.ld`, `nooverlay.ld`. Ruta en `NUGGET`.
- **mkpsxiso** (en `MKPSXISO` o en el PATH).
- ROM de MAME `wrally`: `worldr14.c01 worldr15.c03 worldr16.c22 worldr17.c23 worldr18.i07 worldr19.i09 worldr20.i11 worldr21.i13 wrdallas.bin` en `WR_ROM` (por defecto `./rom`).

## Construir
    WR_ROM=/ruta/a/wrally NUGGET=/ruta/a/nugget MKPSXISO=/ruta/a/mkpsxiso ./construir.sh

Salen `ps1/wrps1_USA.cue/.bin` (MiSTer con BIOS americana) y `ps1/wrps1_EUR.cue/.bin` (PSone PAL).

## Estructura
- `tools/prep.py`: programa del 68000, MCU y tiles a partir de las ROM (`work/`).
- `rc/dec68.py`, `rc/rc68.py`: recompilador estático 68000 → C.
  - Las direcciones de las instrucciones están en `code_addrs.txt`.
  - Las banderas solo se calculan cuando hacen falta.
  - Los registros van fijos en registros del MIPS.
  - La IRQ de vblank se atiende por presupuesto de ciclos.
  - Bucles internos con mucho trabajo en el banco de pruebas (`work/vueltas.txt`, de `rc/regiones.py`): además del código normal, una copia rápida que, tras comprobar al entrar cuántas vueltas caben en la región de cada puntero, accede sin comprobar, lleva los Dn de 16 bits sin mezclar con la parte alta y mira la IRQ con un signo. Mismos ciclos del 68000 y mismo estado.
- `rc/rc51c.py`: traducción 8051 → C de la tarea del DS5002FP. `rc/mcs51.c` es el intérprete 8051 que se usa para el arranque.
- `rc/rc_rt.h`, `rc/rc_mem.c`: entorno del código traducido.
  - Mapa de memoria.
  - Descifrado de la VRAM de Gaelco (`gcrypt.c`, algoritmo de MAME).
  - Modelo del OKI.
- `rc/rc_host.c`: banco de pruebas en el PC.
  - Con `-DRCDBG` y Musashi (MIT, no incluido) compara instrucción a instrucción.
  - Con `-DBLKLOG` registra los tiles usados por cuadro.
- `ps1/main.c`: vídeo y mando.
  - Vídeo a 368×240 y 60 Hz.
  - Caché de tiles en VRAM.
  - Prioridades de la placa.
  - Faros y sombras con semitransparencia.
  - Pantalla de carga.
  - La lista de la GPU se hace en una sola pasada por las capas y los sprites (un cubo por paso de dibujo).
  - Si un cuadro llega tarde, el siguiente no se dibuja y el juego sigue a 60 Hz (`FRAMESKIP`, `-DFRAMESKIP=0` lo quita).
- `ps1/cd.c`: lector de CD por sondeo (datos y CD-DA).
  - Caché: en carrera quedan fijos los paquetes del recorrido y de carrera; al salir (el fondo deja de ser el mapa) se vuelven a leer de fondo los de menús.
  - Lo que falte se pide a `TILES.BIN` sin esperar; si suena una frase de CD-DA y faltan muchos tiles, se corta y luego sigue.
- `ps1/snd.c`: el OKI M6295 sobre la SPU (frases cortas) y sobre pistas de audio (frases largas).
- `ps1/gen2.py`: genera `TILES.BIN`, los paquetes `LEVELnn.BIN` y `RACE.BIN`.
- `snd/sndps1.py`, `snd/psxadpcm.c`: muestras a PS-ADPCM y pistas WAV.
- `ps1/mkiso.py`: genera las dos imágenes.
- `perf/`: medidas con el emulador `psxprof` (modelo de tiempos aproximado).
  - `medir.sh`: ritmo y perfil del recorrido estándar.
  - `escenarios.sh`: segunda partida, attract largo, dirección oscilante; compara cuadro a cuadro con la versión original (`BASE`) y cuenta los tiles que faltan en pantalla.

## Mandos
| Botón | Acción |
|---|---|
| SELECT | moneda |
| START | empezar |
| X | acelerar |
| Cuadrado | marcha |
| cruceta | dirección |
| L2 | cifras de rendimiento |

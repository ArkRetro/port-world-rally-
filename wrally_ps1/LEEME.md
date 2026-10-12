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
    WR_ROM=/ruta/a/wrally NUGGET=/ruta/a/nugget MKPSXISO=/ruta/a/mkpsxiso LICENCIA=/ruta/a/scph101.bin ./construir.sh

`LICENCIA` (opcional) es para el logo de PlayStation del arranque: la BIOS solo lo dibuja si los sectores 5-11 del
disco son iguales al que lleva dentro, así que se saca de **tu** BIOS al construir (`ps1/licencia.py`; también vale un
`LICENSEA.DAT` o la imagen de un juego original). Sin ella la imagen arranca igual, pero sin el logo.

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
  - Con `-DBLKLOG` registra los tiles usados por cuadro (attract, una partida en cada rally hasta los records, con la dirección oscilante y con el piloto automático).
  - Botones por cuadro como `psxprof`: `NOCOIN`, `STEER` e `INPUTS` (guion de botones; p. ej. elegir rally).
  - Piloto automático (`AUTOP=a-b`): cada 10 cuadros prueba en procesos hijos girar a un lado, al otro, a ratos o nada durante 150 cuadros y sigue con lo que más avanza en el tramo. Así las partidas llegan a la meta de los tres tramos de cada rally (y siguen con el rally siguiente). Al acabar escribe el guion `INPUTS` que ha seguido, que `psxprof` repite igual con `-DOKI_CUADROS`.
- `ps1/main.c`: vídeo y mando.
  - Vídeo a 368×240 y 60 Hz.
  - Caché de tiles en VRAM.
  - Prioridades de la placa.
  - Faros y sombras con semitransparencia.
  - Pantalla de carga al estilo del juego (bandas de bandera a cuadros, CARGANDO con el degradado de las cifras del marcador y la barra START-FINISH). Al arrancar, encima, el logo de `ps1/logo_pina.png` (después del de PlayStation de la BIOS, si se construye con `LICENCIA`).
  - La lista de la GPU se hace en una sola pasada por las capas y los sprites (un cubo por paso de dibujo).
  - Si un cuadro llega tarde, el siguiente no se dibuja y el juego sigue a 60 Hz (`FRAMESKIP`, `-DFRAMESKIP=0` lo quita).
- `ps1/cd.c`: lector de CD por sondeo (datos y CD-DA).
  - Caché: en carrera quedan fijos los paquetes del recorrido y de carrera, y los de menús guardados (se sustituyen antes que los fijos y después de lo demás); al salir (el fondo deja de ser el mapa) los de menús vuelven a ser fijos y se leen de fondo, y el de carrera queda guardado. En carrera lo fijo no se sustituye nunca (antes, lo que no se usaba en 10 s sí, y en la meta faltaban trozos).
  - Los tiles iguales a otro del paquete (o del de carrera) no llevan datos: en carrera usan el hueco de ese (de 250 a 380 por recorrido).
  - En carrera, con el CD libre, se leen de antemano los sectores de `TILES.BIN` de al lado de los sprites del coche y del haz de los faros que se ven: sus poses van de 16 en 16 tiles en el orden en que se suceden.
  - Al cruzar la meta (se para el reloj de la carrera) se sueltan los sprites del jugador que no se ven y se lee de fondo `POST` ('Your Time' sale entero). Cuando el juego funde la paleta a negro (6 bajadas seguidas de la luminosidad, sin subir) lo de la carrera deja de ser fijo y se leen `POST2` y los menús. Al salir, los tiles que compartían hueco con uno de la carrera se copian a uno propio (en la VRAM) antes de soltar ese hueco.
  - Lo que falte se pide a `TILES.BIN` sin esperar; si suena una frase de CD-DA y faltan muchos tiles en pantalla, se corta y luego sigue. Fuera de la carrera, una frase que acaba de empezar se corta mientras se lee un paquete (como mucho 2,5 s; no con `MENU2`, que tarda más) y sigue donde iba; en la meta, como mucho 20 cuadros para `POST`.
- Lectura del CD:
  - Al arrancar se lee `MENU` a doble velocidad y luego, de fondo, `SNDFX` (los efectos que aún no han llegado no suenan).
  - Cada `LEVELnn` lleva primero lo que sale al empezar y detrás los sprites del jugador, que se leen de fondo ya en carrera.
  - Detrás de cada `LEVELnn` hay una copia de `RACE.BIN` (`ZRACEnn.BIN`): lo que falte de él se lee sin otra búsqueda.
  - `mkiso.py` comprueba que `SYSTEM.CNF` y el ejecutable estén en el primer sector del directorio raíz: la BIOS no lee más (si no, se queda en su logo).
  - Opciones sin probar en MiSTer: `-DCD_DMA` (datos por DMA) y `-DCD_SIN_INIT` (sin Init al arrancar).
- `ps1/snd.c`: el OKI M6295 sobre la SPU (frases cortas) y sobre pistas de audio (frases largas).
  - Las frases que el juego encadena (la música de los menús y de 'Your Time') van seguidas en una misma pista: la siguiente sigue sin parar la unidad ni buscar.
  - Pausa (START en carrera): calla las voces y el CD, oscurece la pantalla con PAUSA y el juego queda parado; al seguir, todo continúa donde estaba.
- `ps1/gen2.py`: genera `TILES.BIN` y los paquetes `LEVELnn.BIN`, `RACE.BIN`, `MENU.BIN`, `MENU2.BIN`, `POST.BIN` y `POST2.BIN` a partir de los registros del banco de pruebas. Cada recorrido se llena hasta la capacidad con los sprites del jugador (poses del coche, marcador y, de noche, el haz de los faros), los más vistos primero. Fuera de la carrera los menús llenan la caché (sustituyen a lo de carrera, que se vuelve a leer al empezar). `POST`/`POST2`: lo que sale al acabar una carrera ('Your Time', 'Continue ?', las fotos del attract).
- `ps1/logo.py`: el logo de arranque a `LOGO.BIN` (15 bits, 128 de ancho).
- `snd/sndps1.py`, `snd/psxadpcm.c`: muestras a PS-ADPCM y pistas WAV (el ADPCM del OKI se pasa a muestras lineales; `CADENAS`: frases encadenadas en una pista).
- `ps1/mkiso.py`: genera las dos imágenes.
- `perf/`: medidas con el emulador `psxprof` (modelo de tiempos aproximado).
  - `medir.sh`: ritmo y perfil del recorrido estándar.
  - `escenarios.sh`: partida, segunda partida, attract, dirección oscilante, los otros rallies, attract de 8 minutos, partida hasta los records y segunda partida en otro rally; compara cuadro a cuadro con la versión original (`BASE`), cuenta los tiles que faltan en pantalla y lo resume por escenario (`tabla.py`).
  - Referencia de la placa: `psxprof` con `DUMPAT` guarda, por cuadro, la imagen que enseña la PS1 y el estado de la placa con el que se dibujó (y `INPUTS` lleva el mando: elegir rally, conducir).
    - `arcade.py` dibuja ese estado como MAME (y como `ps1/main.c`).
    - `difref.py` compara las dos imágenes y atribuye cada diferencia a sprites y tiles (falta, sobra, prioridad, color).
  - `psxprof`: `INPUTSV` es como `INPUTS` pero por vblank (para pulsar START en carrera: la pausa no deja avanzar los cuadros del juego); `CLUTCACHE=1` imita la caché de paleta de una entrada de la GPU de MiSTer.
  - `cortes.py`: cortes en el sonido (no cuenta el paso de una frase a la siguiente de una cadena).

## Mandos
| Botón | Acción |
|---|---|
| SELECT | moneda |
| START | empezar; en carrera, pausa (otra vez para seguir) |
| X | acelerar |
| Cuadrado | marcha |
| cruceta | dirección |
| L2 | cifras de rendimiento |

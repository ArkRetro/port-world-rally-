# World Rally (Gaelco, 1993) → PlayStation — código fuente (arkretro)

Port **para uso personal**. El paquete no incluye ninguna ROM, BIOS ni dato sacado de ellas.
Todo lo del juego (programa, MCU, gráficos, sonido) se genera en tu máquina a partir de **tus** ROM de MAME (`wrally`).
No distribuyas las imágenes de CD que salen.

## Requisitos
- Linux con `python3` (numpy, scipy), `gcc`, `mipsel-linux-gnu-gcc` (binutils/gcc cruzados para MIPS).
- **nugget** (de pcsx-redux, `src/mips`): `ps-exe.ld`, `nooverlay.ld`. Ruta en `NUGGET`.
- **mkpsxiso** (en `MKPSXISO` o en el PATH).
- ROM de MAME `wrally`: `worldr14.c01 worldr15.c03 worldr16.c22 worldr17.c23 worldr18.i07 worldr19.i09 worldr20.i11 worldr21.i13 wrdallas.bin` en `WR_ROM` (por defecto `./rom`).

## Construir
    WR_ROM=/ruta/a/wrally NUGGET=/ruta/a/nugget MKPSXISO=/ruta/a/mkpsxiso LICENCIA=/ruta/a/scph101.bin ./construir.sh

`LICENCIA` (opcional) es para el logo de PlayStation del arranque: la BIOS solo lo dibuja si los sectores 5-11 del
disco son iguales al que lleva dentro, así que se saca de **tu** BIOS al construir (`ps1/licencia.py`; también vale un
`LICENSEA.DAT` o la imagen de un juego original). Sin ella arranca en consolas americanas (sin el logo), pero no en
las PAL con BIOS 4.0E o posterior (SCPH-7002/7502/9002/102) ni en las japonesas, que comparan el logo con el suyo:
para una PS1 real con PSIO o XStation construye siempre con `LICENCIA`.

Salen `ps1/wrps1_USA.cue/.bin` (MiSTer con BIOS americana) y `ps1/wrps1_EUR.cue/.bin` (PSone PAL), y en `ps1/sd/` las
carpetas para la tarjeta de PSIO o XStation (`SD_NOMBRE` cambia el nombre; por defecto `World Rally PS1`).

## PS1 real con PSIO o XStation
- Consolas: PSIO necesita el puerto paralelo (SCPH-1000 a 7503) y su Switch Board; XStation va en placas PU-7, PU-8 y
  PU-18 (SCPH-1000 a 5903). La PSone (SCPH-101/102) no admite ninguno.
- Tarjeta (FAT32 o exFAT): copia a la raíz la carpeta de tu sistema y región, tal cual:

      World Rally PS1 (USA)/        .bin y .cue   -> XStation (y CD grabado)
      World Rally PS1 (USA) PSIO/   .bin y .cu2   -> PSIO

  El nombre de los ficheros tiene que ser el de la carpeta (la línea FILE del .cue y el .cu2 dependen de él): para
  cambiarlo, `SD_NOMBRE` al construir (hasta 45 caracteres ASCII en total, sin puntos ni `\ / : * ? " < > |`).
- PSIO no lee el .cue: sin el `.cu2` no hay pistas de audio. Su imagen es otra: el CD-DA de PSIO no pasa por el lector
  de la consola sino por la entrada externa de la SPU (la del puerto paralelo), y el EXE de esa imagen la enciende (el
  menú de PSIO lo hace con un gancho en el kernel que aquí no correría: el juego va sin interrupciones). Usa el
  firmware al día (2.6.11 o posterior para el `.cu2`; con uno más antiguo, construye con `CU2_REV=1`) y arranca en
  modo Midboot.
- XStation: firmware 2.0.2 (la beta 2.2.0 tiene fallos con el CD-DA). Tras copiar, «Refresh game list». Prueba primero
  con FULL LOAD y luego con FAST LOAD.
- Región: cualquiera de las dos imágenes arranca en cualquier consola con PSIO o XStation (solo cambia el texto de
  licencia), siempre que se haya construido con `LICENCIA`. El juego sale siempre en NTSC a 60 Hz: en una consola PAL
  usa RGB.
- Si algo falla, anota la consola, el firmware del ODE, el modo de arranque (Midboot/LoadExec, FULL/FAST LOAD) y la
  tarjeta, y mira las cifras de L2 (errores del CD).

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
  - Reloj del juego y saltos progresivos: si el juego va atrasado más de un vblank, se dejan de dibujar hasta 3 cuadros
    seguidos (antes uno y nunca dos seguidos). Un cuadro del juego por vblank (59,83 en NTSC): unas décimas por minuto
    más deprisa que la placa. `-DRITMO=59100` va a 59,1, pero repite un cuadro de cada ~81 (un tirón cada ~1,4 s).
- `ps1/crt0.s`: arranque del EXE: sin interrupciones y con la pila en su sitio antes de borrar la BSS (un cargador de
  ODE puede saltar al EXE con las interrupciones activas).
- `ps1/cd.c`: lector de CD por sondeo (datos y CD-DA).
  - Para el hardware real y los ODE (XStation usa el controlador de la consola; PSIO lo imita): registros por KSEG1
    (sin la cola de escritura de la CPU), HINTSTS leído hasta que dos lecturas coinciden (en placas antiguas las
    banderas cambian sin sincronizar), una Pause rechazada mientras la unidad aún busca (INT5 80h) se repite a los
    2 cuadros, el CD-DA cuenta desde que suena de verdad (bit 7 del stat, con GetStat cada cuadro tras Play, y no
    desde su primera respuesta; tras un cambio de velocidad, 3 cuadros antes de lo siguiente), tras un error de lectura se esperan 2 cuadros antes de reintentar (0,25 s si es «aún no lista») y solo los errores
    seguidos abandonan la lectura (80h, «aún no lista», no cuenta). Al empezar: tiempos del bus como la BIOS, lo que
    dejara pendiente el cargador se reconoce y vacía, e Init se repite si no contesta. Los ficheros del disco se
    buscan hasta 3 veces.
  - Los 2 sectores que llegan justo al acabar o cortar una lectura (la unidad sigue un momento) se guardan: si la
    siguiente empieza por ellos (los paquetes de fondo que van seguidos en el disco: SNDFX, RACE y MENU2; POST y
    POST2) se dan de ahí y la unidad sigue sin volver a buscar.
  - El temporizador 1 (líneas de vídeo) se lee hasta que dos lecturas coinciden (cuenta con el reloj de la GPU).
  - Caché: en carrera quedan fijos los paquetes del recorrido y de carrera, y los de menús guardados (se sustituyen antes que los fijos y después de lo demás); al salir (el fondo deja de ser el mapa) los de menús vuelven a ser fijos y se leen de fondo, y el de carrera queda guardado. En carrera lo fijo no se sustituye nunca (antes, lo que no se usaba en 10 s sí, y en la meta faltaban trozos).
  - Los tiles iguales a otro del paquete (o del de carrera) no llevan datos: en carrera usan el hueco de ese (de 250 a 380 por recorrido).
  - En carrera, con el CD libre, se leen de antemano los sectores de `TILES.BIN` de al lado de los sprites del coche y del haz de los faros que se ven: sus poses van de 16 en 16 tiles en el orden en que se suceden.
  - Al cruzar la meta (se para el reloj de la carrera) se sueltan los sprites del jugador que no se ven y se lee de fondo `POST` ('Your Time' sale entero). Cuando el juego funde la paleta a negro (6 bajadas seguidas de la luminosidad, sin subir) lo de la carrera deja de ser fijo y se leen `POST2` y los menús. Al salir, los tiles que compartían hueco con uno de la carrera se copian a uno propio (en la VRAM) antes de soltar ese hueco.
  - Lo que falte se pide a `TILES.BIN` sin esperar; si suena una frase de CD-DA y faltan muchos tiles en pantalla, se corta y luego sigue. Fuera de la carrera, una frase que acaba de empezar se corta mientras se lee un paquete (como mucho 2,5 s; por `MENU2`, que tarda más, solo tras la demostración del attract, hasta 4 s: las fotos del título que vienen después son de `MENU2`) y sigue donde iba; en la meta, como mucho 20 cuadros para `POST`.
- Lectura del CD:
  - Al arrancar se lee `MENU` a doble velocidad y luego, de fondo, `SNDFX` (los efectos que aún no han llegado no suenan).
  - Cada `LEVELnn` lleva primero lo que sale al empezar y detrás los sprites del jugador, que se leen de fondo ya en carrera.
  - Detrás de cada `LEVELnn` hay una copia de `RACE.BIN` (`ZRACEnn.BIN`): lo que falte de él se lee sin otra búsqueda.
  - `mkiso.py` comprueba que `SYSTEM.CNF` y el ejecutable estén en el primer sector del directorio raíz: la BIOS no lee más (si no, se queda en su logo).
  - Opciones sin probar en MiSTer: `-DCD_DMA` (datos por DMA) y `-DCD_SIN_INIT` (sin Init al arrancar).
- `ps1/snd.c`: el OKI M6295 sobre la SPU (frases cortas) y sobre pistas de audio (frases largas).
  - Las duraciones de las frases de CD-DA (75 sectores por segundo) se pasan a cuadros con la frecuencia medida al
    arrancar contra el temporizador 2: 59,83 cuadros por segundo en una consola NTSC, 59,28 en una PAL.
  - `ode_cfg`: en la imagen para PSIO (`mkiso.py` cambia ese byte del EXE) el CD-DA entra por la entrada externa de la
    SPU (I2SB); en las demás, apagada.
  - Las frases que el juego encadena (la música de los menús y de 'Your Time') van seguidas en una misma pista, con 4 sectores (53 ms) de silencio entre una y otra: la siguiente sigue sin parar la unidad ni buscar; si el juego no la pide, se para en ese silencio (el tiempo cuenta desde que la unidad empezó a sonar). Al cortar o parar una frase el volumen del CD baja a 0 en el acto.
  - Pausa (START en carrera): calla las voces y el CD, oscurece la pantalla con PAUSA y el juego queda parado; al seguir, todo continúa donde estaba.
- `ps1/gen2.py`: genera `TILES.BIN` y los paquetes `LEVELnn.BIN`, `RACE.BIN`, `MENU.BIN`, `MENU2.BIN`, `POST.BIN` y `POST2.BIN` a partir de los registros del banco de pruebas. Cada recorrido se llena hasta la capacidad con los sprites del jugador (poses del coche, marcador y, de noche, el haz de los faros), los más vistos primero. Fuera de la carrera los menús llenan la caché (sustituyen a lo de carrera, que se vuelve a leer al empezar). `POST`/`POST2`: lo que sale al acabar una carrera ('Your Time', 'Continue ?', las fotos del attract).
- `ps1/logo.py`: el logo de arranque a `LOGO.BIN` (15 bits, 128 de ancho).
- `snd/sndps1.py`, `snd/psxadpcm.c`: muestras a PS-ADPCM y pistas WAV (el ADPCM del OKI se pasa a muestras lineales; `CADENAS`: frases encadenadas en una pista).
- `ps1/mkiso.py`: genera las dos imágenes, las de PSIO (el EXE con `ode_cfg` a 1) y las carpetas de `ps1/sd/` con el
  `.cue` (CRLF, con el nombre de la carpeta) y el `.cu2` de PSIO (igual byte a byte que el de Cue2cu2; `CU2_REV=1` para
  firmware anterior a 2.6.11). Detrás de la última pista de música va una pista de 2 s de silencio, para que acabe
  como las demás (pausa automática) y no en el final del disco. Comprueba que `SYSTEM.CNF` y el ejecutable estén en
  el primer sector del directorio raíz.
- `perf/`: medidas con el emulador `psxprof` (modelo de tiempos aproximado).
  - `medir.sh`: ritmo y perfil del recorrido estándar.
  - `escenarios.sh`: partida, segunda partida, attract, dirección oscilante, los otros rallies, attract de 8 minutos, partida hasta los records y segunda partida en otro rally; compara cuadro a cuadro con la versión original (`BASE`), cuenta los tiles que faltan en pantalla y lo resume por escenario (`tabla.py`).
  - Referencia de la placa: `psxprof` con `DUMPAT` guarda, por cuadro, la imagen que enseña la PS1 y el estado de la placa con el que se dibujó (y `INPUTS` lleva el mando: elegir rally, conducir).
    - `arcade.py` dibuja ese estado como MAME (y como `ps1/main.c`).
    - `difref.py` compara las dos imágenes y atribuye cada diferencia a sprites y tiles (falta, sobra, prioridad, color).
  - `psxprof`, modelos del hardware real y de los ODE (todos apagados por defecto; sin ellos sale igual que antes): `CD_STAT` (stat con busca/lee/toca), `CD_PAUSA_REAL` (Pause rechazada mientras busca), `CD_SEEK_MUL`/`CD_SEEK_JIT`/`CD_SPEED_MS`, `CD_INT3_RND` y `CD_BUSY` (tiempos de respuesta de psx-spx), `CD_GLITCH` (HINTSTS inestable), `CD_ERR`/`CD_ERR80` (errores de lectura, «aún no lista»), `CD_AP` (dónde da la pausa automática), `CD_COLA` (cola de escritura de la CPU), `T1_GLITCH` (temporizador leído a medias), `LINE_CYC` (consola PAL), `CD_INIT_MS`, `LOADER_IRQ`/`LOADER_SP` (arranque desde un cargador con interrupciones activas), y los conjuntos `ODE=xstation` y `ODE=psio`. `POKE68=dir:valor[:desde]` fija una palabra del 68000 cada cuadro (p. ej. `POKE68=FEC1AA:0:1900,FEFF08:0:1900` con `STEER=1` para el reloj y llega a la meta y a 'Your Time').
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
| L2 | cifras de rendimiento; abajo, las del CD: sectores perdidos, errores, vigilancias, último código de error, Pause rechazadas y cuadros por segundo ×1000 |

### JogCon (Namco)

El JogCon de Ridge Racer Type 4 sirve como volante (`ps1/mando.c`, comun con el WR2).

- **Conectarlo:** en el puerto 1, con la luz de MODE encendida. Si se apaga (modo digital), funciona como un pad normal.
- **El centro:** es donde este la rueda cuando el juego lo ve, y el motor la devuelve ahi como un muelle.
- **Como gira el coche:** la placa del WR1 solo lee bien el joystick. Con su volante optico o de potenciometro el juego
  no gira, tampoco en MAME. Por eso el giro de la rueda se pasa a pulsos de izquierda/derecha, tantos como lo girado:
  un cuarto de giro, un cuadro de cada cuatro; todo el giro, siempre.
- **El giro completo:** la marca de la rueda arriba (las 12) es recto. Por defecto, todo el giro es a las 3 (derecha) o
  a las 9 (izquierda), es decir, un cuarto de vuelta. Como no se sabe cuantas unidades da el JogCon en ese cuarto de
  vuelta, se mide:
  1. Manten L1+R1.
  2. Gira la rueda hasta las 3 (o hasta las 9).
  3. Suelta.

  Abajo sale el numero mientras giras y despues el medido. Si nos lo dices, lo ponemos por defecto (`VUELTA_DEF` en
  `ps1/mando.c`).
- **Botones:** los mismos que el pad.

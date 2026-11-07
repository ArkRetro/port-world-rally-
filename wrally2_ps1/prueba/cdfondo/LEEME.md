# Sonda `cdfondo`: lectura del CD a 2x de fondo con la CPU ocupada (diseno.md 6.2, hito H2)

Mide en el hardware (MiSTer, PS1 con PSIO o XStation) si la lectura de datos a doble velocidad de fondo aguanta
mientras la CPU esta ocupada como lo estara con el 68000 recompilado, con los puntos de sondeo del diseno, y con las
dos variantes del driver del WR1 (`wrally_ps1/ps1/cd.c` por ruta, sin cambios): copia del FIFO por CPU y `-DCD_DMA`.
Las dos van en el mismo EXE (cd.c compilado dos veces; a la de DMA se le ponen los simbolos `d_*` con objcopy).

Carga de CPU por cuadro (60 Hz): bloque "68000" del 35 % que sondea `cd_poll` cada ~0,35 ms (16384 ciclos del
68000), bloque "dibujo" del 40 % con sondeos entre fases (~1,5 ms) y uno al final, y espera del vblank sondeando en
bucle como el WR1 (sin sondear en las 32 lineas antes del vblank previsto). El trabajo es real: lecturas seguidas y
al azar y escrituras a la RAM.

Datos: `gen.py` escribe `cd/DATOS.BIN` (nada de la ROM; `SECTORES`, 6000 por defecto = 12 MB, 40 s a 2x). Cada sector
lleva su indice y un xorshift32 sembrado con el. Se lee seguido con `cd_stream_async` durante `DURACION` s (60) por
variante; al acabar el fichero se espera a que la unidad pare (10 cuadros, como entre dos cargas del port) y se vuelve
al principio con una busqueda.

Pantalla (368x240), una columna por variante:
- SEGUNDOS, SECTORES OK, PERDIDOS (indice mayor del esperado), REPETIDOS (menor), MALOS (indice bien, flujo mal).
- DRV SECTORES / DRV PERDIDOS / DRV ERR VIG COD: `cd_dbg` del driver (sectores vistos, perdidos = `CD_SECTOR_LOST`,
  errores INT5, vigilancias, ultimo codigo de error en hex).
- HUECO MAX US: hueco maximo entre entradas a `cd_poll` con lectura activa, y donde se dio (6 = bloque 68000,
  D = dibujo, V = espera del vblank); HUECOS 4 MS: cuantos pasaron de 4 ms.
- CICLOS SECTOR / MAX / VACIO: ciclos de CPU dentro de `cd_poll` por sector llegado (media y maximo) y por sondeo vacio.
- KB/S (a 2x, ~300; en rojo por debajo de 250), VUELTAS (vueltas al principio), TARDE (cuadros que se pasaron del vblank).
- Abajo, por variante: OK si SECTORES OK > 0, PERDIDOS = REPETIDOS = MALOS = DRV PERDIDOS = 0 y HUECO MAX <= 4000 us
  (la puerta de 6.2); FALLO si no. Los errores del driver no cuentan para la puerta (salen en rojo). Primero corre la
  variante CPU, luego la DMA; al acabar las dos queda el resumen fijo. Si la DMA se cuelga en un aparato, la pantalla
  se queda con las cifras de la CPU ya decididas.

Construir: `. env` con NUGGET, MKPSXISO y LICENCIA; `./build.sh` (`DURACION=s`, `SECTORES=n`, `SD_NOMBRE`). Salen
`cdfondo_USA/EUR.bin/.cue` (~16 MB) y `sd/` (carpetas para PSIO y XStation).

psxprof: `cd wrally_ps1/perf && ./psxprof .../cdfondo_USA.cue .../cdfondo.ps-exe .../cdfondo.nm 8200 salida` (138 s;
con `ODE=psio`, `ODE=xstation`, `CD_ERR=500` para ver que cuenta errores). psxprof no copia los datos de la DMA 3 (su
`dma_start` solo tiene los canales 2, 4 y 6), asi que alli la variante DMA sale con todo PERDIDOS/REPETIDOS y FALLO
por diseno; la CPU da OK. La DMA se comprueba en Mednafen con BIOS (`juega.sh cdfondo_USA.cue salida.mov 165 ""`) y
en el hardware.

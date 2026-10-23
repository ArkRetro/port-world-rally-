# World Rally 1 y 2 en un solo disco (PS1)

Un CD con los dos ports y un menú para elegir cuál cargar, al estilo del World Rally. Tiene:

- de fondo, una bandera de cuadros que ondea (como el marco del título del WR1);
- **ELIGE JUEGO** en la letra cursiva de la pantalla de CARGANDO;
- las dos portadas (el título de cada juego) con su placa de rally, roja la del WR1 y azul la del WR2;
- **PULSA START** parpadeando abajo.

Mandos del menú:

- **Izquierda / derecha** (o L1 / R1): eligen. La portada elegida lleva un marco amarillo que late y la otra se oscurece.
- **START, X o círculo**: cargan el juego. La pantalla se funde a negro, sale CARGANDO y la BIOS carga el EXE del juego.
- Para volver al menú, se reinicia la consola.

Uso personal. Las portadas salen de las ROM y las imágenes llevan el logo de tu BIOS: no van en el repositorio.

## Cómo va

- `INICIO.EXE` (`menu.c`, `inicio.s`) es el que arranca SYSTEM.CNF.
  - Al empezar guarda el estado de interrupciones y DMA que deja la BIOS. Luego trabaja sin interrupciones: el vsync y el mando van por sondeo, como en los ports.
  - Para lanzar un juego devuelve ese estado tal cual y llama a `LoadExec` de la BIOS. Así cada juego arranca igual que si lo hubiera lanzado la BIOS desde su propio disco.
- **Disco:**
  - En la raíz van SYSTEM.CNF, INICIO.EXE y los ficheros del WR1 con su EXE.
  - En la carpeta `WR2` van los del WR2. Su `cd.c` busca allí si la carpeta existe; si no, en la raíz, como en su disco suelto. Hace falta porque `TILES.BIN` se llama igual en los dos y porque la BIOS solo lee el primer sector de cada directorio: los EXE tienen que caber ahí, y `mkiso.py` lo comprueba.
  - Detrás de los datos van las pistas de CD-DA del WR1. Su tabla `TRACKS.BIN` se rehace para este disco, porque las pistas empiezan en otro sitio.

## Construir

Primero hay que construir los dos ports (sus `ps1/cd/` y sus `iso_USA.xml`). Después:

```
NUGGET=... MKPSXISO=... LICENCIA=tu_bios.bin ./construir.sh
```

Los pasos que hace:

1. `captura.sh`: saca las portadas del título de cada port con `psxprof` (WR1) y `psxprof2` (WR2) y las deja en `work/portada1.ppm` y `work/portada2.ppm`.
   - Solo se ejecuta si esas portadas no están. Sirven otras capturas de 368 x 240 en ppm o png.
   - Con `SIN_PORTADAS=1` el menú pone un 1 y un 2 en su lugar.
2. `pantalla.py`: dibuja la pantalla en `work/pantalla.bin`, con una vista previa en `work/vista.png`.
3. Compila `cd/INICIO.EXE`.
4. `mkiso.py`: crea `wr12ps1_USA.bin/.cue` y `wr12ps1_EUR.bin/.cue`.
   - Además crea las variantes `_xs`, con el EXE del WR2 para XStation, y `_psio`, con el EXE del WR1 para PSIO.
   - Y las carpetas de `sd/` para la tarjeta: `(USA)` (CD grabado, MiSTer: .bin y .cue), `(USA) XStation` (.bin y .cue)
     y `(USA) PSIO` (.bin y .cu2).
   - Variables: `REGIONES='USA EUR'`, `XSTATION=0` y `PSIO=0` (sin esas variantes) y `SD_NOMBRE` (por defecto
     `World Rally 1 y 2 PS1`).

## Variante XStation

El lector de la XStation es una tarjeta SD: no hay sectores mal leídos. En el WR2 sobran entonces dos cosas que están
para los CD grabados:

- **El EDC de cada sector.** Es CPU en cada sector que llega, también los que se leen de fondo en carrera.
- **Los 3 s mínimos de CARGANDO antes de cada tramo.** En XStation la pantalla acaba en cuanto no queda nada que traer.

El EXE del WR2 lleva la marca `WR2PS1-ODE:0` (en `cd.c`) y `mkiso.py` la cambia a `1` en la imagen de XStation. El WR1
va igual en todas: su lector no tiene esos márgenes.

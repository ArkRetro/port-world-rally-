# World Rally 2: Twin Racing (Gaelco 1995) para PlayStation

Port en curso del arcade World Rally 2 a la PS1 (MiSTer, PS1 real con PSIO o XStation), con el mismo metodo que el
port del World Rally 1 de `../wrally_ps1/`: el codigo del 68000 recompilado a C, la MCU de proteccion traducida y el
video y el sonido hechos para la PS1. Configuracion de la placa: un monitor (DIP Monitors = One), 1 jugador, dificultad
Normal. **Uso personal**: en el repositorio no hay nada de la ROM; todo se genera desde tus ROM de MAME.

Diseno y plan: `doc/diseno.md` y `doc/hitos.md`.

## Estado

- H0-H1 (banco de pruebas en el PC): hecho en buena parte. El juego corre en el PC con el codigo recompilado; comparado
  con Musashi instruccion a instruccion (12 000 cuadros sin diferencias) y con MAME cuadro a cuadro (arranque, attract y
  una partida de Portugal de 10 730 cuadros con las mismas pantallas en los mismos cuadros y las mismas puntuaciones).
- H2 (primer EXE para la PS1): el juego entero corre en la PS1 con la ROM dentro del EXE y la imagen en mosaico (cada
  tile y cada sprite es un cuadrado de su color), con cifras de CPU y RAM. Arranca en Mednafen con BIOS desde la imagen de
  CD. Puertas: CPU del 68000 + MCU en carrera 26 % (mediana) / 34 % (p99); huella de estado igual al banco en 20 000
  cuadros de attract y una partida; sondas `prueba/cdfondo` (CD a 2x de fondo) y `prueba/spucad` (SPU) hechas.
  Falta la prueba en MiSTer, PSIO y XStation (prueba 0, entregada).
- H3 (en curso): attract con los graficos de verdad; ROM por bancos hecha en el banco.

## Grabar el CD para una PS1 de verdad

- Graba la imagen con el `.cue` (no el `.bin` suelto), en modo **DAO / disc-at-once** y a **poca velocidad** (4x-8x),
  en CD-R de buena calidad (los plateados/azules de antes leen mejor que los muy oscuros). La consola necesita el chip
  de modificacion o un metodo de arranque para discos grabados.
- La imagen del World Rally 2 solo tiene una pista de datos (el sonido son samples de la SPU que se leen de
  `SPU.BIN`, no pistas de audio). Cada sector se comprueba (EDC de Mode 2 Form 1) y el que llega mal se vuelve a leer;
  los de `SPU.BIN` ademas con su suma, y lo escrito en la RAM de la SPU se lee de vuelta para ver que llego.
- Con L2 + R2 salen las cifras; la tercera fila: sectores de `SPU.BIN` que llegaron mal, perdidos (sin leer bien en 4
  intentos), escrituras a la SPU que no salieron, errores del controlador del CD y errores de los paquetes. Si en la
  consola algo suena o se ve mal, esas cifras dicen si es el disco.

## Banco de pruebas

Requisitos: Python 3, gcc; opcional Musashi (https://github.com/kstenerud/Musashi) para la comparacion paso a paso.

```
WR2_ROM=/ruta/a/wrally2.zip ./banco.sh            # o la carpeta con las ROM de wrally2 (y wrally2a)
cd rc && ./rc_host 12000 ../work/run              # attract; variables en la cabecera de rc/rc_host.c
MUSASHI=/ruta/a/Musashi WR2_ROM=... ./banco.sh    # ademas rc/rc_host_dbg (comparacion con Musashi)
```

ROM: `wr2_64.ic64`, `wr2_63.ic63`, `wrally2_ds5002fp_sram.bin` y los graficos (`wr2_ic68/69/70` o las 14 EPROM de
`wrally2a`).

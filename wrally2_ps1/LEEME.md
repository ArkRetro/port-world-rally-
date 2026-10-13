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
  CD. Falta: las sondas de CD y de la SPU, medir la CPU en carrera y la prueba en MiSTer, PSIO y XStation (prueba 0).

## Banco de pruebas

Requisitos: Python 3, gcc; opcional Musashi (https://github.com/kstenerud/Musashi) para la comparacion paso a paso.

```
WR2_ROM=/ruta/a/wrally2.zip ./banco.sh            # o la carpeta con las ROM de wrally2 (y wrally2a)
cd rc && ./rc_host 12000 ../work/run              # attract; variables en la cabecera de rc/rc_host.c
MUSASHI=/ruta/a/Musashi WR2_ROM=... ./banco.sh    # ademas rc/rc_host_dbg (comparacion con Musashi)
```

ROM: `wr2_64.ic64`, `wr2_63.ic63`, `wrally2_ds5002fp_sram.bin` y los graficos (`wr2_ic68/69/70` o las 14 EPROM de
`wrally2a`).

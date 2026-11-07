# Sonda `spucad`: encadenado de trozos en la SPU (diseno.md 5.2, hito H2)

Mide en el hardware (MiSTer, PS1 con PSIO o XStation) si encadenar trozos de sonido por la direccion de repeticion de
la SPU, con las reglas del juego, hace sonar cada trozo una vez y en orden. Nada de la ROM: `gen.py` sintetiza 12
tonos de una escala pentatonica (0,2 a 3 s, duraciones todas distintas) y los codifica a PS-ADPCM como lo hara `sndwr2.py`:
filtro 0 en el primer bloque, banderas 3 (fin + repetir) en el ultimo, sin inicio de bucle.

Reglas que aplica (5.2): KON con la direccion del trozo y, 2 lineas despues, repeticion = bloque de silencio
(0x1000, se repite a si mismo); el siguiente se encola (repeticion = trozo nuevo) solo cuando la voz ya toca el
anterior, con una guarda de 1 cuadro, y >= 111 ms antes de su final; parar = volumen 0; tono 0x2DF. Voces 0, 4, 5 y 6:
la 0 trozos cortos encolados justo en la guarda, con paradas y KON nuevos; la 4 medios encolados justo a 111 ms del
final; la 5 largos; la 6 cualquiera, al azar, con KON en medio de un trozo (efecto que corta a otro).

Deteccion: el reloj es el temporizador 2 (sysclk/8 = 96 cuentas por muestra de la SPU, el mismo cristal). Cada paso
al bloque de repeticion pone ENDX; como los trozos duran distinto (>= 50 ms entre dos), el tiempo entre dos ENDX dice
que trozo acaba de sonar: el esperado (VISTO), el anterior (REPETIDO), otro (SALTADO), el silencio o nada (OTROS).
ENDX se borra escribiendo el registro en Mednafen y DuckStation pero no en el nucleo de MiSTer (solo KON lo borra): la
sonda lo mide (ENDX BORRABLE) y, si no se borra, solo ve el primer paso de cada KON y el resto va por calendario, como
el juego. Por eso hay un segundo detector: el IRQ de la SPU vigila una voz cada vez con la direccion del trozo
encolado (IRQ OK si la voz entra en el a la hora prevista, IRQ FALLO si no). Se comprueba ademas que la direccion de
repeticion leida es la escrita (REPDIF) y que el ADSR no se apaga (ADSR0).

Pantalla: T segundos; ESPERADOS (trozos arrancados: KON + encolados) y VISTOS (pasos detectados a tiempo);
SALTADOS, REPETIDOS, TARDE (encolados despues del fin del anterior), PERDIDOS (sin ENDX 3 cuadros despues del fin
previsto), OTROS; IRQ OK / FALLO / CANC (vigilancia cancelada por un KON); SIL BUCLES (vueltas del silencio vistas);
DESVIO (max. entre ENDX y calendario, ms); IRQ DESV (max. entre el IRQ y el calendario) y abajo IRQMAX, el peor
disparo: voz (indice 0-3), trozo, A1 adelantado / A0 atrasado, ms desde que se armo, trozo que sonaba; por voz, trozo,
pasos, ADSR y repeticion leidos, trozos que quedan en la cadena. A los DURACION s (240) deja de encolar, espera 4 s y queda fija: RESULTADO OK si todo lo de fallo es 0 y
VISTOS = ESPERADOS (cuando ENDX es borrable); FALLO si no.

Construir: `. env` con NUGGET, MKPSXISO y LICENCIA; `./build.sh` (`DURACION=s`, `ROMPE=1`: encola el siguiente sin
esperar a que la voz toque el anterior, deben salir SALTADOS e IRQ FALLO; `ROMPE=2`: no pone silencio al entrar y
encola tarde, deben salir REPETIDOS). Salen `spucad_USA/EUR.bin/.cue` y `sd/` (carpetas para PSIO y XStation).
Mednafen: `juega.sh spucad_USA.cue salida.mov 250 ""` con `SND=1` para oir la melodia. `psxprof` no modela la
repeticion ni el IRQ de la SPU y lee el ADSR como 0: alli sale FALLO por diseno (sirve para ver la pantalla).

Medido en Mednafen con BIOS (SCPH-101): 240 s con las reglas, RESULTADO OK (1398 trozos, 1398 pasos vistos, 0 saltados,
0 repetidos, 0 tarde, 0 perdidos; IRQ OK 300, FALLO 0; ENDX borrable; desvio ENDX-calendario <= 3 ms). `ROMPE=1`:
FALLO (saltados 48, perdidos 28, IRQ FALLO 23 en 40 s); `ROMPE=2`: FALLO (repetidos 92, tarde 158, IRQ FALLO 70).
En el hardware falta medir (MiSTer, PSIO, XStation): apuntar la pantalla final; si ENDX BORRABLE sale 0, VISTOS solo
cuenta el primer paso de cada KON y vale lo demas (IRQ, saltados/repetidos por duracion y el oido).

Hallazgo para el juego: la reverberacion, aunque apagada, lee su area de trabajo de la RAM de la SPU (Mednafen: desde la
base, 0x1F801DA2, recorre los 512 KB cada 12 s) y esas lecturas tambien disparan el IRQ de la SPU; la sonda pone la
base a 0xFFFE (los ultimos 16 bytes). Un trozo que toquen dos voces a la vez dispara el IRQ antes de tiempo: la sonda
reparte los trozos para que no coincidan.

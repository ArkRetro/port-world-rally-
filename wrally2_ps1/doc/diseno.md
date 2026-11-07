# World Rally 2 -> PS1: diseno final

(Copia del diseno hecho con el analisis de la placa. Las rutas `scratchpad/...` y `analisis/...` son de los ficheros de
trabajo de ese analisis, que no van al repositorio porque salen de las ROM.)

Base: el diseno ganador `d_carga` (cada tramo se carga entero en la pantalla de carga y la carrera no toca el CD), con
los injertos que propusieron los dos jueces desde `d_hibrido` y `d_fiel`, y corregidos todos los errores que encontraron.
Las cifras discutidas se han vuelto a medir para este documento (marca **[f]**; scripts y salidas en
`scratchpad/wr2/diseno/final_calc/`, anexo A). Fuentes: `analisis/{hw,video,sound,mcu,reuse,ref}.md`, los tres disenos,
los veredictos y el port del WR1 (`wrally_ps1/`, LEEME.md). 1 KB = 1 024 B en todo el documento.

Configuracion pedida: DIP Monitors = One (una pantalla de 384x240), 1 jugador, dificultad Normal, joystick. Destino: PS1
en MiSTer y PS1 real con PSIO o XStation. Uso personal: en el repositorio solo hay programas, direcciones, tamanos de
tablas y guiones de entradas; todo lo que sale de la ROM se genera con `construir.sh` desde las ROM del usuario.

Nombres de tramo (FEFD0A): PT1 = 4 Estoril, PT2n = 8 Povoa (noche), PT3 = 5 Viseu (nieve); SA1 = 2, SA2n = 9, SA3 = 7;
CO1 = 0xA, CO2n = 6, CO3 = 0; RAC1n = 0xB, RAC2 = 1, RAC3 = 3 (nieve). Las demos del attract son el tramo 1 de cada
rally.

---

## 0. Resumen

### 0.1 Decisiones

| # | Tema | Decision | Cifra clave |
|---|---|---|---|
| D1 | Carrera | **El CD no lee nada desde la cuenta atras hasta la meta o «Continue?»**. Todo lo que el tramo puede necesitar esta en VRAM + RAM antes de la salida | puerta G-CD = 0 sectores |
| D2 | Pantalla de carga | **El propio mapa del tramo del juego** (386 cuadros = 6,45 s a 59,83 Hz, musica desde la SPU, CD libre). Lectura de fondo a 2x; CARGANDO solo si falta algo al llegar al negro | PT1: 1,41-1,47 MB, 4,6-4,8 s |
| D3 | Codigo 68000 | Recompilar **el conjunto alcanzable con Monitors=One sin los modos 16-23** (13 852 instr.). Sin interprete 68000 | `rc_run` 361 548 B medido **[f]**; 411 652 B con la trampa de ROM **[f]** |
| D4 | ROM del 68000 | Fuera del EXE. Residente = solo lo que leen los menus + datos de la zona de codigo (bloques de 256 B, ~72 KB). Todo lo demas que lee un tramo va en **su banco**, dentro del area de tramo. **Trampa exacta**: un bloque que falta se lee del CD | residente 80 KB; banco PT1 <= 170 KB |
| D5 | Suma de la ROM del arranque | Exacta: al arrancar se lee `ROM.BIN` entera (907 264 B) y el bucle de 66 cuadros corre de verdad | 3,0 s a 2x |
| D6 | RAM | 1 024 288 B fijos + **area de tramo de 1 007 328 B (983,7 KB)** con asignador por fases (banco ROM + L2) | PT1: 902-966 KB (margen 8,3 / 1,8 %) |
| D7 | Tiles en carrera, demos y mapa | **Formato B**: 4 bpp + CLUT de 16 por hueco, rehecha desde la paleta viva. 4 352 huecos | registro de 144 B igual en CD, RAM y subida |
| D8 | Tiles de menus | **Formato A**: 8 bpp exacto, CLUT = paleta del juego (fundidos gratis). Cada pantalla cabe entera | 2 560 huecos; <= 1 114 tiles por pantalla |
| D9 | Politica de cache en carrera | Huecos fijos de partida + VRAM-sola (VO) del principio del tramo + 416 rotativos servidos desde la L2; **se echan antes los que tienen copia en la L2** que los VO muertos | sim. **[f]**: 0 atascos, 0 tiles perdidos |
| D10 | Tile que falta | Fondo: cuadrado plano del color medio (proxy); sprite: no se dibuja. Se cuenta; lectura de emergencia a `TILES.BIN` | puerta <= 0,1 % de cuadros |
| D11 | Sonido | GAE1 por registros (contadores en tiempo del 68000) -> SPU con trozos encadenados. **Toda la musica en la SPU, sin CD-DA**: disco de una sola pista de datos | fijo 378 320 B + variable <= 141 856 B (max. usado 139 056) |
| D12 | Ritmo | 1 cuadro del juego por vblank de la PS1 (59,83 Hz en NTSC). **Tono de la SPU escalado por fps/59,1** (0x2DF en vez de 0x2D6). Cambiado despues: el sonido a tiempo real con 0x2D6 y el juego a un cuadro por vblank con saltos progresivos (`RITMO=59100`: 59,1 con un tiron cada ~1,4 s; hitos H5) | sin escalar, deriva de 0,74 s por carrera de 60 s |
| D13 | MCU | Trabajo A traducido; **modelo de tiempo por acceso**: corre en la recogida sobre una copia con sellos; escrituras aplicadas en su sello; si el 68000 escribe una entrada que el trabajo lee mas tarde, **se repite el trabajo** | <= 10 conflictos / 3 500 trabajos (el de ahora: 962/2 960 en el attract **[f]**) |
| D14 | CD fuera de carrera | Lectura de fondo a 2x con sondeo en puntos fijos del codigo de la PS1 (no en ciclos del 68000); modo prudente 1x | hueco max. entre sondeos <= 4 ms |
| D15 | Repositorio | `wrally2_ps1/` aparte (el banco ya existe, commit be15d9e). Lo generico del WR1 se usa por ruta y no se edita; guardia en cada commit; `comun/` al final | WR1 identico byte a byte |
| D16 | Orden | Bajar primero el riesgo del arranque y de la CPU en el hardware (H2: EXE mosaico + dos EXE de prueba), luego RAM/ROM por bancos con el attract (H3), datos (H4), carreras (H5), sonido (H6), entrega (H7) | ver §12 |

### 0.2 Que cambia respecto a `d_carga` y por que

1. **Sin interprete 68000 ni «codigo tibio».** Compilar lo alcanzable con Monitors=One sin los modos de servicio cuesta
   361 548 B **[f]**; el tibio de `d_carga` (329 196 B) estaba medido con el mapa del WR1 y ahorraba solo 10-40 KB a
   cambio de un interprete nuevo y de paradas de CD a mitad de carrera. Las 9 343 direcciones ejecutadas en todas las
   trazas de MAME estan dentro del conjunto **[f]**.
2. **La trampa de ROM se paga en el presupuesto.** Una comprobacion de puntero nulo con salida fuera de linea en cada
   lectura general cuesta +50 104 B **[f]** sin perfiles (no «+4 instrucciones», ni +139 KB). Se presupuesta
   `rc_run` = 417 792 B (lo medido sin perfiles + ganchos); con perfiles se espera ~387 KB.
3. **ROM residente minima.** En vez de «menus + lo que leen >= 2 rallies» (229 KB), el residente es solo lo de menus
   (~72 KB) y cada tramo lleva en su banco todo lo que lee (PT1: 162 KB medidos). Para el tramo mas justo (PT1) es +52 KB
   netos de area.
4. **Rotativos 416 y politica protectora.** La simulacion **[f]** da con ROT 400 lo mismo que con 600 (0 atascos, mismas
   faltas) y ahorra 29 KB de L2. Al probar dejando fuera la unica linea completa de RAC1/RAC3 (no se habia hecho), la
   politica de `d_carga` perdia tiles VO (5 136 y 113 206 tile.cuadro); echando antes lo que tiene copia en la L2, 0.
5. **Toda la musica en la SPU** (de `d_hibrido`): el CD queda libre en todas las pantallas, el disco es una sola pista
   de datos y desaparecen el audio externo de PSIO y los fallos de CD-DA de XStation.
6. **Menus en 8 bpp exacto** (de `d_hibrido`); el mapa del tramo sigue en B porque sus 415 huecos pasan a rotativos.
7. **Suma de la ROM exacta** (de `d_fiel`), **tono escalado** (de `d_fiel`), **sondas de hardware en H2** (de `d_fiel`),
   **arranque por etapas R0/R1 y pruebas tempranas en el hardware** (de `d_hibrido`), **modelo de la MCU por acceso**
   (de los jueces).
8. Los errores de los jueces se corrigen todos; la lista esta en §14.

---

## 1. Medidas nuevas de este documento **[f]**

| Que | Resultado | Fichero |
|---|---|---|
| Conjunto Monitors=One sin modos 16-23 (servicio, test, fallo de tension) | 13 852 de 22 252 instr. (62,3 %); las 9 343 direcciones ejecutadas en `ref/trace/union_addrs.txt` + las 10 trazas de `d_carga_work/trace/` estan todas dentro | `reach15.py`, `rc15/work/code_addrs.txt` |
| `rc_run` de ese conjunto, `rc68.py` del WR2, `mipsel -Os` | **361 548 B** (0x5844C) | `rc15/rc/base/o.o` |
| Mismo + trampa de ROM en RD8/16/32 generales (puntero nulo -> `rc_romt`, fuera de linea con `RC_CALL`) | **411 652 B** (0x64804, +50 104) | `rc15/rc/v3/o.o` |
| Comprobacion de las cifras de los jueces | 381 048 / 431 940 / 445 520 / 520 372 B (Monitors=One / +trampa / sin tablas / variante de `d_hibrido`) | `juez/v2..v4`, `d_hibrido_calc` |
| Modelos de tiempo de la MCU (`juez/mcumod.py` sobre `hw/out/hs3_*.txt`) | atomico en recogida 961/2 960 (attract), 6/3 528 (carrera); dos fases 962; tiempo por acceso 8 y 6. El 68000 escribe FC46 a +400-856 us; la MCU lo lee a una mediana de +1 042 us | salida en §8.3 |
| Simulacion de la cache, ROT 200/300/400/600/1 000, PT1/PT2n/RAC1n/RAC3 | PT1: ROT 400 = ROT 600 en faltas y atascos; con 300 o menos se pierden tiles VO (368 tile.cuadro) | `sim_*_rot*.txt` |
| Misma simulacion dejando fuera la unica linea completa (`rac_s1`, `rac_s3`) | politica de `d_carga`: 12,2 % / 49,5 % de cuadros con faltas, sobre todo por VO soltados; politica protectora: 0 soltados, quedan 8,9 % / 38,9 % por cobertura (el final del tramo no lo vio ninguna otra linea) | `sim2.py`, `sim2_*_rot416.txt` |
| Politica protectora en PT1 (9 lineas) | faltas 0-3,73 % de cuadros, todas «fuera del paquete»; subidas 3,2-6,0 por cuadro de media, p99 34-41, max 55-177; 0 atascos | `sim2_4_rot416.txt` |
| Tono de la SPU | 4 096 x 7 812,5 x (59,83 / 59,1) / 44 100 = 734,6 -> **0x2DF** (0x2D6 en la placa) | |

---

## 2. Mapa de la RAM

El EXE se carga en 0x80010000; hay 2 031 616 B hasta el final de la RAM (la BIOS se queda los primeros 64 KB). `crt0.s`
del WR1 pone la pila antes de borrar la BSS.

### 2.1 Tabla (bytes)

| Bloque | Bytes | Fuente |
|---|---|---|
| `crt0` + C del port (video A/B, cache, cargador, CD, SPU/GAE1, mando, cifras, trampa ROM) + libgcc | 81 920 | WR1: 52 308 sin libgcc; est. |
| **`rc_run`**: 68000 recompilado (Monitors=One, modos 1-15, trampa de ROM, ganchos MCU/GAE1, `RC_PLAZO`) | **417 792** | 411 652 medido sin perfiles **[f]** + 6 KB de ganchos; con perfiles se espera ~387 KB |
| `ht` (`RC_HN` 2048 x 8 B; 1 406 entradas de despacho) | 16 384 | **[f]** |
| MCU: trabajo A traducido con cuenta de ciclos y registro de accesos | 14 336 | 12,2 KB a -O2 (`mcu.md` §7) + ~15 % |
| MCU: tablas MOVC 0x1025-0x40E3 y 0x5488-0x54CD | 12 552 | `mcu.md` §2.1 |
| MCU: copia de la ventana FC00-FDFF, sellos de lectura, cola de escrituras | 4 096 | |
| **ROM residente** (bloques de 256 B leidos en menus + vectores y tablas de la zona de codigo) | **81 920** | 61,5 + 10,4 KB medidos (`d_carga_work`) + 11 % |
| `rc_romtab[4096]`: puntero sesgado por bloque de 256 B (o NULL) | 16 384 | |
| RAM del 68000 0xFE0000-0xFEFFFF | 65 540 | |
| VRAM del 68000 0x200000-0x20FFFF (registros GAE1 en 0x2890-0x28FF) | 65 540 | |
| Paleta 0x210000-0x211FFF | 8 196 | |
| Paginas de vregs (0x218), E/S (0x300) y lectura sin mapear | 12 300 | `rc_mem.c` |
| `rc_rpg[4096]` + `rc_wpg[4096]` (paginas de 4 KB; las de la ROM a NULL) | 32 768 | |
| Lista de la GPU (un bufer) | 49 152 | peor cuadro 42 KB (`video.md` §9) |
| `tslot`: raiz de 128 + 67 hojas de 256 codigos (u16: hueco o indice de L2) | 34 816 | el constructor comprueba <= 67 grupos por fase |
| Datos por hueco (4 352 x 8: codigo, muerte, paleta/version, clase) | 34 816 | |
| Copia de sprites del vblank (entradas activas de los dos bancos + celdas) | 8 192 | <= 150 entradas, <= 424 celdas |
| Sombra de paletas, CLUT de trabajo, lote de CLUT | 12 288 | |
| CD: estado de `cd.c`, 2 sectores de detras, sector suelto, 4 de emergencia, directorio | 20 480 | los paquetes van directos a su destino |
| Proxies: pen medio por grupo de 8 codigos | 4 096 | |
| Sonido: modelo GAE1, calendario de voces, `snd_tab` | 6 144 | |
| Pila | 16 384 | WR1 deja 2,9 KB |
| Varios: cifras, mando, cargador, indices de paquete, tablas 5->8 bpp | 8 192 | |
| **Total fijo** | **1 024 288** (1 000,3 KB) | |
| **Area de tramo** | **1 007 328** (983,7 KB) | 2 031 616 - fijo |

Fuera de esta tabla quedan, por decision: interprete 68000 (-24 KB), tablas de pagina por comparaciones (`d_carga` R2:
cuesta +13,6 KB de codigo y hace lentas las escrituras a VRAM/paleta), tabla de proxies por codigo (32 KB).

### 2.2 El area de tramo por fases

Un solo bloque con asignador de pila (bajo: banco de ROM; encima: L2 ordenada por primer avance; arriba: cargas de paso).

| Fase | Contenido | Bytes (peor caso) |
|---|---|---|
| Arranque, hasta el fin de la suma (suceso en PC 0x2616) | ROM no residente (0x000000-0x0DD7FF menos lo residente) | <= 835 000 |
| Attract: titulo -> demo | paquete de la demo siguiente: banco ROM del tramo 1 + lo que no cabe en los bloques libres de VRAM | PT1: 928 208 |
| Attract: demo en marcha | L2 de la demo (6 261 - 4 352 tiles) + banco | PT1: 275 KB + 170 KB |
| Menus de partida (coche, rally) | cargas de paso (tiles A de la pantalla siguiente) | <= 200 KB |
| Mapa del tramo | se llena con el paquete del tramo (banco + L2) | ver §2.3 |
| Carrera | banco + L2; tras «Continue?» se conserva (el tramo se repite) | ver §2.3 |
| Tras la meta (FEC146 = 0x21), resultados, tablas, fin de partida, records | la L2 ya no vale: cargas de paso de la pantalla siguiente sobre ella | <= 240 KB |

### 2.3 Necesidad por tramo

- Area = L2 + banco. L2 = (N - 3 936) x 144 B, con N = tiles del paquete del tramo (incluidos los ~1 000 fijos de
  partida) y 3 936 = 4 352 huecos - 416 rotativos.
- Banco = todo lo que lee el tramo en carrera fuera del residente, + relleno de +-1 bloque. Lecturas medidas por tramo
  (`d_fiel_calc/rom_bank.txt`): PT1 162, PT2n 136, PT3 110, RAC1n 113, RAC2 114, RAC3 136 KB; SA y CO medidos 20 s
  (80-116 KB), extrapolados x1,6.
- N: medido (union de lineas, `d_carga_work`) y estimado con todas las lineas + cierre (`d_carga` §3.3).

| Tramo | N medido | N est. | L2 (est.) | Banco | **Necesita (est.)** | Margen sobre 983,7 KB |
|---|---|---|---|---|---|---|
| **PT1** | 9 142 | 9 600 | 796 KB | 170 KB | **966 KB** (902 con N medido) | **+1,8 %** (+8,3 % con N medido) |
| CO3 | 5 065 (20 s) | 8 800 | 684 KB | 168 KB | 852 KB | +13,4 % |
| RAC3 | 8 009 | 8 500 | 642 KB | 144 KB | 786 KB | +20,1 % |
| CO2n / CO1 | 5 228 / 4 463 (20 s) | 8 000 / 7 800 | 572 / 543 KB | 154 / 178 KB | 725 / 721 KB | +26 % |
| PT3 | 6 856 | 8 200 | 600 KB | 118 KB | 718 KB | +27,0 % |
| RAC2 / RAC1n | 6 186 / 7 019 | 7 400 / 7 300 | 487 / 473 KB | 156 / 121 KB | 643 / 594 KB | +35 / +40 % |
| SA3 / SA2n / SA1 | 3 993 / 4 469 / 3 549 (20 s) | 7 000 / 6 800 / 6 200 | 431 / 403 / 318 KB | 136 / 160 / 194 KB | 567 / 563 / 512 KB | >= +42 % |
| PT2n | 6 494 | 6 800 | 403 KB | 144 KB | 547 KB | +44,4 % |

**Cabe todo.** El unico justo es PT1: con los 9 142 tiles medidos sobra un 8,3 %; si su paquete llega a 9 600, queda
un 1,8 %, por debajo del 2 % que pide la puerta G-RAM (§11: area >= max(L2 + banco) x 1,02 con los N y el codigo
finales), y entonces se aplica P1 (perfiles, que ya estan en el plan y se esperan en ~30 KB).

### 2.4 Palancas si un tramo no cabe (en este orden)

| # | Palanca | Gana | Coste / riesgo | Cuando se decide |
|---|---|---|---|---|
| P1 | Perfiles `regiones`/`vueltas` del banco en `rc68.py` (lecturas con region prevista, menos sitios generales con trampa) | ~30 KB de codigo | ninguno (el WR1 ya los usa) | H2-H4, medido |
| P2 | Sitios de memoria nunca ejecutados en ninguna traza -> acceso fuera de linea (sin camino rapido) | 15-30 KB (est.) | nulo en tiempo (codigo frio) | H4, medido |
| P3 | ROT 416 -> 300 en ese tramo, con el margen de muerte por tile del constructor | 17 KB | la sim con 300 pierde 368 tile.cuadro en PT1 si no se ajusta | H4 (sim) |
| P4 | Overlay de codigo solo de menus (fuera del cierre estatico de carrera) en lo alto del area mientras no hay carrera; se relee en el negro mapa -> carrera si la L2 lo pisa (+0,5 s) | ~150 KB | `rc68.py` en dos funciones; entrada a overlay ausente = parada | solo si P1-P3 no bastan |
| P5 | Leer en carrera solo la cola de la L2 de ese tramo a 1x, >= 20 s por delante del avance (`d_carga` §3.6) | lo que haga falta | rompe D1 en ese tramo (lectura de ~4-6 KB/s en una rafaga) | ultimo recurso |

Descartadas: compresion de la L2 (`d_carga` R4): ganaria 12-20 % pero inflar cuesta ~4 K ciclos por tile; en el p99 de
subidas (34-41 por cuadro) son 136-164 K ciclos (24-29 % del cuadro). Interprete 68000: solo si apareciera codigo
fuera del conjunto alcanzable (hoy, 0 de 9 343).

### 2.5 Configuracion R0 (solo para H2)

Para arrancar en el hardware sin bancos de ROM: la ROM 0x000000-0x0DD7FF va contigua en el EXE (907 268 B, como el
WR1 con `rom_sw.bin`), `RG_rom(a)` = `(a >> 20) == 0` como en el WR1 (mismas instrucciones; 0xDD800-0xFFFFF no se lee
nunca, lo comprueba `-DROMCHK` en el banco), sin `rc_romtab` ni trampa (`rc_run` 361 548 -> presupuesto 393 216). Pool
libre: **222 940 B**. Ademas, en H2 se enlaza un EXE con los tamanos de R1 (§2.1) solo para medir el area (puerta).

---

## 3. La ROM del 68000

### 3.1 Formato en la PS1

- `rc_romres`: los bloques residentes copiados en orden de direccion; cada tramo contiguo de la ROM se copia contiguo y
  con 4 bytes de cola (los de su vecino), asi que todos sus bloques comparten un puntero sesgado.
- Banco del tramo: igual, dentro del area de tramo.
- `rc_romtab[a >> 8]` = puntero sesgado tal que `rc_romtab[a >> 8] + a` es la direccion en la RAM, o NULL.

### 3.2 Caminos de `rc_rt.h` / `rc68.py`

| Acceso | WR1 | WR2 |
|---|---|---|
| Lectura con region `rom` prevista (`RDP*(rom, a)`) | `t6 = rc_rom`; srl + bnez + addu + lhu | `t6 = rc_romtab`: `b = ROMTAB(a); b ? MW(b, a) : rc_romt(a, n)` (+4 instr.: srl 8, sll 2, addu, lw, beqz; `rc_romt` fuera de linea con `RC_CALL`). ~1 000 lecturas de ROM por cuadro de carrera -> ~4 K ciclos |
| Lectura a direccion constante `MW(rom, K)` | directa | `rc68.py` conoce el residente (`romres.txt`): si K es residente, `MW(rc_romres, remap(K))` (igual que hoy); si no, por la tabla |
| Bucles rapidos `RBLK(rom, a, n)` | cabe en 1 MB | `ROMTAB(a) && ROMTAB(a) == ROMTAB(a + n - 1)`; si no, el bucle normal |
| Lectura general `RD8/16/32` | `rc_rpg[a >> 12]` sin comprobar | `p = rc_rpg[a >> 12]; if (!p) return rc_romt(a, n);` Las paginas de la ROM estan a NULL; `rc_romt` mira `rc_romtab`. **Medido: +50 104 B** **[f]** |
| `RDA32`/`RDP32` (cruce de bloque) | sin mirar | igual: la cola de 4 bytes lo garantiza |

`rc_romf(a, n)` (la trampa): si `rc_romtab[a >> 8]` es NULL lee el sector de `ROM.BIN` que lo contiene (parada de
0,1-0,4 s), lo deja en una de 4 entradas de emergencia de 2 KB (por turno), apunta los 8 bloques en la tabla y suma 1 a
la cifra «ROM». El 68000 ve siempre el dato correcto. En el banco de pruebas (`rc_host -DROMCHK`) cada lectura se
compara con el conjunto de la fase y se informa del PC; puerta G-ROM = 0 en todos los escenarios.

### 3.3 Conjuntos (de `ROMSTAT` del banco; hasta H4, de las tomas de MAME de `d_carga_work`)

- **Residente** = bloques leidos en modos de menu (3, 6, 8-F, incluidos el mapa B y el resultado D) + bloques de la zona
  0x0-0x1B7FF leidos como datos (vectores, 0x4E0-0x2000, tablas del sonido 0x1ACD0-0x1B800, salto 0x1B5B0). Medido:
  61,5 + 10,4 KB.
- **Banco del tramo k** = los demas bloques leidos en las carreras de k (modo 1) y en su demo (modo 5) por todas las
  corridas, + 1 bloque a cada lado de cada tramo contiguo. Lo que se lee depende del avance y no de la linea (9 lineas
  de PT1 anaden 0-0,75 KB a las 2 completas).
- Nadie lee 0x0DD800-0x0FFFFF: no va al disco.
- `tools/rombank.py` escribe `ROMRES.BIN`, la seccion ROM de cada `STkk.PAK`/`DEMOr.PAK`, `romres.txt` (para
  `rc68.py`) y `ROM.BIN` (0x000000-0x0DD7FF, para el arranque y las trampas).

### 3.4 Suma de la ROM del arranque, exacta

El test suma 0x0-0xDD4FE (PC 0x260C, 453 247 vueltas de 32 ciclos = 14,5 M ciclos = 66 cuadros, con las IRQ activas).
Un gancho que devolviera la suma cambiaria la temporizacion del attract frente a MAME y lo que la IRQ guarda en la pila.
Por eso, antes de soltar el 68000 se lee `ROM.BIN` entera (907 264 B, 3,0 s a 2x): lo residente a su sitio, el resto al
area de tramo, y `rc_romtab` apunta todos los bloques. Un suceso en PC 0x2616 (fin del bucle) pone a NULL lo no
residente y libera el area. Sin ganchos; la pantalla dice «ROM Checksum 3EDB» como la placa.

---

## 4. VRAM

### 4.1 Mapa (1 024 x 512 halfwords)

```
 x:  0                       368 384      448      ...                                   1023
 y=0   [ FB0 368x240           ][C0][ b0 ][ b1 ][ b2 ][ b3 ][ b4 ][ b5 ][ b6 ][ b7 ][ b8 ][ b9 ]
 240   [ S0: paletas, CLUT de efecto, CLUT libres ]
 256   [ FB1 368x240           ][C0][ b10][ b11][ b12][ b13][ b14][ b15][ b16][ b17][ b18][ b19]
 496   [ S1: letra 4 bpp (x 0-63), CLUT libres    ]
```

- **Framebuffers** de 368x240 en (0,0) y (0,256), NTSC 60 Hz como el WR1. Se dibuja la imagen de 384 con desplazamiento
  -8 (GP0 E5): se ven las columnas 8-375 de la placa; el marcador va de x = 64 a 350.
- **C0** (x 368-383, y 0-511): 512 CLUT de 16.
- **S0** (y 240-255, x 0-367): filas 240-245 = las 64 paletas del juego de 32 entradas (11 por fila; CLUT de A y fuente
  de las CLUT de B); fila 246 = RESTA y SUMA de 16 (B) y de 32 (A); filas 247-255 = 207 CLUT libres.
- **S1** (y 496-511): x 0-63 letra de 4 bpp (64 glifos de 8x8) para CARGANDO y las cifras; x 64-367 = 304 CLUT libres.
- **Bloques** b0-b19 de 64x256 en x = 384 + 64 (b mod 10), y = 256 (b div 10). Un gestor les asigna uso por fase:

| Distribucion | Bloques | Huecos |
|---|---|---|
| CARRERA / DEMO / MAPA (B) | b0-b16 tiles (17 x 256 = **4 352** huecos), b17-b19 CLUT (3 072); con C0 y ~530 de S0/S1, **4 114 CLUT** | 4 096 con CLUT propia + 256 sin ella (b16): celdas de efecto con RESTA/SUMA y tiles de media paleta con la CLUT del juego |
| MENU (A) | pares (b0,b1)...(b18,b19) = 10 paginas de 8 bpp | **2 560** huecos A |
| ELEGIR RALLY y menus de partida | paginas A de la pantalla (<= 5 paginas = 10 bloques) + 4 bloques B con los fijos del coche (1 024 huecos, CLUT en C0 + S0/S1), que no se mueven en toda la partida | |
| TITULO | paginas A 0-5 (1 536 >= presentacion) + 4 bloques en B (prelectura de la demo) | |

- Hueco B s: bloque s >> 8, U = 16 (s & 15), V = 16 ((s >> 4) & 15); se sube a (X + 4 (s & 15), Y + 16 ((s >> 4) & 15)),
  4x16 halfwords (GP0 A0). Hueco A: 8x16 halfwords.
- La pantalla de carga no ocupa huecos: se dibuja en los dos FB con primitivas sin textura y la letra (la de WR1:
  bandas, CARGANDO, barra START-FINISH). El logo de arranque va directo al FB en 15 bits.
- Opcion H8: 384 de ancho (MiSTer/RGB): pierde C0 -> 3 894 huecos B (-458): solo fuera de carrera o en tramos con margen.

### 4.2 Formatos de tile

| | A (menus) | B (carrera, demo, mapa) |
|---|---|---|
| VRAM | 8 bpp, indice = pen 0-31; CLUT = paleta del juego | 4 bpp; CLUT del hueco `[0, c15(pal[c*32+pen1]) ... c15(pal[c*32+pen15])]` |
| Calidad | exacta | exacta si el tile tiene <= 15 pens (22 % del fondo, 39 % de sprites, ~30 % en PT1); resto PSNR mediana 39,4 / 38,2 dB |
| Fundido de paleta | gratis (se suben las paletas cambiadas) | se rehacen las CLUT de lo visible cuya paleta cambio (<= 650 x 16 entradas) |
| Registro en CD | 162 B: codigo (2) + 128 nibbles bajos + 32 del plano del bit 4; 12 por sector | **144 B**, 14 por sector |
| En RAM (L2) | no hay (cargas de paso en crudo) | el mismo registro de 144 B |

Registro B (igual en el disco, en la L2 y para subir): 0-127 indices de 4 bpp (pixel izquierdo en el nibble bajo);
128-129 codigo; 130-139 receta (15 pens x 5 bits); 140 paleta con la que mas se usa; 141 banderas (fondo/sprite, efecto,
media paleta baja/alta, variante); 142-143 ultimo avance FEFD08 + margen. Cuantizacion fuera de linea (`tools/tiles4.py`,
fusion voraz por pixeles con la paleta mas vista); los tiles con todos sus pens en 0-15 guardan indice = pen (sin
perdida; las celdas de efecto se dibujan con RESTA/SUMA directamente). Mismo tile con dos paletas en un cuadro (0,2
veces por cuadro): hueco variante (copia VRAM->VRAM, GP0 0x80) con otra CLUT; 64 variantes.

El mapa del tramo y el de la demo van en B a proposito: sus 415 huecos pasan a rotativos en el negro de antes de la
carrera. Pierde la exactitud en los tiles de mas de 15 colores del mapa (opcion H8: mapa en A y 0,3 s mas de negro).

### 4.3 Como se dibuja (en `irq_take`, antes de despachar la IRQ6 = el `frame_done` de MAME)

1. `GP0(02h)` con el color de la entrada 0 de la paleta.
2. Plano 0 de la VRAM viva (0xC000; vregs[0] = 0x0C90 fijo): 25 x 16 (+1) posiciones; X = (VRAM[0x2802/2] + 0x14) & 0x3FF,
   Y = (VRAM[0x2800/2] + 1) & 0x1FF. Sin espejo: rectangulo crudo 0x7D/0x65; con espejo: cuadrilatero con ventana de
   textura (`emit_tile` del WR1, `main.c:948-988`). A: agrupado por paleta; B: CLUT por primitiva.
3. Sprites de la **copia S(N-1)** hecha en el vblank anterior, con el banco del bit 4 de vregs[1] **actual**; entradas
   0-511 en orden (las de despues tapan); fuera las de bit 15 = 1 (pantalla derecha). numero = ((w0 & 0x1FF) << 10) +
   (celda & 0xFFF); color = ((w0 >> 9) & 0x7F) + (celda >> 12); x = ((X + ex 16) & 0x3FF) - 127; y = ((Y + ey 16) &
   0x1FF) - 16. Medido: con esta regla 16 de 16 objetos fijos al suelo van con el fondo; con la VRAM viva, ninguno.
4. Celdas de efecto ((color & 0x3F) == 0x3F) en su sitio del orden: pens 1-7 -> modo 2 (B-F) con RESTA (0x8000 | p
   0x0421); pens 8-15 -> modo 1 (B+F) con SUMA (8: 8 0x0421; 9-15: (16-p) 0x0421). Diferencia con la placa: 0,017 % de
   los pixeles (dos celdas pisadas).
5. Copia **S(N)**: entradas activas de los dos bancos y sus celdas (10-15 K ciclos).
6. Paletas: la IRQ escribe <= 56 entradas por cuadro; se convierten las cambiadas (negro -> 0x8000, pen 0 -> 0) y se
   sube la version. Tras subir CLUT o tiles, la lista empieza con una primitiva de otra CLUT (cache de CLUT de la GPU,
   `psxprof CLUTCACHE=1`).

---

## 5. SPU

### 5.1 Mapa (512 KB)

| Zona | Direccion | Contenido | Bytes |
|---|---|---|---|
| reservada | 0x00000-0x00FFF | captura | 4 096 |
| silencio | 0x01000-0x0100F | bloque de ceros en bucle (destino de repeticion por defecto) | 16 |
| **fija** | 0x01010-0x5D5DF | 33 efectos 193 856 + motor y derrapes comunes 90 784 + musica corta (mapa S77, S78, records S79/S7A, fin de partida S80) 93 680 | **378 320** |
| **variable** | 0x5D5E0-0x7FFFF | segun la fase (tabla siguiente) | **141 856** |

| Fase | Zona variable | Bytes |
|---|---|---|
| Arranque, logo, fotos, titulo | M: titulo S70+S71 (73 232) + S: derrapes del titulo S68-S6A, S6B, S6D (56 368) | 129 600 |
| Mapa de demo y demo k | M: musica de la demo 0xA2-0xA5 (73 152-73 216); S queda (la demo no usa el ch6) | <= 129 584 |
| Elegir coche | S7C (73 200) + S7E (36 640) | 109 840 |
| Elegir rally (coche ya elegido) y toda la partida | banco del coche N 83 184 / L 102 416 / R 101 792 + S7E | **<= 139 056** |
| Records -> attract | se vuelve a cargar el conjunto del titulo | 129 600 |

Toda la musica del juego cabe asi (la que `sound.md` mandaba a 7 pistas de CD-DA, 205 s, incluida). Los bucles largos
(S7C, S7E) no dependen de copias en el disco: el juego vuelve a encolar el trozo.

### 5.2 Encadenado y ritmo

- PS-ADPCM a la frecuencia original, un sample por trozo, al derecho (en la ROM estan al reves), banderas 3 en el ultimo
  bloque, filtro 0 en el primero, sin «inicio de bucle» (`snd/sndwr2.py` + copia de `psxadpcm.c`).
- KON con la direccion del trozo; despues, direccion de repeticion = silencio. Al encolar: repeticion = el trozo nuevo,
  **solo cuando la voz ya toca el anterior** (calendario propio por voz, guarda de 1 cuadro); el juego encola >= 111 ms
  antes del final (>= 4,06 s en musica). Parar: volumen 0. Voces: ch0 -> 0, ch4 -> 4, ch5 -> 5, ch6 -> 6 (<= 3 a la vez).
- Mono = max(bits 12-15, bits 8-11) del volumen (el juego escribe 0x0F80/0x0F81). Sin reverberacion.
- **Tono = 4 096 x 7 812,5 x (fps / 59,1) / 44 100**, con fps medido al arrancar (`medir_fps` del WR1): 0x2DF en NTSC
  (59,83 Hz), +0,2 semitonos. Sin escalar, el modelo del GAE1 (tiempo del 68000) va un 1,2 % por delante de la SPU y una
  cadena continua del ch6 se queda 0,74 s atras en 60 s, mas que el margen de 111 ms. Opcion `RITMO=591`: un cuadro
  repetido cada ~82 (59,1 Hz de media) y tono 0x2D6.
- Cargas bloqueantes: voces a tono 0 y modelo del GAE1 quieto (la pausa del WR1); al seguir, cada voz sigue donde
  estaba. Un hueco de la zona variable que se sustituye apaga antes sus voces; los trozos no cargados suenan como silencio
  (`fx_ready` del WR1).
- Peticiones de sonido de la MCU (FEFD02/FEFD04, ID 0x8E): las escribe el trabajo traducido; se comprueban con `SNDLOG`.

---

## 6. CD

### 6.1 Disco: una sola pista de datos, ficheros en orden de uso

| Fichero | Contenido | Tamano |
|---|---|---|
| `SYSTEM.CNF`, `WR2PS1.EXE` | primero del directorio (lo comprueba `mkiso.py`) | EXE ~560 KB (texto + datos) |
| `ROM.BIN` | 0x000000-0x0DD7FF (arranque y trampas) | 907 264 |
| `ROMRES.BIN` | residente (si el arranque no lee `ROM.BIN` entera, opcion) | ~80 KB |
| `SPUFIJO.BIN`, `SPUTIT.BIN`, `SPUDEMO0-3.BIN`, `SPUMENU.BIN`, `SPUCAR0-2.BIN`, `SPUS7E.BIN` | bancos y conjuntos de la SPU | 378 320 / 129 600 / ~73 KB x4 / 109 840 / <= 102 416 x3 / 36 640 |
| `MENUS.PAK` | tiles A por pantalla (test, logo, fotos, titulo, elegir coche/rally, resultado, tablas, nombre, records, fin) | ~1,5 MB |
| `GAMEN.PAK`, `GAMEL.PAK`, `GAMER.PAK` | fijos de partida por coche (poses, marcador, efectos comunes), B | ~144 KB c/u |
| `DEMO0-3.PAK` | mapa de la demo + tiles de la demo + banco ROM | <= 1,05 MB c/u |
| `ST00-11.PAK` | por tramo: mapa (B) + VO + L2 (orden por primer avance) + banco ROM, seguidos (una busqueda) | 0,9-1,47 MB c/u |
| `TILES.BIN` | los 32 768 tiles en B, por codigo (emergencias en carrera) | 4,5 MB |
| `TILESA.BIN` | idem en A (emergencias en menus) | 5,0 MB |
| `LOGO.BIN` | logo de arranque | ~150 KB |

Total ~32 MB. Sectores de paquete: cabecera de 32 B (`WR2T`, paquete, indice, n.o de registros, primer avance, seccion
VO/L2/ROM/MAPA) + 14 registros B (o 12 A). La seccion ROM son tramos (direccion, longitud) + datos.

### 6.2 Como se lee

- `ps1/cd.c` del WR1 por ruta, sin cambios (endurecido para MiSTer, PSIO y XStation; guarda 2 sectores de detras).
  `rd_dst(i)` deja cada sector en su destino: registros de L2 o banco directamente en el area; registros VO a VRAM desde
  el sector suelto.
- **Bloqueante**: en el primer cuadro negro de una transicion (FEC298 = 0) el 68000 y la MCU se congelan (no corre
  ningun contador); el estado del juego no cambia. Si dura > 0,5 s se pinta CARGANDO. Monedas pulsadas durante la carga
  se guardan y se entregan 2 cuadros despues.
- **De fondo**: con el juego en marcha, en pantallas sin necesidad del CD (todas: no hay CD-DA). Las paginas de un banco
  que se esta cargando estan a NULL hasta que acaba (el juego no las lee en esa fase: lo comprueba G-ROM).
- **Sondeo** (correccion de los tres disenos: los ciclos del 68000 no son tiempo real; el 68000 recompilado va 2-3
  veces mas rapido y el salto de la espera del cuadro lleva al vblank). Puntos de sondeo en el codigo de la PS1:
  (1) suceso del contador de ciclos cada 16 384 ciclos del 68000 mientras corre; (2) entre las fases de `rc_vblank`
  (plano, sprites, efectos, cada 8 subidas, paletas); (3) el bucle de tiempo libre `bg_service`; (4) la espera del vsync
  (como el WR1). `psxprof` mide el hueco maximo entre sondeos con lectura activa: puerta <= 4 ms (a 2x llega un sector
  cada 6,67 ms) y 0 `CD_SECTOR_LOST`.
- **Coste de CPU**: la copia por CPU del FIFO cuesta ~23 000 ciclos por sector (`cd.c:96-97`, medido en `psxprof`),
  ~13 000 con `-DCD_DMA` (sin probar en MiSTer: lo prueba la sonda de H2). A 2x: 10,2 % de la CPU; con las subidas de
  los VO (14 por sector, ~1 000 ciclos cada una), 16 %. Solo durante cargas de fondo (mapa, titulo, menus).
- **Modo prudente** `CARGA_1X`: todo a 1x con el sondeo del WR1 entre cuadros; lo que no llegue en la ventana se acaba
  en CARGANDO (§7.2).

---

## 7. Pantallas de carga

### 7.1 Cuando hay ventana (medido, `ref.md` §4)

Toda transicion: peticion (FEC29C) -> 60 cuadros de fundido de paleta -> negro -> 4 cuadros con FEC298 = 0 -> el modo
nuevo aparece de golpe 3 cuadros despues (11 en carreras). El mapa del tramo (modo B) dura 386 cuadros **incluido su
propio fundido de salida**: la carrera aparece en el cuadro 2 388 tras un mapa de 1 998-2 383. Ventana util de fondo =
**386 cuadros = 6,45 s** a 59,83 Hz (no 7,5 ni 7,8 s). El tramo se sabe en el primer negro antes del mapa (FEC3C6,
FEC3C8 y la tabla 0xD40C4).

### 7.2 Que se carga, donde y cuando (2x = 307 200 B/s, busqueda 0,1-0,4 s)

| Momento | Tipo | Que -> donde | Bytes | Tiempo 2x | Se ve |
|---|---|---|---|---|---|
| Arranque, antes de soltar el 68000 | bloqueante (logo + barra) | `ROM.BIN` -> residente + area; tiles A del test -> VRAM | 907 264 + ~16 000 | **3,0 s** | si |
| Negro 94 + test 367 cuadros (7,7 s, sin musica) | fondo | SPU fija + conjunto del titulo -> SPU; tiles A de logo/fotos/titulo (<= 1 500) -> VRAM | 378 320 + 129 600 + 240 000 | 2,4 s | no (sin fondo: +2,4 s de barra) |
| Titulo (971 cuadros, musica en la SPU) | fondo | paquete de la demo siguiente: ~1 000 VO -> bloques B libres; el resto + banco -> area | PT1: 1 075 664 | 3,5 s | no |
| Negro titulo -> mapa de demo | bloqueante | musica de la demo -> hueco M (cuando el ch5 del titulo acaba); area -> VRAM (subidas desde la RAM) | ~73 KB | **0,35 s** | negro un poco mas largo |
| Demo -> logo / records | bloqueante + fondo | tiles A del logo (o records, 448) en el negro; fotos y titulo de fondo durante el logo | 32-72 KB + 200 KB | **<= 0,3 s** | negro |
| Records (366 cuadros) -> logo | fondo | conjunto SPU del titulo y tiles del logo | 129 600 + 32 KB | 0,5 s | no |
| START -> «Choose A Car» | bloqueante | tiles A de elegir coche (825) -> VRAM; S7C -> SPU | 205 200 | **0,9 s** con busqueda | CARGANDO |
| Elegir coche (<= 659 cuadros) | fondo | tiles A de elegir rally (452) + S7E | 108 960 | 0,35 s | no |
| Elegir rally (<= 899 cuadros), coche ya elegido | fondo | fijos de partida `GAMEc.PAK` (~1 000 B) -> 4 bloques B; banco SPU del coche -> hueco de S7C (zona variable = coche <= 105 216 + S7E 36 640) | 246 416 | 0,8 s | no |
| Negro antes del mapa del tramo | bloqueante | tiles B del mapa (415) -> VRAM | 59 760 | **0,35 s** con busqueda | negro |
| **Mapa del tramo (6,45 s)** | **fondo** | `STkk.PAK`: VO (2 936) -> VRAM; L2 + banco ROM -> area | PT1: 1 412 480 (est.) / 1 346 528 (med.) | **4,6 / 4,4 s** | **no** |
| Negro mapa -> carrera | solo si falta algo | lo que falte; los 415 huecos del mapa pasan a rotativos y se llenan desde la L2 (~0,4 M ciclos) | | 0 s a 2x; **2,7-3,1 s** de CARGANDO con `CARGA_1X` | no / si |
| Carrera | ninguna | — | 0 | — | — |
| Meta (FEC146 = 0x21) -> resultado | fondo, tras la meta | tiles A del resultado (1 114) -> area, sobre la L2 ya muerta; en el negro, a VRAM | 178 240 | 0,6 s | no |
| «Continue?» (10 s) | fondo | tiles de fin de partida; si continua: mapa en el negro y VO (2 936) de fondo durante el mapa | 422 784 | 1,4 s | no |
| Resultado -> mapa siguiente | como «negro antes del mapa» + mapa | | | | |
| Tabla del rally (366) -> elegir rally | fondo | tiles A de elegir rally (S7E ya esta) | 72 320 | 0,25 s | no |
| Fin de partida -> nombre -> records | fondo en cada pantalla | tiles A de la siguiente (<= 600) | <= 96 000 | <= 0,35 s | no |

Visible: arranque 3,0 s (5,4 s sin carga de fondo), START 0,9 s, +0,35 s de negro antes de cada mapa, ~0,3 s en cada
transicion del attract (unos 2,6 s por ciclo de 159 s). Entre el mapa y la carrera, nada a 2x.

Con `CARGA_1X` (150 KB/s): el paquete de PT1 tarda 9,2-9,6 s contra 6,45 s de mapa: **2,7-3,1 s de CARGANDO** (no
0,7-1,5 s como decia `d_carga`). El primer tramo ya no anade nada: coche y SPU van en elegir rally.

---

## 8. MCU y tiempos

### 8.1 Cuadro, IRQ y esperas

- `CYC_FRAME` = 219 966 (13 MHz / 59,1 Hz); IRQ6 por presupuesto de ciclos (`irq_take`), reconocida en 0x218008.
- 0x338A-0x3396 (espera del cuadro, 54 ciclos por vuelta): `IDLECNT` suma las vueltas saltadas a 0xFE8118 (estado
  exacto; ya en el banco). 0x3362: `IDLE` normal. 0x3692/0x37D6 (`tst FC12/FC16; beq`) fuera del detector: no esperan
  nunca; si esperaran, avanzan la MCU.
- 0x36D4 `bne 0x380C` siempre tomado (pantalla 2 fija en su modo 2): `rc68.py` emite `if (!Z) rc_bad(0x36D8)`. Un salto
  fuera del conjunto acaba en `rc_bad` con el PC en pantalla.

### 8.2 Sucesos en el contador de ciclos (`rc_rt.h`)

`cyr` cuenta hasta `cyc_next` = min(vblank si imask < 6, primer suceso). `IRQCHK` mira el signo; en `irq_take`, si vencio
un suceso: `RC_SYNC; rc_sucesos(); RC_TLOAD;` y se sigue en `dpc` sin IRQ. `rc68.py` emite `RC_PLAZO()` (recalcula
`cyr` con el vblank pendiente) tras `move to SR`, `andi/eori SR` y `rte`. Los sellos van en ciclos absolutos y se les
resta `CYC_FRAME` en cada vblank. Sucesos: MCU (recogida, escrituras con sello), sondeo del CD (solo con carga activa),
fin de la suma de la ROM (PC 0x2616), eco del test de arranque (cada ~150 us durante el modo 2).

### 8.3 Trabajo de la MCU: tiempo propio por acceso

Medido **[f]** con `juez/mcumod.py` y `fc46.py` sobre `hw/out/hs3_*.txt`:

| Modelo | Carrera (3 528) | Demo del attract (2 960) |
|---|---|---|
| al momento (WR1) | 2 348 | 2 937 |
| entero en la recogida (+77 us) (`d_carga`) | 6 | **961** (955 en FC46) |
| copia en la recogida, escrituras al final (`d_hibrido`) | 8 | 964 |
| lecturas al empezar cada parte, escrituras con sello (`d_fiel`, `wrally2_ps1/rc/mcu.c` actual) | 6 | 962 |
| **cada acceso en su tiempo** (`hw/hs4.py`) | **6** | **8** |

La demo escribe la entrada grabada en FC46 a +400-856 us de la peticion y la MCU la lee a una mediana de +1 042 us
(dentro de 0x074D): ningun modelo atomico lo reproduce. Modelo del port:

1. Escritura de 1 en 0xFEFC14 (PC 0x3684): suceso P en la recogida (fase del bucle de la MCU como el `mcu.c` actual:
   PERIODO 1 808, DESFASE 101, AVISO 104 ciclos; 99,7 % de aciertos; si no hay fase, 1 006).
2. En P: FC15 = 0, FC13 = 0; copia de FC00-FDFF; el trabajo A traducido corre entero sobre la copia y apunta cada
   lectura (direccion, sello P + 12 x ciclos maquina) y cada escritura (direccion, valor, sello). `rc51c.py` cuenta
   ciclos por bloque.
3. Las escrituras se aplican a la RAM en su sello (sucesos agrupados de 64 en 64 ciclos); FC13 = 1 con el sello final.
4. **Repeticion**: las instrucciones del 68000 que escriben entradas del trabajo mientras corre (lista por PC que saca el
   banco con `-DMCUCHK`; p. ej. 0x105B8 para FC46) llevan un gancho de `rc68.py`. Si el trabajo leyo esa direccion con
   un sello posterior a ahora, se repite desde la copia con el valor nuevo (~2 K ciclos). Las escrituras ya aplicadas no
   cambian (son anteriores a esa lectura). El trabajo es una funcion pura de la RAM compartida (`mcu.md`: probado con la
   RAM interna al azar).
5. `-DMCU_AL_MOMENTO`: el modelo del WR1, para Musashi paso a paso (los dos lados igual).

Coste: trabajo 1 955 ciclos de media (5 414 max), una repeticion en ~1/3 de los cuadros de la demo, sucesos ~1 K: <= 2 %
del cuadro. HLE del resto (`mcu_frame`): FC0A = ~FC08, FC0E = ~FC0C, FDD8/FDD9 = 0x0E/0x06, FC01 = 0, FDDA-FDEB = 0;
fuera serie, grabador, FC10 y fallo de tension. El trabajo B no se compila (su peticion, 0x37C8, esta fuera del
conjunto).

### 8.4 Determinismo

Los sucesos van en ciclos del modelo de `rc68.py`, iguales en el banco y en la PS1, y las cargas congelan el 68000: el
estado emulado por cuadro de la PS1 es el del banco (puerta G-ESTADO). Por eso los paquetes del banco valen para la PS1
y el primer ciclo del attract es identico.

---

## 9. Comportamiento en carrera

### 9.1 Al empezar

- VRAM (4 352 huecos B): ~1 000 fijos de partida (coche elegido: 1 042 tiles propios del coche N; marcador, cifras,
  efectos comunes) + **VO** del tramo (2 936: fondo y sprites del principio por primer avance, sin copia en la RAM) +
  **416 rotativos** (los del mapa, llenados desde la L2 en el negro).
- RAM: L2 del tramo (registros de 144 B ordenados por primer avance, con indice disperso cada 64) + banco de ROM.
- CD en Pause desde la cuenta atras (FEC146 2-6) hasta la meta (0x21) o «Continue?» (0xD).

### 9.2 Cada cuadro (`rc_vblank`)

1. Dibujo (§4.3). Para cada codigo visible: `tslot` -> hueco (sigue) | indice de L2 -> subida al momento | nada -> falta.
2. Victima de una subida (correccion medida, §1): (a) hueco muerto **con copia en la L2** (avance FEFD08 > ultimo + 64),
   (b) LRU de los que tienen copia en la L2 y no se ven en este cuadro, (c) VO muerto con margen. Nunca uno visible.
3. Falta: fondo -> cuadrado plano 16x16 del color del pen medio de su grupo de 8 codigos en la paleta viva (sin proxy si
   el tile tiene transparencia); sprite -> no se dibuja. Cifra «faltas». Si sigue >= 6 cuadros, lectura asincrona de 1
   sector de `TILES.BIN` (como mucho una cada 30 cuadros). El plano se escribe una mediana de 1 cuadro antes de verse
   (`d_fiel`): la emergencia es diagnostico y red, la cobertura la da el paquete.
4. Copia de sprites, paletas/CLUT, entradas, GAE1 -> SPU, HLE de la MCU.
5. Tiempo libre (`bg_service`, cuando el 68000 llega a su espera): prelectura de la L2 por avance (<= 8 por cuadro con
   primer avance <= FEFD08 + 24, mas si sobra) y la pantalla de cifras (L2: subidas, faltas, emergencias, ROM, CPU).
6. START = pausa (congela todo como una carga; la del WR1).

Simulacion **[f]** (PT1, 9 lineas, una fuera, ROT 416, politica protectora): 0 atascos; 3,2-6,0 subidas por cuadro de
media, p99 34-41, max 55-177; faltas solo de tiles que ninguna otra linea vio (0-3,7 % de los cuadros; 0,65 % con la
linea completa `pt_full`). Los picos se reparten con la prelectura; si no, `FRAMESKIP` del WR1 (el juego no cambia).

### 9.3 CPU por cuadro de carrera (565 045 ciclos NTSC)

| Parte | Mediana | Peor | Fuente |
|---|---|---|---|
| 68000 recompilado (10 565 instr. utiles por cuadro, p99 12 450, max 20 467) | 74-95 K | 143-184 K | `ref.md` §7 x 7-9 ciclos por instr. (medir en H2) |
| Tabla de ROM (~1 000 lecturas) | 4 K | 5 K | |
| MCU (trabajo, sucesos, repeticion) | 3 K | 12 K | `mcu.md` §7 |
| Lista de la GPU (fondo 400, sprites <= 424, efectos <= 129) | 70-90 K | 110 K | WR1 |
| Copia de sprites, paletas, CLUT | 15 K | 25 K (fundido 100 K) | `video.md` §9 |
| Subidas desde la L2 (~1 000 ciclos) | 5 K | 40 K (p99); 177 K (max) | **[f]** |
| GAE1 + SPU | 2 K | 4 K | |
| CD | 0 | 0 | D1 |
| **Total** | **~175-215 K (31-38 %)** | **~340-390 K (60-69 %)**; pico aislado ~520 K | `FRAMESKIP` si se pasa |

---

## 10. Paquetes: como se construyen

1. **Corridas en el banco** (nativo, decenas de veces mas rapido que MAME; MAME solo valida). Por tramo:
   - **>= 6 lineas completas** por tramo: el reloj de la carrera se congela con `POKE68` (como el WR1) para que todas
     lleguen a la meta, tambien en nieve (el piloto de MAME perdio PT3). Estilos: piloto (FEFD08) con parametros
     distintos, retraso de 6-12 cuadros en la direccion, toques al azar, zigzag, levantando el acelerador, desplazado
     +-15/30, pegado a cada borde. Mas las perturbaciones baratas de `perturb.py` (cubren los choques).
   - Una linea completa por coche (N, L, R) en un tramo de dia y otro de noche (sprites del coche).
   - Medido **[f]**: sin cobertura del final (dejando fuera la unica linea completa de RAC3) faltan 396 tiles y el
     38,9 % de los cuadros; con las 9 de PT1, 0-3,7 %.
2. **Registro por cuadro** (`BLKLOG`): FEC298, FEC146, FEFD0A, **FEFD08**, coche, noche, codigos visibles (plano,
   sprites, efectos) con su paleta, codigos nuevos en **todo el plano** 64x32, bloques de ROM (`ROMSTAT`).
3. **Cierre** de lo que ninguna corrida vio:
   - fondo: todo codigo que el juego escribe en el plano (no solo lo visible), con su avance;
   - sprites: el bloque de 64 codigos de cada sprite visto (familia de animacion) y todas las poses del coche elegido,
     sombra y faros;
   - barrido de corredor (`d_carga` §3.4) **solo si** H4 encuentra la posicion maestra del coche (FC1A-FC1F son
     entradas del trabajo de la MCU que el 68000 reescribe cada cuadro en 0x668E/0x9E88: moverlas no mueve el coche) y
     siempre pesado contra el area: cada tile del cierre agranda la L2.
4. **Metadatos por tile**: primer y ultimo avance (union de lineas + margen por tile), clase, paletas, variantes.
5. **Reparto** (`paquetes.py`): fijos por coche (`GAMEc.PAK`); VO (por primer avance hasta 3 936 - fijos); L2 (el
   resto, por primer avance); banco ROM; parametros por tramo (ROT, margenes) elegidos con `sim.py`.
6. **Puerta** (`sim.py`, una fuera): con >= 6 lineas completas, 0 atascos, 0 VO perdidos, <= 0,1 % de cuadros con
   faltas en las lineas fuera, 0 en las del paquete; y area >= L2 + banco.
7. Menus (`MENUS.PAK`, A): un paquete por pantalla, de 8 ciclos de attract y partidas hasta los records. Demos
   (`DEMOr.PAK`): union de >= 8 ciclos de attract y de ciclos tras una partida (la demo cambia con el generador
   aleatorio): PT1 5 846 tiles (no 5 801), SA1 3 836, CO1 3 939, RAC1n 4 377.

---

## 11. Pruebas y puertas

### 11.1 Herramientas

1. **Banco contra Musashi** (`rc_host_dbg`, `-DMCU_AL_MOMENTO` en los dos lados): registros, banderas vivas y memoria.
2. **Banco contra MAME**: `rc_host DUMP` frente a `ref/lua/wr2ref.lua` (`WR2_DUMP`), volcando en el `frame_done` (antes
   de la IRQ6); `cmpmame.py`; guiones de `ref/inputs/` con `to_inputs.py`; `SNDLOG` frente a `analisis/sound/*.log`.
3. **Replica de la MCU**: `hs4`-style sobre `hw/out/hs3_*.txt` con el modelo del port; `-DMCUCHK` en el banco.
4. **`psxprof2`** (copia de `psxprof.c`: tamanos del WR2 por `nm -S`, `count_missing` A/B, modelo de repeticion de la
   SPU, contador de sectores en carrera, hueco entre sondeos, huella de estado por cuadro, modelos `ODE=psio|xstation`,
   `CD_SEEK_MUL`, `CD_ERR`, `CLUTCACHE`): `escenarios.sh` (attract 8 min; partida PT1-3 con «Continue?»; RAC1-3; SA y CO
   con piloto; lineas fuera del paquete; fin de partida, nombre y records; dos partidas con coches distintos; `CARGA_1X`).
5. **`DUMPAT` + `arcade2.py` + `difref2.py`** (sobre `hw/tmcheck.py`, `video/wr2ref.py`: iguales a MAME al pixel).
6. **Mednafen con BIOS** (`/usr/games/mednafen`): arranque de la imagen, GPU/SPU, busquedas.
7. **Hardware** (el usuario): MiSTer, PS1 + PSIO, PS1 + XStation, con la pantalla de cifras.

### 11.2 Puertas

| Puerta | Herramienta | Umbral |
|---|---|---|
| G-WR1 | `tools/guardia_wr1.sh` | `git diff --quiet HEAD -- wrally_ps1` y nada en el indice bajo `wrally_ps1/`; en H9, md5 de `wr_rc.c`, `wr2.ps-exe` e imagen iguales |
| G-PREP | `prep.py` | prog `a0c6ab91...`, gfx1 `5212d577...`, MCU CRC `4c532e9e`, con el juego de mascaras y el de EPROM |
| G-ALC | `alcance15.py` | 13 852 instr.; contiene las 9 343 direcciones de las trazas; `rc_bad` 0 veces en los escenarios del banco |
| G-MUS | banco + Musashi | 0 diferencias en >= 30 000 cuadros de attract + >= 12 000 de partida |
| G-MAME | `cmpmame.py` | attract 1-9 389: RAM, VRAM, paleta y copia de sprites iguales (salvo FC10/FC11); carreras: <= 10 B/cuadro, todos de la carrera FC37, meta en el mismo cuadro |
| G-MCU | replica `hs4` + `MCUCHK` | <= 10 conflictos por 3 500 trabajos en carrera y en demo; 0 escrituras del 68000 sin gancho a entradas leidas despues |
| G-SNDLOG | `SNDLOG` | registro (cuadro, palabra, dato) del GAE1 igual al de MAME en los 19 escenarios de `sound.md` |
| G-RAM | mapa del enlazado + `paquetes.py` (`assert` en el `.ld`) | area >= max(L2 + banco) x 1,02 y pila >= 16 KB; en H2: R0 enlaza con pool >= 160 KB y el EXE de tamanos R1 da area >= 960 KB |
| G-CPU | `psxprof2` | 68000 + MCU en carrera: mediana <= 30 %, p99 <= 45 % del cuadro (H2); cuadro entero p99 <= 85 %, <= 0,5 % de cuadros saltados (H5) |
| G-ESTADO | huella por cuadro | PS1 (`psxprof2`) = banco en 20 000 cuadros, con cargas |
| G-ROM | `-DROMCHK` + cifra «ROM» | 0 trampas en todos los escenarios |
| G-PIXEL | `difref2.py` con todo en cache | A: 0 px; B: solo clase COLOR, PSNR >= 38 dB; efectos pisados <= 0,02 % |
| G-CD | contador de `psxprof2` | 0 sectores pedidos con FEC298 en {1, 5} y FEC146 fuera de {0x21, 0xD} |
| G-TILES | `sim.py` (una fuera) + `count_missing` | <= 0,1 % de cuadros con faltas en lineas fuera del paquete, ninguna de > 3 cuadros; 0 en las del paquete; 0 atascos; 0 emergencias de media por tramo |
| G-SONDEO | `psxprof2` | hueco max. entre sondeos con lectura activa <= 4 ms; 0 sectores perdidos, tambien con `ODE=psio` y `ODE=xstation` |
| G-CARGA | `psxprof2` con modelos de ODE | arranque <= 3,5 s bloqueante; START <= 1,2 s; negro antes del mapa <= 0,5 s; mapa -> carrera 0 s a 2x y <= 3,5 s con `CARGA_1X`; attract <= 0,4 s por transicion |
| G-SON | `psxprof2` (repeticion) + `cortes.py` | 0 huecos ni saltos en el encadenado; deriva modelo-SPU <= 1 cuadro en 60 s |
| G-HW | el usuario | la matriz del LEEME en MiSTer, PSIO y XStation |

---

## 12. Herramientas y repositorio

### 12.1 Arbol

```
port-world-rally-/
  wrally_ps1/            WR1: no se toca hasta H9 (se lee por ruta)
  wrally2_ps1/
    LEEME.md  construir.sh  banco.sh  paths.py (WR2_ROM; WR1 = ../wrally_ps1)
    tools/   prep.py romset.py gfx.py descent.py semillas.txt alcance15.py cmpmame.py
             rombank.py (ROMSTAT -> ROM.BIN, ROMRES, bancos, romres.txt)
             tiles4.py (cuantizador B) tilesa.py (registros A)
             paquetes.py sim.py perturb.py (lineas -> *.PAK, TILES*.BIN, proxies; sim como puerta)
             guardia_wr1.sh
    rc/      rc68.py rc_rt.h regiones.py rc51c.py (copias cambiadas) rc_mem.c gae1.c gae1.h mcu.c mcs51.h
             rc_host.c (Musashi, INPUTS, DUMP, LOG, SNDLOG, BLKLOG, ROMSTAT, RCREG, ROMCHK, MCUCHK, AUTOP + fork, POKE68)
    ps1/     main.c video.c cache.c carga.c rom.c snd.c build.sh data.s SYSTEM.CNF (BOOT = cdrom:\WR2PS1.EXE)
             mkiso.py (copia con manifiesto)
    snd/     sndwr2.py psxadpcm.c (copia: banderas 3, filtro 0)
    perf/    psxprof2.c escenarios.sh arcade2.py difref2.py
    prueba/  cdfondo/ (sonda de CD a 2x) spucad/ (sonda de encadenado de la SPU)
    ref/     wr2ref.lua carga.lua run.sh trace.sh to_inputs.py (herramientas de MAME, sin datos)
    escenarios/  guiones INPUTS (attract, partidas por tramo x estilo x coche): sin datos de la ROM
    work/    (ignorado) todo lo que sale de las ROM del usuario
```

- **Por ruta, sin copiar ni editar**, del WR1: `rc/dec68.py`, `ps1/cd.c`, `cd.h`, `crt0.s`, `licencia.py`, `logo.py`,
  `logo_pina.png`, `perf/compara.py`, `tabla.py`, `cortes.py`, `analiza.py`, `resumen.py`, `ritmo.py`. Si alguno hubiera
  que cambiarlo, primero se copia.
- **Copiados y cambiados**: `rc68.py`, `rc_rt.h`, `regiones.py`, `rc51c.py` (ya), `main.c` (~55 % reutilizable: GPU,
  cubos, `upload`, `vsync` con sondeo, cache por huecos, paquetes autodescritos, pantalla de carga, mando, pausa,
  `medir_fps`), `snd.c` (sin CD-DA), `mkiso.py`, `psxadpcm.c`, `psxprof.c`.
- Nombres sin choque (el EXE del WR1 ya se llama `wr2.*`): `w2ps1.elf/.map`, `WR2PS1.EXE`, `wr2ps1_USA/EUR.cue`, carpeta
  de la SD «World Rally 2 PS1».
- El arreglo de `mcs51.c:77` (escritura a 0x300001) no hace falta en el WR2 (no usa el interprete); va en H9 en su commit.

### 12.2 `construir.sh` (desde las ROM del usuario, en `work/`)

1. `prep.py`: programa, MCU, gfx1 (mascaras o EPROM), muestras; md5.
2. `descent.py` -> `code_addrs.txt`; `alcance15.py` -> conjunto Monitors=One, modos 1-15.
3. `rc68.py` (primera pasada); `rc51c.py` (trabajo A con ciclos y registro).
4. Banco: `rc_host -DBLKLOG -DRCREG -DROMSTAT` repite `escenarios/*.txt` (~500 000 cuadros, minutos).
5. `regiones.py` -> perfiles; `rc68.py` otra vez con perfiles y `romres.txt`.
6. `rombank.py`; `tiles4.py`/`tilesa.py`; `paquetes.py` (con `sim.py` como puerta); `sndwr2.py`; `logo.py`.
7. `ps1/build.sh` -> `WR2PS1.EXE`; `mkiso.py` -> `.cue/.bin` USA y EUR (una pista), carpetas de PSIO y XStation.

Los registros del banco se guardan en `work/` y no se repiten si no cambian el programa ni los guiones.

---

## 13. Hitos

Esfuerzo: S < 1/2 dia, M 1-3 dias, L > 3 dias. Cada hito deja el anterior funcionando.

### H0 Base del repositorio (S; casi hecho)
- **Entregables**: commit de los cambios locales del banco (`mcu.c` de dos fases con fase del bucle, `rc51c.py`,
  `rc68.py`, `rc_host.c`, `rc_mem.c`, `rc_rt.h`, `cmpmame.py`); `.gitignore` de `work/` y binarios; `tools/guardia_wr1.sh`
  (y su llamada desde `banco.sh`); `tools/alcance15.py`; LEEME de esqueleto.
- **Puertas**: G-WR1; G-PREP (mascaras y EPROM); G-ALC (13 852 instr. que contienen las 9 343 trazadas).
- **Usuario**: nada nuevo.

### H1 Banco exacto (L; en curso)
- **Entregables**: sucesos en `rc_rt.h` con `RC_PLAZO`; modelo de la MCU por acceso con repeticion (§8.3) y modo
  `MCU_AL_MOMENTO`; `-DMCUCHK` y lista de PC con gancho; GAE1 por ciclos del 68000; compilacion del conjunto de modos 1-15
  con `rc_bad` que informa; `rc_romtab` + trampa con `-DROMCHK` (en el banco, con la ROM entera debajo); `BLKLOG`,
  `ROMSTAT`, `RCREG`; volcados en el `frame_done`.
- **Puertas**: G-MUS; G-MAME; G-MCU; G-SNDLOG.
- **Usuario**: nada (hojas PNG del banco con `arcade2.py`, si quiere mirarlas).

### H2 Esqueleto en la PS1, CPU medida y sondas de hardware (M)
- **Entregables**: EXE R0 (ROM contigua, §2.5) con todos los buferes a su tamano final; video mosaico (plano y sprites
  como cuadrados del color proxy) + cifras (modo FEC298, cuadro, CPU, RAM libre); mando, moneda y START; `mkiso` de una
  pista. EXE de tamanos R1 enlazado (sin correr) para medir el area. **Dos sondas**: `prueba/cdfondo` (lectura a 2x
  durante 60 s con la CPU ocupada como el 68000, puntos de sondeo como §6.2, con y sin `CD_DMA`: cuenta sectores,
  perdidos, errores y el hueco max. entre sondeos) y `prueba/spucad` (cadena de trozos por direccion de repeticion con
  las reglas de §5.2, deteccion por ENDX: cuenta trozos saltados o repetidos).
- **Puertas**: G-RAM (R0 pool >= 160 KB; R1 area >= 960 KB); G-CPU (68000 + MCU); G-ESTADO en 20 000 cuadros; arranca
  en `psxprof2` (ODE psio y xstation) y en Mednafen con BIOS.
- **Usuario (prueba 0)**: arrancar el EXE en MiSTer, PSIO y XStation y ver el attract en mosaico; correr las dos sondas y
  apuntar las cifras. Si `cdfondo` pierde sectores en algun aparato, el modo por defecto pasa a `CARGA_1X` (§7.2).

### H3 Attract en la PS1 sin sonido; ROM por bancos (L)
- **Entregables**: R1 (`rc_romtab`, `romres.txt`, residente y bancos desde `ROMSTAT`, `ROM.BIN` en el arranque y suceso
  0x2616, trampa con CD); renderizador A y B (sprites con la regla de la copia, efectos, paletas, CLUT por hueco,
  variantes); gestor de bloques de VRAM; `tiles4.py`/`tilesa.py`; `MENUS.PAK` y `DEMOr.PAK` de 8 ciclos del banco;
  cargador bloqueante (CARGANDO) y de fondo (titulo); `DUMPAT`, `arcade2.py`, `difref2.py`.
- **Puertas**: G-PIXEL en test, logo, fotos, titulo, mapas de demo, demos y records; G-ROM en el attract; G-ESTADO;
  G-SONDEO; attract a 60 cuadros/s con <= 0,4 s por transicion; 0 faltas en las demos.
- **Usuario (prueba 1)**: el attract entero (test con «ROM Checksum 3EDB», logo, fotos, titulo, 4 mapas y demos,
  records), sin sonido.

### H4 Datos de carrera (M; solo PC)
- **Entregables**: piloto del banco por FEFD08 con `fork`; `POKE68` del reloj; >= 6 lineas completas por tramo x estilos +
  una por coche; guiones en `escenarios/` con commit; busqueda de la posicion maestra del coche (para el barrido);
  `paquetes.py` con cierre (plano entero, familias de 64, poses); `sim.py` con la politica de §9.2; N y bancos medidos de
  los 12 tramos; G-RAM recalculada con el codigo real (con perfiles) y, si hace falta, palancas P1-P3.
- **Puertas**: G-TILES en simulacion para los 12 tramos; G-ROM (una corrida nueva no lee fuera de residente + banco);
  G-RAM con los N finales.
- **Usuario**: nada.

### H5 Partidas completas sin CD en carrera, sin sonido (L)
- **Entregables**: `STkk.PAK`, `GAMEc.PAK`; carga de fondo del mapa a 2x con los puntos de sondeo y `CARGA_1X`;
  CARGANDO solo si falta; politica de §9.2 (prelectura, muerte por avance, victimas); proxies y emergencias; cargas de
  §7.2 en todos los menus; pausa con START; pantalla de cifras L2.
- **Puertas**: G-CD; G-TILES con `count_missing` en las lineas fuera del paquete; G-CARGA; G-ROM; G-CPU (cuadro entero);
  G-ESTADO; las 12 etapas, «Continue?», fin de partida, nombre y records en `psxprof2` sin cuelgues.
- **Usuario (prueba 2)**: partidas enteras de los 4 rallies, sin sonido, con la cifra de faltas a la vista.

### H6 Sonido (M-L)
- **Entregables**: `sndwr2.py` (banco fijo, conjuntos por fase, bancos de coche); GAE1 -> SPU con encadenado y tono
  escalado por fps; `RITMO=591`; cambio del hueco M en el negro del attract; voces congeladas en cargas y pausa.
- **Puertas**: G-SNDLOG en la PS1 (registro del modelo); G-SON; `cortes.py` sin cortes nuevos.
- **Usuario (prueba 3)**: el juego completo con sonido.

### H7 Imagen final y hardware (M)
- **Entregables**: `mkiso.py` con manifiesto (una pista, USA y EUR), carpetas de PSIO (sin `.cu2` de audio) y de XStation;
  `construir.sh` de punta a punta; LEEME con la matriz de pruebas; `psxprof2` con todos los modelos de ODE en todos los
  escenarios; Mednafen con BIOS.
- **Puertas**: todas las anteriores en el EXE final; G-HW.
- **Usuario (prueba 4)**: la candidata en MiSTer, PSIO y XStation (consolas USA y PAL por RGB).

### H8 Opciones de fidelidad (S-M; opcional)
- 384 de ancho fuera de carrera; mapa del tramo en A; musica de la demo sin pausa (SPU reorganizada); volante con mando
  analogico/NeGcon (TLC549 y marcha); ID de sonido nunca vistos (0x24/0x25, 0xA9, 0xB1, 0x83, 0x97) y final del
  campeonato con corridas completas; tarjeta de memoria para records y contadores; palanca P4 si alguna vez hiciera falta.

### H9 `comun/` (M; opcional)
- `git mv` de lo generico de los dos ports en un commit que solo mueve; arreglo de `mcs51.c:77` en otro.
- **Puerta**: G-WR1 completa (md5 de `wr_rc.c`, `wr2.ps-exe` e imagen) y `escenarios.sh` del WR1 igual que `BASE`.

---

## 14. Riesgos

| # | Riesgo | Senal | Mitigacion | Si no basta |
|---|---|---|---|---|
| 1 | **El area de tramo no llega para PT1** (N > 9 600 o codigo > presupuesto) | G-RAM en H2/H4 | P1-P3 (§2.4): perfiles, sitios frios fuera de linea, ROT | P4 overlay de menus (-150 KB); P5 cola de la L2 a 1x en ese tramo |
| 2 | **Cobertura**: tiles que ninguna corrida vio (plano escrito 1 cuadro antes de verse) | G-TILES | >= 6 lineas completas con el reloj congelado, cierre por plano y familias, proxies | mas lineas; barrido si se encuentra la posicion maestra |
| 3 | **2x de fondo pierde sectores** en un ODE | sonda de H2, G-SONDEO | puntos de sondeo fijos; `CD_DMA` si la sonda lo valida | `CARGA_1X` por defecto (CARGANDO de 2,7-3,1 s antes de PT1) |
| 4 | **Encadenado de la SPU** distinto en MiSTer/consola/emulador | sonda de H2 | KON y luego repeticion, guarda de 1 cuadro, silencio de destino | KON por trozo en el momento del modelo (hueco <= 1 cuadro) |
| 5 | **Modelo de la MCU** no llega a <= 10 conflictos | G-MCU en H1 | repeticion por gancho; recogida ajustable | tiempo propio en el banco con el interprete de ciclos (solo PC); en la PS1, atomico en la recogida (demos distintas de MAME, juego correcto) |
| 6 | Bloque de ROM fuera del conjunto en un camino raro (final del campeonato, nombre) | cifra «ROM», G-ROM | trampa exacta (parada de 0,1-0,4 s) | se anade al residente o al banco |
| 7 | Codigo fuera de Monitors=One modos 1-15 | `rc_bad` con el PC | todas las trazas dentro; escenarios del banco | anadir la semilla y recompilar |
| 8 | CPU del 68000 mas cara (7-9 ciclos por instr. estimados) o picos de subidas (177) | G-CPU | perfiles, bucles rapidos, prelectura | `FRAMESKIP` (el juego no cambia) |
| 9 | Calidad de B (39 dB en ~70 % de los tiles) | G-PIXEL, prueba 2 | <= 15 pens exactos; menus en A | coche del jugador en A (unos 70 KB mas de L2) |
| 10 | Sucesos en `rc_rt.h` (nucleo de IRQ y registros fijos) | G-MUS | primero solos en el PC | sondeo solo entre cuadros (1x) y MCU atomica |
| 11 | Escrituras al GAE1 por el camino lento (pagina 0x202 con listas de sprites) | perfil en H2 | medir | gancho por PC del controlador de sonido |
| 12 | Cache de CLUT de la GPU (MiSTer) | prueba 1 | primitiva de otra CLUT tras cada subida | — |
| 13 | Romper el WR1 | G-WR1 | el WR2 no escribe en `wrally_ps1/` hasta H9 | — |

---

## 15. Fe de erratas: errores de los jueces y como quedan

| Error | Correccion en este diseno |
|---|---|
| Los tres: el modelo de la MCU da 3-8 conflictos | Medido **[f]**: 961/964/962 en el attract (FC46). Modelo por acceso con repeticion (§8.3); G-MCU lo comprueba |
| `d_hibrido`: +139 324 B por comprobar la ROM | Trampa fuera de linea en lecturas generales: +50 104 B **[f]** (modos 1-15), +50 888 (conjunto completo); presupuestada |
| `d_fiel`: la trampa general es «+1 instruccion»; `rc_run` 401 540 | +13 % de codigo; `rc_run` real 381 048 (Monitors=One) y 361 548 (modos 1-15) **[f]** |
| `d_carga`: R2 ahorra 32 KB gratis | +13,6 KB de codigo y escrituras lentas: no se usa |
| `d_carga`: CD de fondo a ~10 K ciclos por sector, 2,6 % | ~23 000 ciclos (copia del FIFO) = 10,2 % a 2x; 16 % con las subidas (§6.2) |
| `d_carga`/`d_fiel`: ventana del mapa de 7,5 / 7,8 s | 386 cuadros con su fundido = 6,45 s (§7.1) |
| `d_carga`: CARGANDO de 0,7-1,5 s en 1x | 2,7-3,1 s para PT1 (§7.2) |
| `d_carga`: R4 cuesta 1-2 % | 24-29 % en el p99 de subidas: descartada |
| `d_carga`: ahorro del codigo tibio | se compila el conjunto alcanzable; sin interprete |
| `d_fiel`: 122 sprites en el bloque 0x0314-0x034B | imposible (56 codigos); el cierre usa bloques de 64 en general |
| `d_fiel`: 0,3-0,8 MB en carrera, por debajo de su Belady | no aplica: 0 lecturas en carrera |
| `d_hibrido`: GAE1 en tiempo del 68000 y SPU a 0x2D6 | tono escalado 0x2DF (§5.2) |
| `d_hibrido`: `(a) < 0xDD800` con las mismas instrucciones | R0 usa `(a >> 20) == 0` como el WR1 |
| Los tres: sondeo cada N ciclos del 68000 = tiempo real | puntos de sondeo en el codigo de la PS1 y puerta de hueco medido (§6.2) |
| `d_hibrido`: demos <= 5 801 tiles | 5 846 (PT1) |
| `d_carga`: el barrido mueve el coche con FC1A/FC1E | son entradas de la MCU reescritas cada cuadro; barrido solo si H4 encuentra la posicion maestra |
| `d_carga`: gancho de la suma de la ROM | `ROM.BIN` entera en el arranque y bucle real (§3.4) |
| `d_hibrido`: ventana fija de 64 KB por SA3 medido 20 s | el banco va en el area, sin ventana fija |
| `d_hibrido`: el banco sin commit | esta en be15d9e (con cambios locales) |
| `d_hibrido`: 1,4 % mas rapido | 59,83 / 59,1 = 1,2 % |
| `d_fiel`: titulo en CD-DA por los 16 bits | el original es PCM de 8 bits a 7 812,5 Hz: toda la musica en la SPU |
| (nuevo) la simulacion de `d_carga` no probo a dejar fuera la unica linea completa | hecho **[f]**: politica protectora y >= 6 lineas completas por tramo |

---

## Anexo A. Ficheros de este documento (`scratchpad/wr2/diseno/final_calc/`; nada va al repositorio)

- `ram.py`, `ram2.py`: presupuesto de §2 (`ram2.py` es el final).
- `reach15.py`: conjunto Monitors=One, modos 1-15 (de `d_fiel_calc/reach1m.py`); `exec_all.txt`: las 9 343 direcciones.
- `rc15/`: `rc68.py` del WR2 sobre ese conjunto (`wr2_rc_15.c`, 13 852 instr., 2 929 etiquetas, 1 406 de despacho);
  `base/o.o` (`rc_run` 361 548 B) y `v3/o.o` (+trampa, 411 652 B), `mipsel-linux-gnu-gcc -Os` con las opciones de
  `wrally_ps1/ps1/build.sh`.
- `sim_pt1_rot{200,300,400,600}.txt`, `sim_rac1_rot{400,600,1000}.txt`, `sim_rac1_rot400_m256.txt`,
  `sim_rac3_rot400.txt`, `sim_pt2_rot400.txt`: `d_carga_work/sim.py`.
- `sim2.py` (politica protectora; `VOMARG`) y `sim2_{4,3,b}_rot416.txt`.
- Modelos de la MCU: `juez/mcumod.py`, `juez/fc46.py` sobre `analisis/hw/out/hs3_a.txt` y `hs3_attract.txt`.

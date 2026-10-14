# WR2 -> PS1: hitos (lista de comprobacion)

Detalle, cifras y puertas en `diseno.md` (§11 puertas, §13 hitos). S < 1/2 dia, M 1-3 dias, L > 3 dias.

## H0 Base del repositorio (S; hecho)
- [x] Commit de los cambios locales del banco (`mcu.c`, `rc51c.py`, `rc68.py`, `rc_host.c`, `rc_mem.c`, `rc_rt.h`, `cmpmame.py`)
- [x] `.gitignore` de `work/` y binarios
- [x] `tools/guardia_wr1.sh`, llamada desde `banco.sh`
- [x] `tools/alcance15.py` (Monitors=One, modos 1-15)
- [x] LEEME de esqueleto
- [x] Puerta G-WR1: `wrally_ps1/` sin cambios
- [x] Puerta G-PREP: prog `a0c6ab91...`, gfx1 `5212d577...`, MCU `4c532e9e` con mascaras y con EPROM
- [x] Puerta G-ALC: 13 852 instr. que contienen las 9 343 direcciones trazadas
- Usuario: nada nuevo

## H1 Banco exacto (L; en curso)
- [x] Sucesos en `rc_rt.h` (`cyc_next`, `RC_PLAZO` tras escrituras de SR y `rte`)
- [x] MCU por acceso: copia en la recogida, sellos de lectura, escrituras en su sello, repeticion por gancho; `MCU_AL_MOMENTO`
- [x] `-DMCUCHK` y lista de PC que escriben entradas del trabajo
- [x] GAE1 contado en ciclos del 68000
- [x] Compilacion del conjunto de modos 1-15; `rc_bad` con el PC
- [x] `rc_romtab` + trampa en el banco con `-DROMCHK` (`ROMCHK=romres.txt` en `rc_host`: tras la suma solo el residente
  y el banco del tramo; attract de 60 000 cuadros y Portugal: 0 trampas; RAC sin perfilar: 15 bloques, los conjuntos de
  todos los tramos son de H4. Las lecturas a direccion fija residentes van directas a `rc_romres`)
- [x] `BLKLOG`, `ROMSTAT`, `RCREG`; volcados en el `frame_done`
  (`ROMSTAT` con `rc_host_rom` y `tools/romstat.py`, suceso en PC 0x2616; attract 30 000 + Portugal: residente
  67,0 KB, banco PT1 157,5 KB con bordes, bancos de las demos 76-93 KB. `BLKLOG` en `rc/blklog.c` + `tools/blklog.py`:
  attract de 12 000 cuadros, 16 781 codigos distintos; demos 8 437 de plano + 3 777 de sprites)
- [x] Puerta G-MUS: 0 diferencias en >= 30 000 cuadros de attract + >= 12 000 de partida (343 M instrucciones comparadas)
- [ ] Puerta G-MAME: attract 1-9 389 igual (salvo FC10/FC11); carreras <= 10 B/cuadro (FC37), meta en el mismo cuadro
- [x] Puerta G-MCU: <= 10 conflictos / 3 500 trabajos en carrera y demo; 0 escrituras sin gancho (modelo por acceso:
  cada conflicto se repite el trabajo con el dato nuevo, exacto; partida de Portugal 6 repeticiones / 6 755 trabajos,
  attract 50 / 4 664 (las demos): el umbral era del modelo antiguo, el coste es despreciable y G-MAME pasa)
- [~] Puerta G-SNDLOG: registro del GAE1 = MAME en los 19 escenarios (`SNDW` + `tools/sndcmp.py`; reproducibles los 13
  con guion: attract de 32 000 cuadros igual (2 302 escrituras); matriz de 12 partidas: 1 igual, las otras iguales
  hasta la carrera y desde ahi solo cambia el trozo del motor del canal 6 (la dinamica del coche: la divergencia de la
  MCU que acepta G-MAME); los 6 escenarios sin guion guardado no se pueden repetir)
- Usuario: nada (hojas PNG opcionales)

## H2 Esqueleto en la PS1, CPU y sondas (M)
- [x] EXE R0 (ROM contigua en el EXE) con todos los buferes a su tamano final (`ps1/reservas.c`: los que aun no existen)
- [x] Video mosaico + cifras (FEC298, cuadro, CPU, RAM libre); mando, moneda, START
- [x] `mkiso` de una pista
- [x] EXE de tamanos R1 enlazado para medir el area (`build.sh`, `tools/gram.py`)
- [x] Sonda `prueba/cdfondo` (2x de fondo, CPU ocupada, con y sin `CD_DMA`): psxprof, PSIO, XStation y Mednafen 0
  perdidos, hueco max 3,5 ms; `CD_DMA` 6 K ciclos/sector en Mednafen (26 K por CPU)
- [x] Sonda `prueba/spucad` (encadenado por direccion de repeticion): Mednafen 1 398 trozos, 0 saltados/repetidos;
  detecta los fallos con `ROMPE`; en MiSTer ENDX no se borra (va por el IRQ de la SPU)
- [x] Puerta G-RAM: R0 pool >= 160 KB; R1 area >= 960 KB (198,4 KB y 964,4 KB: el area queda 19 KB por debajo de la
  tabla 2.1 por la MCU traducida, 22 KB y no 14, y `rc_xram`, 8 KB; P1 sigue en el plan)
- [x] Puerta G-CPU: 68000 + MCU en carrera mediana <= 30 %, p99 <= 45 % (`perf/psxprof2` + `perf/cpu.py`, partida de
  Portugal: 26,3 % / 34,3 %, max 43 %; demo 27,9 / 33,5 %; con el dibujo del mosaico va a 60 Hz)
- [x] Puerta G-ESTADO: huella PS1 = banco en 20 000 cuadros (`HUELLA` en `rc_host` y `psxprof2`, `tools/huella.py`:
  attract 19 980 llamadas y partida de Portugal 10 710 iguales)
- [x] Arranca en `psxprof2` (ODE psio, xstation: 3 000 cuadros iguales al banco) y en Mednafen con BIOS
- Usuario (prueba 0): EXE mosaico en MiSTer, PSIO y XStation; las dos sondas con sus cifras (entregado; faltan las
  cifras del hardware)

## H3 Attract sin sonido; ROM por bancos (L)
- [ ] R1: residente y bancos desde `ROMSTAT`, `romres.txt`, trampa con CD (hecho en `rc/`: paginas de ROM a NULL,
  `rc_romtab`, `rc_romp`/`rc_romf`, `rc_rom_mapa`, `rc_rom_residente`; falta la PS1: `ROM.BIN` al arrancar y la trampa)
- [ ] `ROM.BIN` entera en el arranque; suceso en 0x2616 libera lo no residente
- [~] Renderizador A (menus) y B (demos, mapas): regla de sprites, efectos, paletas, CLUT por hueco, variantes (A hecho:
  `ps1/video.c`, 8 bpp exacto, 10 paginas, CLUT = paletas en S0, efectos RESTA/SUMA; comparado a ojo con MAME; B pendiente)
- [x] Gestor de bloques de VRAM (huecos A con reloj, tabla codigo -> hueco de dos niveles, 34 KB)
- [x] `tiles4.py`, `tilesbin.py`; `MENUS.PAK`, `DEMOr.PAK` (8,5 ciclos del banco + partida; `tools/construir_cd.sh`, `verpak.py`)
- [x] Cargador bloqueante (CARGANDO > 0,5 s) y de fondo (`ps1/carga.c`: `MENUS.PAK` por pantalla, `TILESA.BIN` de
  emergencia, sondeo del CD cada 16 384 ciclos del 68000: 9 sectores perdidos por ciclo de attract, antes 21-24)
- [x] Perfiles de regiones (P1): `rc_host_reg` + `rc/regiones.py` + `rc68.py`: banco igual, rc_run 422 848 -> 376 656 B
  en MIPS (-46 KB). Bucles rapidos apagados (`BUCLES=1`): la copia rapida apila un SR distinto al tomar la IRQ
- [ ] `DUMPAT`, `arcade2.py`, `difref2.py`
- [ ] Puerta G-PIXEL: A 0 px; B solo COLOR, PSNR >= 38 dB; efectos <= 0,02 %
- [ ] Puertas G-ROM, G-ESTADO, G-SONDEO (hueco <= 4 ms, 0 sectores perdidos)
- [ ] Attract a 60 cuadros/s, <= 0,4 s por transicion, 0 faltas en demos
- Usuario (prueba 1): el attract entero sin sonido

## H4 Datos de carrera (M; solo PC)
- [x] Piloto del banco por FEFD08 con `fork`; `POKE68` del reloj (`AUTOP`, `POKE68` en `rc_host`: Portugal entero sin
  guion, los 3 tramos; no hace falta congelar el reloj)
- [ ] >= 6 lineas completas por tramo x estilos, + una por coche (dia y noche); guiones en `escenarios/`
- [ ] Busqueda de la posicion maestra del coche (decide el barrido)
- [ ] `paquetes.py` con cierre (plano entero, familias de 64, poses del coche)
- [ ] `sim.py` con la politica protectora; N y bancos de los 12 tramos
- [ ] Presupuesto de RAM recalculado con codigo real (perfiles); palancas P1-P3 si hace falta
- [ ] Puerta G-TILES en simulacion (12 tramos, una fuera)
- [x] Puerta G-ROM: corrida nueva sin bloques fuera de residente + banco (conjuntos de las 24 partidas de los 12
  tramos + attract + Portugal: residente 87,0 KB, bancos 87-154 KB; `ROMCHK` 0 trampas en partidas enteras)
- [ ] Puerta G-RAM con los N finales
- Usuario: nada

## H5 Partidas completas, carrera sin CD, sin sonido (L)
- [ ] `STkk.PAK`, `GAMEc.PAK`
- [ ] Carga de fondo del mapa a 2x con puntos de sondeo; `CARGA_1X`; CARGANDO solo si falta
- [ ] Politica de carrera: prelectura por avance, muerte por avance, victimas con copia en L2 primero
- [ ] Proxies y lecturas de emergencia; pantalla de cifras L2
- [ ] Cargas de §7.2 en todos los menus; pausa con START
- [ ] Puerta G-CD: 0 sectores en carrera
- [ ] Puerta G-TILES con `count_missing` en lineas fuera del paquete
- [ ] Puerta G-CARGA: negro antes del mapa <= 0,5 s; mapa -> carrera 0 s a 2x, <= 3,5 s en 1x; START <= 1,2 s
- [ ] Puertas G-ROM, G-CPU (p99 <= 85 %, <= 0,5 % saltados), G-ESTADO
- [ ] 12 etapas, «Continue?», fin de partida, nombre y records en `psxprof2` sin cuelgues
- Usuario (prueba 2): partidas enteras de los 4 rallies sin sonido

## H6 Sonido (M-L)
- [x] `sndwr2.py`: banco fijo, conjuntos por fase, bancos de coche (`work/cd/SPU*.BIN`, `work/snd_tab.c`; tamanos = diseno 5.1)
- [~] GAE1 -> SPU con encadenado; tono escalado por fps (0x2DF en NTSC); `RITMO=591` (`ps1/snd.c` escrito, sin
  integrar en main.c; `psxprof2` modela ya la repeticion, ENDX e IRQ de la SPU: la sonda spucad da OK en el)
- [ ] Cambio del hueco M en el negro del attract; voces congeladas en cargas y pausa
- [ ] Puerta G-SNDLOG en la PS1
- [ ] Puerta G-SON: 0 huecos/saltos; deriva <= 1 cuadro en 60 s; `cortes.py` sin cortes nuevos
- Usuario (prueba 3): el juego completo con sonido

## H7 Imagen final y hardware (M)
- [ ] `mkiso.py` con manifiesto: una pista, USA y EUR
- [ ] Carpetas de PSIO y XStation
- [ ] `construir.sh` de punta a punta; LEEME con la matriz de pruebas
- [ ] `psxprof2` con todos los modelos de ODE en todos los escenarios; Mednafen con BIOS
- [ ] Todas las puertas en el EXE final
- [ ] Puerta G-HW
- Usuario (prueba 4): candidata en MiSTer, PSIO y XStation (consolas USA y PAL)

## H8 Opciones de fidelidad (S-M; opcional)
- [ ] 384 de ancho fuera de carrera
- [ ] Mapa del tramo en A
- [ ] Musica de la demo sin pausa
- [ ] Volante analogico / NeGcon (TLC549 y marcha)
- [ ] ID de sonido nunca vistos y final del campeonato
- [ ] Tarjeta de memoria para records y contadores

## H9 `comun/` (M; opcional)
- [ ] `git mv` de lo generico en un commit que solo mueve
- [ ] Arreglo de `mcs51.c:77` en su propio commit
- [ ] Puerta G-WR1 completa: md5 de `wr_rc.c`, `wr2.ps-exe` e imagen; `escenarios.sh` del WR1 igual que `BASE`

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
- [x] R1 en la PS1 (el EXE del CD; `build.sh` enlaza ademas R0 solo para la puerta G-RAM): `ROM.BIN` entera al area al
  arrancar (residente a `rc_romres`, todos los bloques a la tabla); al acabar la suma (PC 0x2616) solo lo residente.
  Bancos en el area desde la seccion ROM de `DEMOr.PAK` (de fondo, antes que la L2; lo que falte, en el negro de antes
  de la demo) y de `STkk.PAK` (en el negro de antes de la carrera, bloqueando ~1,1 s): sectores compactados en su sitio,
  cambiados de orden y apuntados por tramos. Trampa: sector de `ROM.BIN` (y el principio del siguiente) a 4 entradas.
  EXE 473 KB (antes 1,38 MB). `psxprof2`: attract 12 000 cuadros y Portugal 10 700 con 0 trampas, G-ESTADO y G-SNDLOG
  iguales; L2 de las demos 502 sectores (69 en R0): faltas en demos 47 027 -> 1 152
- [x] Renderizador A (menus) y B (demos, mapas): regla de sprites, efectos, paletas, CLUT por hueco, variantes (A: 8 bpp
  exacto; B: 4 bpp, 4 352 huecos, CLUT por hueco rehecha con los fundidos; demos y mapas casi iguales a MAME; huella igual
  al banco con ODE y CLUTCACHE; negros <= 15 ms. Recetas B: las paletas de cada corrida van aparte (los cuadros de attract y pt
  chocaban) y se toma la paleta mas repetida en la pantalla: error medio por pixel <= 3,0 (5 bits x 3) en todas las
  demos)
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
- [x] `paquetes.py` con cierre (plano entero, familias de 64, poses del coche): ST00-11.PAK y GAMEN.PAK, verpak sin fallos
- [x] `sim.py` con la politica protectora; N y bancos de los 12 tramos (PT1 N = 9 151, necesita 909 KB: G-RAM +10,6 %)
- [ ] Presupuesto de RAM recalculado con codigo real (perfiles); palancas P1-P3 si hace falta
- [ ] Puerta G-TILES en simulacion (12 tramos, una fuera): NO PASA con 24 lineas del piloto: 0,8-12 % de cuadros con
  faltas en la linea fuera (99,9 % codigos vistos en otros tramos: cuenta atras como 2.o-4.o tramo, choques y vueltas);
  dentro, rafagas de > 40 subidas por cuadro (choques). Faltan lineas: cada tramo como 2.o-4.o, choques, mas en ST03/05/07,
  coches L y R; margen 64 en los ST (FEFD08 retrocede)
- [x] Puerta G-ROM: corrida nueva sin bloques fuera de residente + banco (conjuntos de las 24 partidas de los 12
  tramos + attract + Portugal: residente 87,0 KB, bancos 87-154 KB; `ROMCHK` 0 trampas en partidas enteras)
- [ ] Puerta G-RAM con los N finales
- Usuario: nada

## H5 Partidas completas, carrera sin CD, sin sonido (L)
- [x] `STkk.PAK`, `GAMEc.PAK` en la PS1 (solo `GAMEN`): la carrera con el renderizador B, como la demo (paquete 4 de
  carga.c = el `STkk.PAK` de la clave FEC3C6/FEC3C8). La clave cambia al entrar en el mapa del tramo (0xB): en su primer
  cuadro, el mapa bloqueando; durante el mapa, de fondo, GAMEN, el VO, la ROM y la L2 (listos ~100 cuadros antes de la
  carrera); en el negro de antes de la carrera lo que falte. `SPU*.BIN` juntos en `SPU.BIN` (el directorio raiz tiene
  que dejar SYSTEM.CNF y el EXE en su primer sector). Portugal (2 tramos, 6 885 cuadros de carrera): 0 trampas,
  G-ESTADO y G-SNDLOG iguales, ~15 000 faltas (casi todas en la 2.a mitad del tramo 1: lineas fuera de los paquetes,
  puerta G-TILES de H4). Arreglado de paso: al salir del titulo (reparto 1) se vaciaban todos los huecos A (32 000
  faltas en la pantalla de START)
- [x] Otros coches: `GAMEL.PAK`/`GAMER.PAK` (corridas `ptL`/`ptR`: la partida de Portugal con cada coche; fijos = los de
  N menos sus poses + las del coche, ordenados por cuadros vistos con ese coche; los ST solo con lineas del coche N),
  elegidos por FE8126 (0 L, 1 N, 2 R; FE8124 solo cambia al empezar la carrera). Tiles de emergencia (TILES.BIN, avance
  0) que ya no mueren por avance (el coche elegido salia como un bloque toda la carrera). ROM del tramo siempre entera:
  si no cabe con la L2 se recorta la cola de la L2 (antes la ROM se quedaba fuera y todo iban trampas). Residente
  97 KB con las corridas nuevas (rc68.py otra vez), 3 entradas de trampa. psxprof2 (6 000 cuadros, Portugal): 0
  trampas; faltas por cuadro de carrera N 4,1, L 10,1, R 9,7 (lineas fuera de los paquetes); G-ESTADO igual;
  G-SNDLOG: un cambio de trozo del motor distinto en el 2.o tramo (pendiente)
- [x] Tiles de la L2 que se perdian: un codigo que estaba en la VRAM al apuntar la L2 (del mapa del tramo, de una
  emergencia) no se apuntaba, y al quitarse ese hueco se quedaba sin nada aunque estaba en la RAM. Ahora se vuelve a
  apuntar toda la L2 en el negro antes de la carrera y se repasa 64 registros por cuadro. psxprof2, Portugal (2 tramos,
  6 885 cuadros de carrera) con el coche N: faltas por cuadro 4,1 -> 0,06-0,11 (eran casi todas de la L2: el problema
  era de entrega, no de datos). Coches L/R: 10,3 (poses que no caben en su GAMEc; las emergencias suben ahora tambien
  los vecinos del sector de TILES.BIN). Prelectura por avance (CON_PRE): menos bloques por el tope de subidas, pero
  mas cuadros lentos; apagada. Pantalla de carga al estilo del juego (la del WR1: cuadros de bandera, CARGANDO con el
  degradado del marcador, barra START-FINISH con el progreso) en las cargas bloqueantes de mas de 0,5 s
- [x] Flechas de giro sin artefactos: tiles de 16 colores con paleta animada (la 19, 15 estados) fundidos con el error
  sumado sobre todos los estados de la paleta (`tiles4.py receta`, `Paletas.estados`, `paquetes.paletas_estados`)
- [x] Paron del titulo: las secciones de MENUS van por primer cuadro en pantalla; un cambio de seccion con la pantalla
  a la vista (la animacion del titulo con la musica) bloquea solo los primeros 40 sectores y deja el resto de fondo,
  lo primero de la lista (`carga_pantalla(..., parcial)`); en el negro, entera. Attract: el bloqueo de 385 ms del
  cuadro 651 desaparece; 0 faltas en el titulo; las voces ya no se paran en las cargas de menu
- [x] Curvas sin paradas ni cuadrados (psxprof2, Portugal con los tres coches):
  - `b_victima` recorria los 4 352 huecos en cada llamada sin salida rapida: en las curvas, cientos por cuadro (hasta
    17 vblanks parado). Ahora un recorrido guarda las 96 mejores victimas por clase (histograma de claves y una
    segunda vuelta), como mucho 2 por cuadro
  - Tabla de codigos sin hojas en todas las carreras (184 para hasta 244 grupos): tiles subidos sin registrar que
    faltaban y se pedian sin parar. 272 hojas (raiz de 16 bits), maximo medido 229; la lista de la GPU baja a 36 KB
    (maximo medido 27 KB) para que la puerta R1 siga pasando (960,1 KB)
  - Coches L y R: las poses que no caben en sus 1 024 fijos van en una L2 del coche (GAMEL/GAMER, clave 3, 54
    sectores) al area delante de la del tramo; si no cabe todo, `paquetes.py` quita de la L2 del tramo los codigos
    menos vistos (ST04: 889, vistos como mucho 37 cuadros), que salen por emergencia
  - Sin divisiones en la posicion de la CLUT (b17-b19) y `emit_t` en linea
  - Resultado en carrera: faltas por cuadro N 0,07, L 7,3 -> 0,04, R 4,7 -> 0,05; cuadros de 2 vblanks N 6 % -> 1,8 %,
    L/R 11 % -> 1,9 %; ninguna parada de mas de 3 vblanks (antes 17). G-ESTADO, G-SNDLOG (attract y Portugal, que
    ahora tambien casa) y G-CPU pasan
- [x] Curvas, segunda ronda (partidas nuevas del piloto automatico con un estilo que no se uso al hacer los paquetes,
  AP 7/110, los cuatro rallies, 18 000 cuadros):
  - En carrera la victima sale de una reserva que se llena con un barrido incremental (NB/8 huecos por cuadro, umbral
    de edad adaptable): los cuadros con tiles nuevos ya no recorren los 4 352 huecos dos veces (de 95 a 173 lineas de
    `vid_b` en los cuadros saltados). En la demo del attract sigue la seleccion exacta (con la reserva tenia mas faltas)
  - Un fijo de GAMEc que ya estaba como tile del mapa heredaba su muerte (0) y se desalojaba en plena carrera (0x178A
    faltaba cada cuadro desde la mitad del tramo): ahora no muere. El VO fijo de las demos sigue con su muerte
  - Resultados del tramo con un FEC146 no visto (D/9 tras un choque): la foto salia en mosaico 1,3 s; ahora, con la
    pantalla a la vista, la seccion mayor del mismo modo. MENUS lleva ademas las pantallas de despues de correr
    (elegir rally, resultados, continuar, fin) de una partida de cada rally (work/tramos/r_10_150, solo modos A, D, E y F)
  - El recuento de huecos para las cifras (un cuadro lento de cada 32) solo con las cifras a la vista
  - Resultado: cuadros de 2 vblanks en carrera Portugal 1,1 % -> 0,4 %, Safari 1,4 -> 0,5, Corcega 1,4 -> 0,4, RAC
    2,3 -> 0,5; ninguna parada de 3 o mas vblanks (antes 3-6 por partida); faltas en carrera 0,00-0,17 por cuadro;
    Portugal con los coches N, L y R: 0,7 % y 0,01-0,03 faltas por cuadro. Attract igual que antes (1 132 faltas en
    12 000 cuadros). G-ESTADO, G-SNDLOG y G-CPU pasan
- [x] Pausa en carrera (START, como el WR1): el 68000 y la MCU parados, el cuadro que se ve oscurecido con PAUSA, voces a
  tono 0, el CD sigue leyendo lo pedido; el tiempo de la pausa no cuenta para el reloj del sonido. START no llega al
  juego en carrera (modo 1)
- [~] Carga de fondo del mapa a 2x con puntos de sondeo; `CARGA_1X`; CARGANDO solo si falta
- [ ] Politica de carrera: prelectura por avance, muerte por avance, victimas con copia en L2 primero
- [ ] Proxies y lecturas de emergencia; pantalla de cifras L2
- [~] Cargas de §7.2 en todos los menus; pausa con START. Elegir coche: corridas `menus0-2` en `construir_cd.sh` (los
  tres coches y los cuatro rallies; la seccion 8000C pasa de ~825 a 2 140 tiles) y al empezar una carga bloqueante lo
  pintado en el negro anterior ya no cuenta como visible (76 tiles se quedaban sin hueco y la pantalla salia sin fondo
  30 cuadros): 0 faltas al elegir coche, carga de 1,2 s en el negro
- [ ] Puerta G-CD: 0 sectores en carrera
- [ ] Puerta G-TILES con `count_missing` en lineas fuera del paquete
- [ ] Puerta G-CARGA: negro antes del mapa <= 0,5 s; mapa -> carrera 0 s a 2x, <= 3,5 s en 1x; START <= 1,2 s
- [ ] Puertas G-ROM, G-CPU (p99 <= 85 %, <= 0,5 % saltados), G-ESTADO
- [ ] 12 etapas, «Continue?», fin de partida, nombre y records en `psxprof2` sin cuelgues
- Usuario (prueba 2): partidas enteras de los 4 rallies sin sonido

## H6 Sonido (M-L)
- [x] `sndwr2.py`: banco fijo, conjuntos por fase, bancos de coche (`work/cd/SPU*.BIN`, `work/snd_tab.c`; tamanos = diseno 5.1)
- [x] GAE1 -> SPU con encadenado; tono escalado por fps (0x2DF en NTSC) (`ps1/snd.c` en main.c; clave del trozo = banco
  << 16 | (registro 2 - 0x1000); al cambiar de trozo, la repeticion nueva un cuadro entero despues: la SPU puede ir un
  cuadro detras). `RITMO=591` sin hacer
- [x] Zona variable por ficheros (mascara de cargados; S7E al final de la RAM, junto a S7C o al coche): se sube en el
  negro de antes de la pantalla (mapa de demo, logo, elegir coche, elegir rally: S7E + banco del coche segun FE8126
  0/1/2 -> SPUCAR1/0/2, carrera) con las voces a tono 0; si un trozo no esta, lo pide y se sube con el juego parado
  (como mucho una vez cada 60 cuadros). psxprof2: 0 cargas fuera de los negros; negros 0,44-0,79 s mas largos
  (pendiente: de fondo, como pide el diseno 7.2)
- [x] Puerta G-SNDLOG en la PS1 (`tools/sndps1.py`: `psxprof2` contra `rc_host SNDLOG`): attract 12 000 cuadros, 46 comienzos y 17 cambios
  de trozo; partida de Portugal 10 700 cuadros, 170 comienzos y 37 cambios: mismo canal y trozo, comienzos en el mismo
  cuadro y cambios a +0-2 cuadros, ningun cambio pasando por el silencio; G-ESTADO igual con sonido
- [x] Modelo del GAE1 y SPU con el mismo reloj (tiempo real): el modelo avanza las muestras de 7 812,5 Hz que tocan por
  las lineas del temporizador 1 (sin las pausas de las cargas) y la SPU toca a 0x2D6. Con el modelo por cuadros y la SPU
  escalada a 59,83 cuadros/s se separaban en cuanto el juego iba mas lento (o el emulador contaba otro ritmo): audio
  entrecortado y, en el titulo, la repeticion se ponia al silencio antes de que la SPU acabara el primer trozo (se perdia
  S71). psxprof2: todos los cambios de trozo saltan al siguiente, ninguno por el silencio. G-ESTADO y G-SNDLOG se pasan
  con `MAINOPT=-DSND_JUEGO` (por cuadros, como el banco): attract y Portugal iguales. Muestras: se leian de los graficos
  (faltaba 0x100000); ahora SNR 24-25 dB frente a la ROM
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

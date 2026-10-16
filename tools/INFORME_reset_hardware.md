# World Rally (port Neo Geo): reset a BIOS en AES + NeoSD Pro — informe técnico

## Síntoma
En AES real (BIOS original o UniBIOS 4.0) con NeoSD Pro, la intro llega hasta que el logo
"WORLD RALLY Championship" queda completo y ≤0,2 s después la pantalla se va a negro y la BIOS
reinicia. En MAME nunca ocurre.

## Datos medidos (MAME 0.264)
* Watchdog de MAME (`neogeo`) = **0,1352 s** (8 fotogramas). El driver `aes` no lo emula.
* La intro es un guion por contador de fotogramas (`$10F38C`); el reset real coincide con cnt ≈ 0x3B0–0x3D0.
* El primer MB del P-ROM es la ROM del arcade (Gaelco) con 6303 bytes parcheados; mismas direcciones.
* Ningún mecanismo reproducible en MAME: 7 hipótesis (rutas IRQ en P2, evento OKI/Z80 en 0x3BA, primer
  acceso a datos de ROM, RAM de BIOS, sobrecarga de CPU, línea RESET, IRQ nivel 3) refutadas con evidencia:
  todo lo que ocurre en la ventana del reset ya había ocurrido antes sin reset en el hardware.

## Diferencias frente a un cartucho comercial de 2 MB (Kizuna Encounter, funciona en el mismo hardware)
| | Kizuna | Port |
|---|---|---|
| Código / IRQ | todo en P1 (0x000000-0x0FFFFF) | ~50 % del código y todas las IRQ en P2 (0x2xxxxx) |
| Vectores de fallo | BIOS (reinicio limpio) | manejadores propios que acaban en `rts` |
| Tabla de saltos de cabecera | 4×6 bytes (0x122/128/12E/134) | 4×4 bytes: PLAYER_START/COIN_SOUND ejecutan basura / Line-F |
| Escrituras a espacio ROM | ninguna | `move.w #0,$2FFFF0` en 0x226 |
| Watchdog | 1 patada/fotograma desde VBlank | sobre todo desde IRQ de timer a 240 Hz |
| RAM de BIOS 0x10F300+ | 2-3 variables | >1200 direcciones (mapeo 1:1 de la RAM del arcade) |
| Vuelta a BIOS | SYSTEM_RETURN | nunca |

## Builds de prueba (scripts en `tools/builds/`, no se incluyen ROMs)
* `build_ck.py` → *checksum*: la pantalla azul muestra `ROM S1 S2 S3` calculados en vivo
  (esperado **89A1 4A10 9A2A**). Distinto en hardware ⇒ NeoSD no sirve bien el P-ROM.
* `build_p1_trampolines.py` → vectores 10/25/26/27 vía trampolines en P1 (0x1000-0x1017).
* `build_comply.py` → trampolines + tabla de cabecera correcta + sin escritura a 0x2FFFF0 (65 bytes).
* `build_catcher.py` → capturador de excepciones: cualquier vector no usado (2-9, 11-24, 28-63) congela la
  pantalla (rojo/naranja/azul/morado) con VEC, PC, SR, SP, CNT, D0-D7, A0-A7 y pila, y patea el watchdog
  para que no reinicie. Idéntico a v6 hasta que hay un fallo (validado píxel a píxel y ciclo a ciclo).
* Combinación comply + catcher: aplicar ambos (no se solapan).

## Siguiente paso posible
"Caja negra": la única memoria que sobrevive al reinicio de todas las BIOS probadas es la VRAM
0x8700-0x87FF; se puede registrar estado cada fotograma y mostrarlo en el siguiente arranque.

`tools/lab/BRIEF.md` documenta el laboratorio MAME (rutas, recetas de depurador y Lua).
Los scripts tienen rutas absolutas de la sesión como valores por defecto; pasa tus rutas por argumento.

## HALLAZGO EN HARDWARE REAL (capturador, AES + NeoSD Pro)
Dos capturas en la consola del usuario, ambas **VEC 04 ILLEGAL INSTR** en código de P2 y en momentos distintos:
* PC 0x205308, SR 2004, CNT 0002, SP 108FB8 — 0x205308 es el interior de `move.l A3,$10BE4E` (0x205306, 6 bytes).
* PC 0x204248, SR 2000, CNT 0023, SP 108FC8 — 0x204248 es la última palabra (`00FF`) de `andi.l #$FF,D0` (0x204244).
La CPU ejecutó desde la mitad de instrucciones válidas en P2 ⇒ lecturas de instrucciones corruptas en la zona
PORT/P2 (0x200000-0x2FFFFF) en el hardware real (p. ej. `0280`→`0200`, un bit). En MAME nunca ocurre.
Kizuna (que funciona) no ejecuta código en P2; el port ejecuta ahí ~50 % del código y todas sus IRQ.
Siguiente diagnóstico: `build_catcher2.py` (MEM alrededor del PC, relectura x4096 de ventanas P2/P1, sumas P1/P2;
en hex, B se muestra como X y D como Y).

## Capturador v2 en hardware: P2 deja de responder (bus abierto)
Dos capturas más (VEC 04 en PC 0x204108, CNT 0007; VEC 03 address error en PC 0x200DAC, CNT 001D).
* `MEM 7204 7204 7204>7204 7204 7204`: 0x7204 es `moveq #4,d1`, la instrucción siguiente del propio capturador
  (prefetch). Es la firma del **bus abierto**: al leer P2 nadie conduce el bus.
* `BAD1 ...: 51C8>B25A` = opcodes `dbra` / `cmp.w` del bucle de relectura (prefetch) ⇒ confirma bus abierto.
* `RRD ... 0000` = 65536 fallos de 65536 (el contador de 4 cifras desborda).
* `SUM P2 9A2A 9A2A` correcto ~1 s después ⇒ P2 vuelve a responder. `SUM P1` (1F15/1F16) no cuadra
  (esperado 36F7/BD5C): algunas lecturas de P1 también fallaron durante esa ventana.
* El capturador (código en P1) se ejecutó sin problemas mientras P2 no respondía.
Conclusión: NeoSD deja de servir la zona PORT/P2 de forma intermitente; la CPU ejecuta el bus abierto → excepción.
`build_catcher3.py`: experimento cronometrado en el momento del fallo (fases A sin cambios, B sprites apagados,
C reset Z80, D reescritura de 0x2FFFF0, E espera; 32 muestras de ~15 ms por fase; '.' ok, 1-6 = ventanas P2 malas,
P = falla P1, Z = ambas).

## Build "P1 native": el código del port ejecutado desde P1, como el RoboCop de Teo Tormo
Medido en MAME con el mismo método para los cuatro programas (taps de lectura, fotogramas 600-3000):

| | World Rally HANDOVER | Kizuna | World Heroes | RoboCop (Teo Tormo) |
|---|---|---|---|---|
| Instrucciones leídas de P2 por fotograma | 10 800-22 900 (31-70 % del total) | 0 | 0 | 0 |
| Datos leídos de P2 por fotograma | 250-460 palabras | 470 palabras | 0 | 0 |
| Vectores IRQ en P2 | VBlank, timer 240 Hz, nivel 3, line-A | ninguno | ninguno | ninguno |
| Escrituras al registro de banco 0x2FFFF0 | 1 (entrada USER) | 0 | 0 | 0 |

RoboCop cabe en 1 MB (ROM arcade de 256 KB + runtime de 8,8 KB en 0x70000). World Rally no: su P1 es la ROM
arcade completa (1 MB). El espacio se obtiene así: el port pagina a P2 (tabla 0x260F04) 147 páginas de 4 KB de la
ROM arcade (0x19000-0x68FFF, 0x99000-0xCEFFF, 0xDD000-0xE5FFF, 0xEA000, 0xEE000-0xF0FFF) ya convertidas; los
originales en P1 nunca se leen (cobertura: 16 min de atracción + 5 partidas, 0 lecturas en 0x19000-0x68FFF).

`tools/builds/build_p1_native.py` copia el código nativo (0x200000-0x20B4DD, 46 302 bytes) a **0x020000-0x02B4DD**
y reubica: 4 vectores (10/25/26/27), 63 `jsr/jmp` de P1, 213 operandos absolutos dentro del bloque (incluida la
tabla line-A de 61 entradas) y anula la escritura a 0x2FFFF0. Las referencias relativas al PC no cambian porque el
bloque se mueve entero. Las tablas de datos (0x20B4DE en adelante) siguen en P2 y se leen como datos, igual que Kizuna.

Verificación (MAME 0.264, `neogeo` BIOS por defecto): con la copia antigua de P2 sustituida por opcodes ILLEGAL,
0 instrucciones ejecutadas desde P2 en 59 000 fotogramas de atracción y 41 000 con 5 partidas jugadas, 0 excepciones,
y los hashes de pantalla cada 50 fotogramas idénticos a HANDOVER en toda la ejecución (con la escritura a 0x2FFFF0
conservada: `--keep-bank-write`). Al anularla, el juego va una instrucción adelantado y la carrera, que satura la CPU,
se desfasa unas décimas; es el único efecto. La build final conserva la copia de P2 sin usar y anula la escritura.
`tools/builds/pack_formats.py` genera los formatos Darksoft (prom/srom/m1rom/vroma0/crom0 con entrelazado de 2 bytes,
fpga "10") y Backbit (p1/s1/m1/v1/c1/c2 + neogeo.xml con CRC/SHA1), verificados byte a byte contra el RoboCop.

### Comprobación con NeoBuilder v1.06 (la herramienta oficial de conversión a .neo)
NeoBuilder solo corre en Windows (exige el runtime de VC++ 2015 de 32 bits; bajo mono se aborta en
`CheckRuntimeInstalled`). Su ruta de conversión genérica (`-n/-m/-y/-g/-s zip`) se leyó del IL del ejecutable y
está replicada en `tools/builds/neobuilder_generic.py`. La réplica reproduce **byte a byte** el `robocop.neo` de
Teo Tormo a partir de su set Backbit y el `WRally_NeoSD_PRO_AES_HANDOVER_v6.neo` a partir de sus ROMs separadas, y
aplicada al set Backbit de la build P1-native da exactamente el `WRally_neogeo_P1native.neo` entregado (NGH 0x0999,
P 2 MB sin inversión de mitades, V2 = 0, C entrelazado c1/c2).

## Build "P1 native v2": también las tablas fuera de P2
Con el código en P1 la consola seguía reiniciando (3..2..1 de la carrera y final del título). En MAME, simulando
una ausencia de P2 de 0,5 s (las lecturas devuelven la palabra precargada, como en las fotos del capturador), la
build P1-native se iba a excepción: las **tablas** del port (sprites, baldosas, texto, tabla de páginas) seguían
en P2 y con basura producían violación de privilegio / error de dirección, y en hardware un reinicio.

`tools/builds/build_p1_native2.py` mueve además a P1 toda la zona de datos nativa:
0x20B4DE-0x232FFF → 0x099000 y 0x233000-0x261103 (incluida la tabla de páginas 0x260F04) → 0x02B500, ambas en
páginas del arcade paginadas (muertas). En P2 quedan solo los datos de baldosas de las pistas (588 KB, ahora en
0x20C000-0x29EFFF, lejos de 0x2FFFF0), leídos por bucles acotados que escriben en VRAM. Los datos no contienen
punteros (todas las palabras largas "en rango" son atributos de sprite o índices de página), así que se copian tal
cual; se reubican 283 operandos del código (213 al código, 69 a tablas, 1 a la base de páginas).

Verificación: pantalla idéntica a HANDOVER (atracción + 5 partidas, con la escritura de banco conservada), 0
instrucciones desde P2, 0 lecturas de P2 fuera de 0x20C000-0x29EFFF, intro y título sin ninguna lectura de P2.
Con ausencias simuladas de P2 de 0,5-1 s en el final del título y en la carrera: sin excepción ni reinicio; solo
baldosas corruptas mientras dura.

## Homebrews que funcionan en NeoSD Pro (Rick Dangerous DX, Double Dragon, Blue&Red..., World Racer 32K)
Los cuatro: SSP 0x10F300, reset 0xC00402, excepciones a la BIOS o a manejadores en P1, **0 instrucciones desde P2**.
Double Dragon tiene P de 5 MB con bancos (escribe 0x2FFFF0) y lee de P2 solo datos (1,5 M lecturas en 2 min).
World Rally v2 ya cumple el mismo patrón de P2, y en la intro/título no lee P2 en absoluto; si la v2 reinicia,
el bus de P2 no es la causa. Diferencia que queda y no se puede cambiar sin el código fuente: las variables del
arcade (RAM compartida FEC000-FEFFFF) viven en 0x10C000-0x10FFFF, zona de la BIOS, y la pila en 0x10FF80.

## Registrador de caja negra (`tools/builds/build_recorder.py`, sobre la v2)
Cada VBlank escribe en VRAM 0x8700-0x8713 (zona que ninguna BIOS borra al reiniciar): contador de VBlanks desde el
arranque, contador de intro $10F38C, PC/SR/SP interrumpidos, PC de la última IRQ de timer, marca 'JB' si el VBlank
saltó a la BIOS y 'EX'+vector si una excepción llegó a la pantalla de error. En la siguiente entrada USER, si hay
registro válido, lo muestra 14 s en 4 filas de 5 palabras y después arranca el juego normalmente. Verificado en
MAME con reinicio simulado (muestra contador, PC de la intro, SP 0x10FF6A) y sin reinicio (juego igual).

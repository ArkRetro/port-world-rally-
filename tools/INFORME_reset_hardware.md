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

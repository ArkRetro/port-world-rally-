# World Rally (Gaelco 1993) — análisis del programa 68000 para su reubicación en Neo Geo

Directorio de trabajo: `$LAB/work/m68k/`. ROM analizada: `$LAB/wr_cpu.bin` (1 MB, offset = dirección; checksum de palabras = 0E56).
Herramientas: desensamblado recursivo con capstone (`static_dasm.py`), cobertura dinámica con MAME 0.264 + Lua (`cov.lua`, `cov2.lua`, `cov3.lua`),
port a Python del cifrado de VRAM (`gaelcrypt.py`, verificado contra MAME), decodificación de tablas (`stages.py`, `oki_map.py`, `ram_map.py`, `ranges.py`).
Ayuda: `showasm.py INI FIN` imprime `main.asm` entre dos direcciones hex.

Ficheros entregados
* `ranges.csv` — mapa [ini,fin) CODE / DATA / UNKNOWN de la ROM (357 rangos) con bytes ejecutados por rango.
* `absrefs.csv` — inventario de TODOS los operandos absolutos (abs.L/abs.W) + inmediatos de 32 bits que apuntan a hardware/RAM/ROM (5.920 filas):
  `pc, insn_size, operand_offset (offset del operando dentro de la instrucción), address, mnemonic, operands, access (r/w/rw/addr/x/imm), op_size, region, kind, selfcheck`.
  `access=addr` = `lea`/`pea` (no accede, carga dirección); `x` = jsr/jmp; `imm` = `#$dirección` inmediato de 32 bits. El selfcheck comprueba que los bytes en `pc+operand_offset` contienen la dirección (0 fallos).
* `basereg.csv` — cada sitio donde se carga una base (lea/movea #/adda #) de ROM/RAM/hardware en un An, con los usos `(An)`, `(An)+`, `d(An)`, `d(An,Xn)` que siguen linealmente hasta que An se redefine o acaba la rutina.
* `romdata_refs.csv` — subconjunto de absrefs: referencias a ROM como DATOS (no jsr/jmp): 543 filas, 486 con dirección ≥ 0x15000.
* `calls.csv` (733 jsr/bsr resueltos, 205 funciones), `indirect.csv` (10 jmp/jsr indirectos), `code_insns.txt` (14.315 instrucciones alcanzadas).
* `stages.csv` / `oracle_sites.csv` — tabla de etapas descifrada y los 20 sitios `move.l $ROM.l,$VRAM.l` que usan la VRAM como oráculo de descifrado.
* `oki_map.csv` — mapa completo comando de sonido → (máscara de canales, prioridad, volumen, secuencia de frases (banco, frase OKI)).
* `ram_ranges.csv` — uso de la RAM compartida (dinámico, fases atracción+partida, + nº de referencias estáticas).
* `cov_*.txt`, `cov2_*.txt`, `cov3_*.txt` — salidas crudas de MAME (PCs ejecutados con máscara de fase 1=self-test 2=atracción 4=partida; offsets de ROM leídos como dato; PCs que escriben/leen VRAM/paleta/sprites/vregs; log de E/S; RAM tocada).
  `snaps/wrally/*.png` capturas (0003 = partida). `vram_*.bin`, `ram_*.bin`, `spr_*.bin` volcados (VRAM ya descifrada) en los fotogramas 900 y 3500.

---------------------------------------------------------------------------------------------------------------------------

## 0. Correcciones al brief

* **IRQ6 (VBlank) NO está en 0x56A.** 0x56A es el vector 6 (CHK). El vector 30 (IRQ6, offset 0x78) apunta a **0x2330: `bra $2f8c`** → el manejador real es **0x2F8C**. Los vectores 2..8 (0x536..0x582) son manejadores de excepción que guardan el nº de excepción en `$FEC04C`, mandan 0xA2/0x10 al OKI (silencio) y guardan SR/PC en `$FEC046/48`. Los vectores 9..11 y 24..47 saltan a 0x2242 (bucle/stop, salvo trace=0x21CA) y los 12..23/48..63 a 0x400 (basura, tabla de excepción no usada).
* **El 68k NO cifra nada por software.** Todo lo que se escribe en VRAM son parejas de palabras ya cifradas en ROM (mapas de baldosas, bloques de imagen, fuentes, punteros) escritas siempre con `move.l` (32 bits). 46 sitios de escritura dinámicos → 46/46 son `move.l` (ver §3a). Además el programa **lee la VRAM para descifrar datos** (punteros de código y de datos, tabla de récords, cabeceras de bloques) y para leer los bits de colisión/superficie del mapa. Para el port hace falta un `decrypt()` software (está en `gaelcrypt.py`, 1:1 con MAME) y una **VRAM sombra descifrada en RAM** de 16 KB.
* La "ROM checksum 0E56" sólo se **muestra**, no se compara (§3i).

## 1. Mapa código/datos de la ROM

Método: (a) desensamblado recursivo desde los 64 vectores + todos los PCs ejecutados en MAME (semillas), siguiendo bra/bcc/bsr/jsr/jmp absolutos;
(b) MAME 117 s emulados con tap de lectura sobre 0-0xFFFFF registrando `CURPC` (= instrucción en ejecución → conjunto exacto de instrucciones ejecutadas) y, para lecturas con `offset-CURPC ∉ [0,12]` y PC ≠ 0x2714 (bucle de checksum), el offset como DATO. Fases: 0-6 s self-test, 6-50 s intro/título/demo, 50 s moneda+start, partida (se eligió una etapa en "Choose a Rally") hasta 117 s.

Resultado global (ranges.csv):

| Clase | Bytes | % ROM |
|---|---|---|
| CODE (estático ∪ dinámico) | 68.234 | 6,5 % |
| de los cuales EJECUTADOS en MAME | 32.332 | 47,4 % del código (7.064 de 14.315 instrucciones) |
| DATA leído en la ejecución | 277.452 | 26,5 % |
| UNKNOWN (ni ejecutado ni leído) | 702.890 | 67,0 % — casi todo mapas/metabaldosas de etapas 2-12 (§5) |

Resumen grueso (ver ranges.csv para los 357 rangos finos):

| Rango | Clase | Contenido |
|---|---|---|
| 000000-000100 | DATA | vectores (SSP=FEFF80, PC=2400, IRQ6 → 2330) |
| 000100-000400 | UNKNOWN | relleno (0xFF/0x00) — libre para la cabecera Neo Geo |
| 000400-000480 | CODE (no ejecutado) | manejador de vectores basura 12-23/48-63 (jmp 2000, OKI 0x78) |
| 000480-000536 | UNKNOWN | datos del monitor |
| 000536-0005AE | CODE (no ejec.) | manejadores de excepción 2-8 |
| 0005AE-002000 | UNKNOWN/CODE muerto | **monitor de depuración** (0x5B4..: usa un puerto serie en 0x600000/0x600001 y 0x500000; nunca ejecutado) |
| 002000-002244 | CODE (no ejec.) | resto del monitor (jmp (a2) 0x2082/209E/215C/21C8) |
| 002300-00239C | CODE | trampolines de vectores; **0x2330 IRQ6 → 0x2F8C** |
| 002400-0057E6 | CODE 51 % ejec. | arranque 0x2400, self-test, bucle principal 0x31D2, IRQ 0x2F8C, sprites, scroll, entradas |
| 0057EC-00B1B2 | CODE 46-66 % | lógica de juego (coche, IA, colisiones), copia de mapa a VRAM (0x98C8-0x9DF0), animación de baldosas (0x9DF4) |
| 00B20A-00B24A | CODE | — |
| 00B25E-00DC0A | CODE 41 % | init 0xB25E, paleta/fundidos, menús, HUD |
| 00DC18-0102BC | CODE 37 % | pantallas (intro, "Choose a rally", récords, fin) |
| 0102F2-011AAC | CODE 3 % | pantallas poco frecuentes (continue/ranking/final) |
| 011AE2-01290C | CODE 65-92 % | texto a VRAM (0x11CF4/0x11D62), borrado de capas (0x11FDC/0x11FF2), impresión de cadenas (0x12172), paleta |
| 01290C-014000 | UNKNOWN | datos (tablas de texto/paleta no leídas) |
| 014000-014444 | CODE 95 % | **driver de sonido OKI** |
| 014444-014F02 | DATA | tablas del driver de sonido (frases 0x144AC, scripts 0x14524-0x14DCA); 0x14DCA-0x14DDE código (mute) |
| 014F02-01500A | UNKNOWN | — |
| 01500A-01900A | DATA | mapas de baldosas etapa 1 (2 capas × 8192 B) |
| 019014-06D61E | DATA | metabaldosas capa 1 (fondo) — 345.610 B, cifrados |
| 06D61E-09968C | DATA/UNKNOWN | mapas etapas 2-12 (11 × 2 × ~8192 B) |
| 09968C-0DF508 | DATA/UNKNOWN | metabaldosas capa 0 (primer plano) — 286.332 B, cifrados |
| 0DF508-0DFBBC | DATA | bloques de punteros de etapa (16 B, cifrados); 0xDF64C/0xDF650 pareja "baldosa vacía" cifrada para cada capa |
| 0DFBBC-0DFC7C | DATA | **tabla de etapas** (12 × 16 B) |
| 0D07D4-0DD448 | DATA | **objetos de sprites** (composición multi-sprite: cabecera 3 B en 0xD07D4, índice en 0xD07D6; lectores 0x45BE-0x4A66, puntero `fed5fc`) |
| 0DDC3E-0DE83C | DATA | paletas (0xDDC3E paleta 0, 0xDE25E-0xDE83C paletas de juego, 0xDE09E…) |
| 0DFC7C-0F0A20 | DATA | tablas: paletas (0xDDC3E, 0xDF5CC, 0xDF954, 0xE53EA…), textos (0xED3BB-0xEE10A), bloques de imagen 0xE5674 (capa 1) y 0xEA1BE (capa 0), listas de sonido 0xE1DF0/0xE1E88, tablas de coinage 0xE20CC/0xE20EC, animación de baldosas 0xF01FC/0xF05FC, récords por defecto 0xEFEF2 (72 longs cifrados), 0xF0A04 (18 B calibración A/D) |
| 0F0A20-100000 | UNKNOWN | relleno |

Ningún código ejecutado ni alcanzable estáticamente está por encima de 0x14DDE: **todo el código vive en 0x400-0x14DDE (≤ 84 KB)**; todo lo demás es dato.

## 2. Inventario de referencias absolutas (absrefs.csv / basereg.csv)

Recuento por región y modo (sólo instrucciones en rangos CODE):

| Región | w | r | rw | lea/pea (addr) | #imm32 | jsr/jmp |
|---|---|---|---|---|---|---|
| VRAM 100000-103FFF | 40 | 53 | – | 5 | 11 | – |
| VREGS 108000-10800F | 9 | 0 | – | – | – | – |
| PALETA 200000-203FFF | 18 | 1 (`B84A` vía lea) | – | 220 | 1 | – |
| SPRITES 440000-440FFF | 1 | 0 | – | 2 | – | – |
| E/S 700000-70007F | 42 | 15 | – | 2 | – | – |
| RAM FEC000-FEFFFF | 1205 | 2210 | 881 | 458 | 8 | – |
| ROM datos 000000-0FFFFF | – | 79 | – | 405 | 59 | (192 jsr/jmp absolutos, no son datos) |
| OTRAS | `clr.b $500000` (0x42A) y `lea $600000/$600001` (0x21FA-0x2206): monitor de depuración, código nunca ejecutado |

Accesos indirectos a hardware (basereg.csv, 1.106 cargas de base; las que apuntan a hardware son pocas y están todas aquí):

| PC | Carga | Usos |
|---|---|---|
| 0024AE | `lea $200000,a1` | 0024BC `move.w (a0)+,(a1)+` ×16 (paleta 0 inicial) |
| 002744/002752 | `lea $fec000,a0` / `lea $200000,a0` | test de RAM 0x2878 (`move.w (a0)`, `cmpi (a0)`) 0x1FFF palabras cada uno |
| 0044E6 | `lea $440006,a1` | 0044FE/4500/4518/451A/4538/453A `move.l (a0)+,(a1)+` y 0x456A `move.w #$f0,(a1)` (lista de sprites) |
| 005AE6/005AEC | `lea $70006b,a0` / `lea $70007b,a1` | `st (a0)`, `clr.b (a0)`, `st (a1)`, `clr.b (a1)` (bit-banging del A/D: ENA/D, CKA/D) |
| 008950/008956 | `lea $200000,a2` / `lea $200800,a3` | 00899C/0089AA `move.w d0,(a2)+/(a3)+` ×769 (fundido) |
| 008A60 | `lea $203000,a2` | 008AA0 `move.w d0,(a2)+` (no ejecutado en MAME) |
| 0098E0/00997C | `adda.l #$102000,a1` | 009916/0099B2 `move.l (a0),(a1)` (capa 1 ← metabaldosas) |
| 009C1E/009CBA | `adda.l #$100000,a1` | 009C4E/009CEA `move.l (a0),(a1)` (capa 0) |
| 009E00 | `movea.l #$100000,a0` | 009E0E/009E38 `move.l (a0,d0.w),dn` (lee VRAM descifrada), 009E56/009E76 `move.l (a1,d1.w),(a0,d0.w)` (animación) |
| 009F34/009F3A | `movea.l #$102000,a0` / `#$100000,a3` | 009F5C/009F62 `move.l (a0),d0` (lectura de atributos bajo el coche) |
| 00B25E | `lea $440000,a0` | 00B268 `move.w #$ffff,(a0)+` ×2048 (borra sprites) |
| 00B32A | `lea $101100,a2` | 00B334 `move.l (a0)+,(a2)` / 00B336 `move.l (a2)+,(a1)+` ×72 (oráculo: descifra tabla 0xEFEF2 → fef252) |
| 00B352 | `lea $1021bc,a0` | 00B358 `move.l (a0),$fef506` |
| 00B83A | `lea $200000,a0` | 00B84A `move.w (a0)+,(a1)+` ×192 (copia paleta → RAM) |
| 00E798 | `lea $1020d8,a0` | 00E79E/E7AC `move.l #1/#2,(a0)`, 00E7A4 `move.l (a0),d0` (test de cifrado: escribe 1 y 2 y lee) |
| 011FE0/011FF6 | `lea $102000,a0` / `lea $100000,a0` | 011FE6/011FFC `move.l $df64c/$df650,(a0)+` ×2048 (borrado de capa con baldosa vacía cifrada) |
| 0C172 | `lea $200020,a0` | `move.l #$aaa0aaa,d(a0)` (paleta 1 gris) |
| ~210 `lea $2000xx..$200bc0,aN` | paleta | todos seguidos de bucles `move.w (a0)+,(a1)+` de 16/32/64 palabras desde tablas ROM (ver basereg.csv) |

Las 1.106 bases de RAM/ROM (`lea $fecXXX,aN`, `lea $0eXXXX,aN`) son los sitios a parchear si la RAM/los datos cambian de sitio; en basereg.csv está cada uno con sus usos.

Bucles `movem`: sólo hay 3 (`movem.l $fec00a` en manejadores de excepción/monitor). No hay copias `movem` hacia VRAM.

## 3. Rutinas de hardware

### 3a. Escritura a VRAM (todas `move.l` de 32 bits = pareja de palabras cifrada)

| PC | Instrucción | Rango VRAM (dinámico) | Qué hace |
|---|---|---|---|
| 009916 / 0099B2 | `move.l (a0),(a1)` | 102000-103FFE | **copia de mapa capa 1** (fondo). Rutina 0x98C8 (columna) / 0x9976 (fila): recorre la zona visible y copia parejas (código,atributo) desde las metabaldosas en ROM. Saltan la copia si `fec224&0xF0==0x90`. |
| 009C4E / 009CEA | `move.l (a0),(a1)` | 100000-101FFE | ídem **capa 0** (primer plano) con fec194/fec18c |
| 009E56 / 009E76 | `move.l (a1,d1.w),(a0,d0.w)` | 100BB4-100FAA | **animación de baldosas** (0x9DF4): lee la VRAM descifrada, si el código ∈ clase 0x0E00/0x1080 sustituye por la pareja de las tablas ROM 0xF01FC / 0xF05FC |
| 011D34 / 011D9E | `move.l (a2,d1.w),(a4)+` | 102004-103FFE / 100014-101072 | **bloques de imagen/texto**: 0x11CF4 (capa 1, tabla 0xE5674) y 0x11D62 (capa 0, tabla 0xEA1BE). D0=nº de bloque, D4=posición. La cabecera del bloque (ancho, alto, offset) está cifrada: se escribe en 1030C0/1010A0 y se relee (0x11D02/0x11D0A, 0x11D70/0x11D78). Datos del bloque en `feec3c`/`feec48` = tabla + (cabecera de 3 bytes 0xE5672/0xEA1BC)<<2 |
| 011FE6 / 011FFC | `move.l $df64c/$df650,(a0)+` | 102000-103FFE / 100000-101FFE | **borrado de capa** 0x11FDC / 0x11FF2 con la pareja "baldosa vacía" cifrada |
| 00B334 | `move.l (a0)+,(a2)` + `move.l (a2)+,(a1)+` | 101100-10121E | descifra 72 longs de 0xEFEF2 → `fef252` (tabla de récords por defecto, texto) |
| 00E03E / 00E1F0 | `move.l (a0,d0.w),$1034c8/$1024d0` | 1 pareja | HUD |
| 0031DC, 003282, 0032A4, 0036BC, 00370A, 003738, 003DFE, 003E0A, 003E24, 003E74, 008A30, 008C40, 008C76, 008F12, 008FD2, 008FE6, 009072, 009086, 0090D8, 00B286, 00B304, 00B474, 00B8C8, 00BD2E, 00BE04, 00BEB4, 00C0C2, 00C33C, 00C346, 00E496, 00E79E/E7AC, 00FD76 | `move.l $ROM.l,$VRAM.l` / `move.l (a0)+,$VRAM.l` | 1 pareja cada uno | **oráculo de descifrado** (§3a-bis) |

**Cifrado**: el ROM guarda los datos ya cifrados; MAME descifra en `vram_w` (gaelcrpt, params 0x1F/0x522A) usando el PC para saber si la 2.ª palabra viene del mismo opcode. En el port: `dec_hi = decrypt(0,0,hi); dec_lo = decrypt(hi,dec_hi,lo)` (`gaelcrypt.py::decrypt_pair`, verificado: 0xF09FC→00FEE9B8, 0xEEC7C→67CE546B, 0xE4260→00000004 coinciden con MAME). Alternativa para el port: **pre-descifrar los datos** (metabaldosas, bloques, baldosa vacía, animaciones) en la ROM del cartucho y suprimir el descifrado en las rutinas de copia; los 20 sitios oráculo (`oracle_sites.csv`) se sustituyen por `move.l #valor_descifrado`.

Sitios que **leen** VRAM (53 estáticos; dinámicos en cov2_vramr.txt): oráculo (0x320E, 0x328C, 0x32AE, 0x3712, 0x3740, 0x376E, 0x3E04, 0x3E10, 0x3E2A, 0x3E7A, 0x8A3A, 0x8C48, 0x8C7E, 0x8F6A, 0x8FDC, 0x907A, 0x934C, 0x9382, 0x9490, 0xA444, 0xB242, 0xB290, 0xB336, 0xB358, 0xB4D8, 0xB8CE, 0xBD38, 0xBE2A, 0xBEBC, 0xC13E, 0xC360, 0xC36A, 0xE046, 0xE1F8, 0xE3AA, 0xE4A0, 0xE7A4, 0xFD80, 0x11D0A, 0x11D78) y **lectura de atributos del mapa** (bits 8-12 = colisión/superficie): 0x9E0E/0x9E38 (100000-101FFE), 0x9F5C/0x9F62 (102038-103F82 / 100038-101F82, rutina 0x9F34: 4×7 baldosas alrededor del coche → `feeb5c`), 0x6C76 (101700), 0x7518 (103AD0), 0x7A26 (103BE0), 0x7EF2 (101E00), 0x832A (103ABC), 0x88B8 (1000DC), 0x604C (103E00: comprueba `&0xFF00FD52`, protección), 0xB242 (103F00).

Punteros de **código** descifrados vía VRAM (hay que mantenerlos o re-apuntarlos): `fef828` ← 0x8F6A (ROM 0xDF504 → **0x9EB6**, `jsr (a0)` en 0x33E0), `fef836` ← 0xBD38 (0xE4506 → **0xA32A**, `jsr` en 0x340A), `jmp (a6)` 0xFD86 (0xDF5C8 → **0x3224**), 0x8FD2/0x8FE6 (0xEA1B0 → 0x8FE6, 0xDF3FE → 0x9FE2), 0x9086 (0xDF654 → 0x4D4E), 0x90D8 (0xDF850 → 0x6476), 0xECE2 (0xEDDBA → 0xA66D), 0xC33C/C346 (→ punteros RAM 0xFEC15C/0xFEC156), 0xB286 (→ 0xFEE9B8, `fef54a` lista de sprites).

### 3b. Paleta (fundidos)

* `0x891E` (llamada desde 0x89E4 cada vuelta del bucle principal y desde el IRQ vía 0x4360?): cada 3 fotogramas (`fec16a`) recorre la **paleta sombra `fef87e..fefe80` (769 palabras)** restando 1 a cada nibble R/G/B hasta 0 y escribe el resultado en **200000-200600** (0x899C) y una copia en **200800-200E00** (0x89AA, sólo si `fef860 ≥ 3`). Estado en `fec166` (bit0 = fundido en curso), fin → `fec16a=0`.
* `0x8A5A`: variante que escribe en 203000+ (no ejecutada).
* Cargas directas de paletas desde ROM (210 sitios `lea $2000xx,a1; lea $ROM,a0; move.w (a0)+,(a1)+`×16, p. ej. 0x24AE/0x26A2/0xB6D8…0xC7C6, 0xDA90 (768 palabras), 0x11F00 (200000-2003FE), 0x11FCE, 0x12150/0x12158 (entradas sueltas), 0x397E (paleta 0x49 cíclica, 36.768 escrituras: 0x3964 rota 8 paletas de 0xDF954 cada N frames → ciclo de colores), 0xBA9A (200400-20043E, tabla 0xE53EA)). Casi todas duplican en la sombra RAM `fef87e+` (A2 = `fef87e + offset`) para que el fundido pueda partir del valor actual.
* Rango de paleta realmente escrito: **200000-200E00** (+ 200120.., 203000 por código no ejecutado). Formato xBRG_444; entradas 0x200-0x23F (sprites) etc. Test de RAM de paleta en el self-test (0x2752, 0x1FFF palabras).

### 3c. Lista de sprites (440000)

`0x44E6` (desde IRQ6): `lea $440006,a1`; copia con `move.l (a0)+,(a1)+` ×2 por sprite (8 bytes = 4 palabras: y|flip, ?, x|color, código) tres listas construidas en RAM por la lógica del juego: **`fed600`** (nº en `fec27e`), **`feddf8`** (`fec280`), **`fee5c8`** (`fec282+fec284`); luego rellena hasta el nº de sprites del fotograma anterior (`fec27c`) con y=0xF0 (`move.w #$f0,(a1); addq.l #8,a1`). Máximo observado: 440006-44085E (≈ 267 sprites). `0xB25E` borra 440000-440FFE con 0xFFFF y 0xB402 escribe `move.l #$ffffff,$440000`. Las listas de RAM las rellenan las rutinas de objetos (coche 0x457C, coches rivales, decorado…). Lista `fef54a` = 0xFEE9B8 (puntero descifrado) con 22 entradas de 6 bytes iniciadas a 0x8000 (0xB3A6).

### 3d. Entradas

| PC | Acceso | Uso |
|---|---|---|
| 00240C / 00243A | `move.b $700000/$700001` | DSW al arrancar: cada byte se **invierte bit a bit** y se guarda en `fec108` (DSW alto) / `fec109` (DSW bajo). 0x366E los decodifica: fec109&0xC0 → `feebde` (dificultad: 3C/00/28/14), fec109 bit0 → `fec151` bit0 (service mode), bits 3-4 → `fec162` (créditos), fec108 bits 5-7 → índice en 0xE20CC (coin A, cifrado → `fec1e2/fec1ea`), bits 2-4 → 0xE20EC (coin B → `fec1e4/fec1ec`), bit1 → `fef810`, bit0 → `fef812`, fec109 bit5 → `fef814`, bit2 → `fef816`, bit1 → `fef818` |
| 00520C / 005212 | `move.b $700003`, `move.b $700002` | **cada VBlank** (bsr 0x520C desde 0x2FF2): remapea los bits de P1/P2/monedas/start a `fec1fc` (P1), `fec1fd` (P2), flancos en `fec1f6`… (114 referencias a `fec1fc` en el código: es "el" byte de mando) |
| 0042D2 | `btst #0,$700009` | service coin (0x42C6..) → `fec1ee` créditos, sonido 0x01 |
| 00319E | `btst #1,$700009` | **test switch** al final del IRQ: si está activo reinicia (SSP=FEFF80, jsr 0xFDD0, 0xB25E, bra 0x246C) |
| 005B02 | `btst #2,$700009` | bit de datos del **A/D** |
| 00564E / 005ADC | `move.b $700004,$fec1fa` | volante óptico (0x564E en modo volante, 0x5ADC en modo A/D) |
| 005AE6-005B1A | `lea $70006b/$70007b` | **A/D serie**: ENA/D=1, CKA/D=0, ENA/D=0, 8× (desplaza, lee bit2 de 700009, CKA/D=1, CKA/D=0), ENA/D=1 → `fec1f8` (histéresis de 2, calibración `fef868/fef869`… copiada de ROM 0xF0A04 en 0xB4EC) |
| 70006A/70007A | lecturas nop en MAME; no aparecen en el código (los `st`/`clr.b` son escrituras a 7000xB) |

### 3e. Driver de sonido OKI M6295 (0x14000-0x14444, tablas 0x14444-0x14DCA)

* **Init 0x14000**: borra `feebe8` (comando inmediato), `feebe9` (contador mute), cola `feebea`(cabeza)/`feebeb`(cola)/`feebec` (ring de 16 bytes), 4 estructuras de canal de 16 bytes en **`feebfc`** (+0 timer, +2 ptr script, +6 contador de bucle, +7 flags: 0x80 activo, bit0 temporizado, bit1 frase simple, bit2 ya lanzada, +8 prioridad, +9 comando, +A volumen, +B frase, +C banco), escribe **0x78 → 70000F** (stop de los 4 canales).
* **Encolar**: `0x14076` (D0 = comando 0x00-0x7F; comprueba lista de exclusión 0xE1DF0 contra el último comando `feebe0`) y `0x140A8` (comando ≥ 0x80, listas 0xE1E88 / `feebe2`; 0xAC incrementa `fef53a`). Escriben en `feebec[cabeza]`, cabeza=(cabeza+1)&15. `0x14108`: comando diferido `feebe4/feebe6`. Comando inmediato: byte en `feebe8` (lo usan 0x42F8 (0x01 moneda), 0x312A (0x0D), 0x3174 (0x10)…).
* **Tick 0x14122** (desde IRQ6 en 0x2FFE): lee estado OKI (70000F); si algún canal suena → `0x14DCA` (**70004A ← 0, mute OFF**) y `feebe9=100`; si no, cuenta atrás y al llegar a 0 `0x14DD4` (**70004A ← 1, mute ON**). Saca un comando (inmediato o de la cola) → `0x141D4` y luego `0x142E2` (secuenciador).
* **0x141D4** (lanzar comando): comando ≥ 0xF0 → `0x14054` (para todos los canales). Comando < 0x80 → entrada de 4 bytes en **0x14C02 / 0x14C9A / 0x14D32** (según `fec152&3` = 2 / 0 / otro) = [máscara de canales, prioridad, volumen, frase]. Comando 0x80-0xB2 → puntero en **0x14524 / 0x145F0 / 0x146BC** (`fec152&3`) a un script [máscara, prioridad, volumen, bytes…]. Elige canal: el primer canal libre de la máscara, o el de menor prioridad si todos ocupados (si prioridad nueva < ocupada → descarta); escribe **stop (0x08<<canal) en 70000F** y espera a que el estado lo confirme.
* **0x142E2** (secuenciador, cada frame y canal activo): si temporizado y timer>0 decrementa; lee el script: byte < 0xF0 = índice en la **tabla de frases 0x144AC** (60 entradas × 2 bytes = (banco, frase OKI)); si el OKI dice que el canal sigue sonando → stop (`(canal)<<3` en 70000F); **banco → 70000D** (si ≠ 0; 0x14386), luego `0x80|frase` → 70000F y `(1<<canal)<<4 | volumen` → 70000F (0x14394/0x143A8). Opcodes (tabla 0x143EE): F0 xx ignora; F1 pp prioridad; F2 vv volumen; F3 nn dd dd repite nn veces saltando dd (nn=0 → bucle infinito, usado en casi todos los scripts como "loop"); F4 dd dd salto; F5 xx tt tt espera tt frames; F6 modo temporizado (cada frase lleva 2 bytes de duración); F7 lo quita; F8-FE nop; FF fin (libera canal).
* **oki_map.csv** tiene los 38+51 comandos de los 3 conjuntos. Conjunto activo en MAME: `fec152&3==2`. Ejemplos (conjunto 2): 00 = ph01 (banco 0, frase 2D, vol 4, canales 0-2); 02/03 motor (banco 6, frases 5F/60); 0A = (b3,f52); 14-16 = (b4,f57-59); 80/81 scripts de motor en bucle; A9 = (b9,f6F) ×4 loop; comandos 0x9D (b11,f73) etc. Las frases "(b0,f00)" son entradas vacías (frase 0 = cabecera del OKI, no suena). Observado en el log de E/S: bancos usados 3,4,6,7,B,C,D,E,F; 0x14386 cambia banco justo antes de lanzar la frase (no hay reproducción simultánea de bancos distintos salvo las frases fijas 0x00-0x2F que no dependen del banco).
* Otros sitios OKI: excepciones 0x402/0x430/0x438/0x4B0/0x516/0x51E/0x58A/0x592, monitor 0x7D2-0x7EC, 0x221E-0x2238 (0xA2/0x10, 0x9A/0x9B — parar canales), 0x1402C (0x78).

### 3f. Manejador IRQ6 (VBlank) — 0x2F8C (vector 30 → 0x2330)

1. `move.w #0,$10800c` (CLR INT). `movem.l d0-d7/a0-a6,-(a7)`.
2. Contadores: `fec1c2` (fase &0xF==3 → ++), `fec244 += fec3`/`-=` según `fec1b4` bit6 (desplazamiento del mapa).
3. **`move.w $fef824,$70005a`** — latch bit5 (flip) cada frame.
4. Si `fef251` bit0 == 0: `bsr 0x43E2`. Luego **0x4440 (vregs scroll)**, **0x44E6 (sprites → 440000)**, **0x520C (lee 700002/3)**, 0x40A8, 0x4360, **jsr 0x14122 (tick OKI)**, 0x8732, jsr 0xAFAE.
5. Si `fec1c9` bit0: 0x49CA, copia `fef4da→fec1aa`, `fec286→fec282`. 0x457C (coche jugador→sprites), 0x46E4, 0x488E. `fec1a6++` (**contador de frames** usado para sincronizar el bucle principal: 0x2770 espera a que cambie).
6. Lógica de cronómetro/velocidad (`fec1aa` ÷6, `fec1b2`, `fec1ba`, llamadas 0x12768 (fin de tiempo), sonidos 0x0D/0x10 en `feebe8`).
7. `bsr 0x5E16`; si `feead4==1` → `jmp 0xE68A` (no vuelve: pantalla de fin). **`btst #1,$700009`** (test switch) → reinicio. `movem.l (a7)+`; `rte`.
Interrupciones se habilitan en 0x246C (`move #$2000,SR`); el self-test corre con `#$2700`.

### 3g. Vregs de scroll — 0x4440 (desde IRQ)

Normal (`fef824==0`): `108000 ← -fec140 - 0x0D` (scrolly capa 0 ← y), `108002 ← -fec13e + 0x42` (scrollx capa 0), `108004 ← -fec144 - 0x0D`, `108006 ← -fec142 + 0x48`. Flip (`fef824≠0`): `108000 ← fec140 + 0x0D - 0x106`, `108002 ← fec13e - 0x42 - 0x186`, `108004 ← fec144 + 0x0D - 0x106`, `108006 ← fec142 - 0x48 - 0x17E`. Variables de cámara: **`fec13e/fec140`** (x/y capa 0, mundo) y **`fec142/fec144`** (capa 1). Valores típicos en carrera: 108002 ≈ 0x0042, 108006 ≈ 0x0046 (horizontal fijo), 108000/108004 = y de la pista; en la intro 108000=0xFFF3, 108002=0x8045 (bit 15 ignorado por el hardware). 10800C sólo se escribe en 0x2F8C.

### 3h. Latch 74LS259 (70000B + n·0x10, bit0 del dato; el código escribe palabras en 7000nA = byte alto 0, byte bajo = dato)

| n | Función (wrally.cpp) | Sitios |
|---|---|---|
| 0 (70000A) | coin lockout 1 | 0 en B2A2 (init); 1 en 31E6, F27C (bloquea al entrar en modo "partida"?); también 0x4182/0x4292/0x4332 escriben 0 a 70001A; 11 refs |
| 1 (70001A) | coin lockout 2 | B2AA (0), 2C08/31EE/D8D4/D95E/F294/F46C/10632 (1), 4182/4292/4332 (0) |
| 2 (70002A) | coin counter 1 | 0x4394 `move.w d0,$70002a` (pulso 1/0 por moneda) |
| 3 (70003A) | coin counter 2 | 0x43D4 |
| 4 (70004A) | mute | 0x14DCA (0 = suena), 0x14DD4 (1 = mute tras 100 frames de silencio) |
| 5 (70005A) | flip | 0x2FD0 cada VBlank ← `fef824` (de DSW "cabinet"? lo fija 0x375A/… según fec108) |
| 6 (70006B) | ENA/D | 0x5AF4/0x5AFC/0x5B18 (`st`/`clr.b (a0)`) |
| 7 (70007B) | CKA/D | 0x5AF6/0x5B10/0x5B12 |

### 3i. Arranque 0x2400 y self-test

```
2400 movea.l #$feff80,a7 ; jsr $b25e (init de hardware/RAM: borra 440000 ×2048, borra fec100-fedfff, descifra punteros, latches 70000A/1A=0,
                           tabla de récords 0xEFEF2→fef252 vía VRAM 101100, 0xEF9AA→fef558 (0x144 palabras), punteros de bloques, calibración A/D 0xF0A04→fef866)
240C/243A lee DSW (700000/700001) → fec108/fec109 (bits invertidos) ; bsr $366e (decodifica DSW) ; move #$2000,SR
2470 si fec151 bit0 (service mode) → 31CE (bucle principal, salta el test)
247C bsr $249c  : SELF-TEST → 2704-271E suma de palabras de toda la ROM (16×0x8000 palabras, add.w (a0)+,d1) = 0x0E56, a texto (0xFD88) en fef366
                  2732 espera un VBlank (fec1a6) ; 273C SR=#$2700 ; 2744 test de RAM fec000-feffff (0x2878: AAAA/5555, A6 = 1.ª dirección mala)
                  2752 test de RAM de paleta 200000-203FFF ; 2760 SR=#$2000
                  277E-281A imprime "Version 1-0", "RAM OK"/"RAM Error at XXXXXXXX", "ROM Checksum 0E56", "Coprocessor OK" si fef500==0xA53E
                  (lo escribe el DS5002FP) o "Coprocessor Not Ready" (textos 0xED858/ED86C/ED87E/ED896/ED8AE/ED8C8, rutina de impresión 0x12172, volcado 0x12530)
                  2826-2876 espera 0x1F4 frames (500 ≈ 8 s) si todo OK, o indefinidamente si hay error / coprocesador no listo (sale pulsando 1P+2P: fec1fc&0xDF==0xDF)
2480 bsr $289e ; 2484 espera a fec1fc & 0x50 ; 2498 bra $31d2 (bucle principal)
```
* **Segunda suma de control (anti-manipulación) en 0xDA42-0xDA56**: al entrar en la intro suma como longs (`move.l #$2d61f,d1; add.l (a4)+,d0; dbra d1` — `dbra` sólo usa D1.w = 0xD61F → 0xD620 longs) los bytes **0x1900A-0x4E88A** (219.264 B = el principio del conjunto de metabaldosas de la capa 1) y guarda el resultado en `fef80c`; se compara con la constante **0xF0A00 = 0xFF0BB639** en 0x403C, 0x7560, 0x7BBA, 0x8B24, 0xAFCC; si no coincide (y fef80c≠0) activa flags que degradan el juego (`fef4b4|=0x40`, `fef4f4|=0x100`, `fec1f2|=0x100`, `fec1f0` bit9, `fef54e|=0x4000`). Verificado en Python: la suma de la ROM original es exactamente 0xFF0BB639. **Si se pre-descifran o mueven las metabaldosas hay que parchear 0xDA44-0xDA56 (o la constante 0xF0A00).**
* **El checksum de ROM de 16 bits (0x2704) sólo se muestra**: no hay comparación. Al parchear/reubicar sólo cambia el texto mostrado (recalcular con `sum16(words)`), o parchear 0x2704-0x271E.
* "Coprocessor OK" depende de `fef500 == 0xA53E` (lo pone el MCU; 0x2812 lo sobrescribe con 1 tras mostrarlo). El bucle principal también espera al MCU en **0x33F0 `tst.w $fec100; beq`** (handshake MCU) y lee `fec1fc` (el MCU participa en las entradas/lógica). Para el port hay que emular estos handshakes (ver el análisis del MCU).
* Bucle principal 0x31D2: `bsr 0x89E4` (fundido), `jsr 0xE584` (intro), 0x3224 (`bsr 0x8C56`), 0x3270 (`jsr 0x10DFE`), …, 0x33C8 `jsr 0x123D0`, 0x88A8, 0x831A, 0x8002, `jsr (a0)` (fef828=0x9EB6), 0xA258, 0xA17E, `jsr 0xB064`, espera MCU 0x33F0, `jsr 0xAB2C`, `jsr (a0)` (0+fef836=0xA32A), `jsr 0x127B8`, …

## 4. RAM compartida FEC000-FEFFFF (16 KB; el DS5002FP la ve en 0000-3FFF)

* **Pila**: SSP inicial **FEFF80** creciendo hacia abajo; mínimo observado FEFE80 (≤ 256 bytes; la sombra de paleta acaba justo en FEFE80). Los manejadores de excepción usan A7=FEFFF0 (0x5CE). **FEFF80-FF0000 no se toca salvo la pila.**
* `fec000-fec0ff`: zona del monitor/excepciones (fec000 ptr, fec004, fec006 A0, fec00a registros, fec042, fec046 SR, fec048 PC, fec04c nº de excepción); el init la borra desde fec100 (no toca fec000-fec0ff). **fec100** = palabra de handshake con el MCU (0x33F0), fec102.
* `fec108/fec109` DSW invertidos. `fec10a` long. `fec13e/140/142/144` scroll. `fec150-fec1ff`: flags de estado (fec151 service, fec152 región/idioma (&3 elige tablas de sonido), fec154, fec162 créditos cfg, fec166/16a fundido, fec188-fec197 **4 punteros de datos de etapa**, fec198 nº etapa, fec19a/19c/19e, fec1a0, fec1a6 contador de frames, fec1aa/1ae cronómetro, fec1b2/1b4, fec1c2, fec1ca/1d6, fec1de puntero a objeto, fec1e2-1ec coinage, fec1ee créditos, fec1f8 A/D, fec1fa volante, **fec1fc/fec1fd mandos P1/P2**).
* `fec200-fec2ee`: objeto coche jugador (fec224 posición en el mapa long, fec228, fec242/244/248/24a scroll de mapa, fec256-262, fec270/272, fec27c-fec286 contadores de sprites, fec28c…).
* Tablas de objetos: `fec28c`/`fec58c`/`fec88e` (coches rivales, 0x300 cada uno), `fecea6`, `fed466`+`fed468`, `fed52e`/`fed530`, `fed5f6`, `fed5fc` (ptr), **`fed600-feddf8` lista de sprites A**, **`feddf8-fee5c8` lista B**, **`fee5c8-fee8e8` lista C**, `fee8f0` (20 × 10 bytes, 0x8000), `fee9b8` (lista fef54a, 22×6), `feea4a`/`feea6e` (5×6), `feea8c-feeb5c` variables de carrera/etapa (feea8c/90/94 de la tabla de etapas, feeab0, feeabc, feead0-eae8…), `feeb5c` 28 atributos de superficie, `feebc4`, `feebd2-feebe8` estado de sonido, **`feebea-feebfb` cola OKI, `feebfc-feec3b` 4 canales**, `feec3c/feec48` ptr de bloques, `feec40-46` flags.
* `fef14c-fef18c`, `fef24c-fef268`, **`fef252-fef36a` tabla de récords (descifrada de 0xEFEF2)**, `fef366` buffer de texto hex, `fef378-fef3c2` estado de pantalla/menú, `fef45c-fef476`, `fef4b4-fef4f6` (fef4f6 ptr de lista), `fef500` **palabra "Coprocessor OK" = 0xA53E** (MCU), `fef506-fef55a` (punteros descifrados: fef506, fef51c, fef524, fef540 (=0x67CE546B clave de comprobación con 0xEE14C en 0x14034), fef54a), **`fef558-fef7e0` tabla copiada de 0xEF9AA (0x144 palabras)**, `fef7f8-fef866` punteros de código/datos (fef7f8 `jmp (a0)` 0x604A, fef828 (0x9EB6), fef836 (0xA32A), fef85c, fef862 SP guardado), `fef866-fef878` calibración A/D, **`fef87e-fefe80` sombra de paleta (769 palabras)**.
* Dinámicamente (atracción+partida) el 68k toca 8.872 bytes en 43 rangos (ram_ranges.csv). El test de RAM del self-test escribe los 16 KB. Desde el punto de vista del MCU, las zonas interesantes son fec100, fec1fc-fec1fd, fef500 (ver análisis MCU).

## 5. Datos de pista

* **Tabla de etapas 0xDFBBC**, 12 entradas × 16 bytes: `w0, w1` (0xC446,0x33A5 en todas), `+4` long cifrado → puntero a un **bloque de 4 longs cifrados** (0xDF508 + 16·k), `+8` long cifrado → `feea8c/feea90` (p. ej. 0x2168/0x1E0C = tamaño/ límites en píxeles), `+C` long cifrado → `fec1a0`/`feea94` (flags). Se cargan en 0x3DDA-0x3E84 (nº etapa en `fec198`, 1..12) escribiendo cada long en `$100004/$100008/$10000A/$102000` y releyéndolo (0x3DFE-0x3E7A).
* Bloque de etapa (descifrado) → **`fec188` = metabaldosas capa 1 (fondo) = 0x019014 para TODAS las etapas; `fec18c` = metabaldosas capa 0 (primer plano) = 0x09968C para todas; `fec190` = mapa capa 1; `fec194` = mapa capa 0`** (stages.csv):

| Etapa | mapa capa 1 (fec190) | mapa capa 0 (fec194) |
|---|---|---|
| 1 | 01500A | 01700A |
| 2 | 06D61E | 06F61E |
| 3 | 071628 | 073628 |
| 4 | 07D646 | 07F646 |
| 5 | 075632 | 077632 |
| 6 | 08565A | 08765A |
| 7 | 081650 | 083650 |
| 8 | 089664 | 08B664 |
| 9 | 07963C | 07B63C |
| 10 | 08D66E | 08F66E |
| 11 | 091678 | 093678 |
| 12 | 095682 | 097682 |

* **Formato** (rutinas 0x9A1A y 0x9A38, y sus gemelas 0x9D52/0x9D70 para la capa 0):
  * Mapa = matriz de índices de 16 bits: `índice = mapa[ ((y>>7)<<5) + (x>>7) ]` → **32 metabloques de ancho (4096 px) × 128 de alto (16384 px)**, 8192 bytes por capa (2 × 8192 = 16 KB por etapa; el 2.º mapa mide 8202 B hasta el siguiente: 10 B de relleno).
  * Metabloque = **8×8 baldosas de 16×16 px = 64 parejas (código, atributo) cifradas = 256 bytes**: `dirección = base + (índice<<8) + ((y>>4)&7)<<5 + ((x>>4)&7)<<2`. El 68k copia la pareja con un único `move.l (a0),(a1)` a VRAM (0x9916/0x99B2 capa 1 en 102000+, 0x9C4E/0x9CEA capa 0 en 100000+), actualizando sólo la fila/columna que entra al hacer scroll (`fec242/fec248` posición, `fec256/fec25a` tamaño visible; `fef862` guarda SP).
  * Conjuntos de metabaldosas: capa 1 **0x19014-0x6D61E (345.610 B = 1.350 metabloques)**, capa 0 **0x9968C-0xDF508 (286.332 B = 1.118 metabloques)**. Mapas: **0x1500A-0x1900A** (etapa 1) y **0x6D61E-0x9968C** (etapas 2-12, 180.334 B). **Total datos de pista = 0x1500A..0xDF508 = 828.670 bytes (79 % de la ROM)**, todos cifrados (parejas de palabras).
  * Los atributos (palabra 2) llevan en los bits 8-12 el código de superficie/colisión que el 68k lee desde la VRAM descifrada (0x9E0E, 0x9F5C/0x9F62, 0x6C76, 0x7518, 0x7A26, 0x7EF2, 0x832A, 0x88B8) → en el port hay que mantener una **copia descifrada de las dos capas en RAM** (16 KB) o leer el atributo descifrado directamente de los datos pre-descifrados.
  * Animación: 0x9DF4 recorre 15×24 baldosas de la capa 0 y sustituye las de código (hi-0x1A00)&0x1F80 ∈ {0x0E00, 0x1080} por las parejas de **0xF01FC / 0xF05FC** (90 entradas × 4 B, indexadas por `(hi&0x7F)` y bit6 del atributo).
  * Baldosa vacía: parejas cifradas **0xDF64C** (capa 1) y **0xDF650** (capa 0) usadas por 0x11FDC/0x11FF2 para borrar la capa entera (2048 `move.l`).
* **Datos de imagen fuera de las pistas** (también parejas cifradas): bloques capa 1 en **0xE5674** (cabeceras cifradas de 4 B: alto, ancho, offset; datos en 0xE5674 + (0xE5672/3 como 24 bits)<<2) y capa 0 en **0xEA1BE** (ídem con 0xEA1BC); fuente/texto a través de ellos (0x12172 interpreta cadenas con códigos de control 01/02/03 a partir de 0xED3BB…).
* Confirmado en MAME (cov3): en la partida (etapa elegida = 6) `fec188=00019014 fec18c=0009968C fec190=0008565A fec194=0008765A fec198=6 feea8c=1F68 feea90=1F0C`; 0x9A4C leyó el mapa 0x8749C-0x8762A y 0x9D84 el 0x8949C-0x8962A; 0x9916/0x99B2 leyeron metabaldosas 0x19014-0x3F912 y 0x9C4E/0x9CEA 0x9968C-0xCE58A. En la atracción el único lector masivo de 0x1900A-0x4E888 fue 0xDA50 (suma anti-manipulación, no la demo).
* Lectores (todos en código, direcciones calculadas, no absolutas): 0x9A4C `move.w (a0),d4` (mapa), 0x9916/0x99B2/0x9C4E/0x9CEA `move.l (a0),(a1)` (metabaldosas), 0x11D34/0x11D9E (bloques), 0x9E56/0x9E76 (animación). Para el cartucho Neo Geo, **todo 0x15000-0xDF508 y 0xE5674-0xEFxxx puede vivir en P2 como datos puros** siempre que las 4 bases (`fec188-fec197`, cargadas de la tabla descifrada 0xDF508-0xDFBBC) y `feec3c/feec48` se apunten a la nueva ventana, y los 59 `#imm32`/405 `lea` a ROM (romdata_refs.csv) se repasen; el código (≤ 0x14DDE) y las tablas pequeñas (0xDF508-0xF0A20, 70 KB) caben en P1 en sus direcciones originales.

## 6. Notas para la reubicación

1. Mantener el código en sus direcciones originales (0x400-0x14DDE) es viable: la cabecera Neo Geo ocupa 0x100-0x1FF y los vectores los da la BIOS; en WR 0x100-0x3FF está libre y 0x400-0x5AE sólo contiene manejadores de excepción/monitor sin uso. Así los 9 punteros de código descifrados (0x9EB6, 0xA32A, 0x3224, 0x8FE6, 0x9FE2, 0x4D4E, 0x6476, 0xA66D, 0xFEC15C/0xFEC156) siguen siendo válidos.
2. Sustituir cada `move.l $ROM,$VRAM` + relectura (oracle_sites.csv) por el valor descifrado; sustituir las lecturas de VRAM de atributos por lecturas a la sombra; cambiar `0x102000/0x100000` por la sombra en las 4 rutinas de copia y en 0x9E00/0x9F34/0x11CF4/0x11D62/0x11FDC/0x11FF2, y dejar que una capa nueva (VBlank) vuelque de la sombra a la VRAM de Neo Geo / construya los tiles.
3. Paleta: sólo se necesitan 200000-200E00 (y 203000 si se activa 0x8A5A); la sombra `fef87e` ya existe en RAM: el port puede leer de ahí.
4. Sprites: la capa nueva lee las tres listas de RAM (fed600/feddf8/fee5c8 + contadores fec27e/fec280/fec282/fec284) y 0x44E6 se anula.
5. E/S: 0x520C (700002/3), 0x42D2/0x319E/0x5B02 (700009), 0x564E/0x5ADC (700004), 0x240C/0x243A (700000/1), latches 7000xA (ver 3h), OKI 70000D/70000F sólo dentro de 0x14000-0x14444 (+ excepciones/monitor).
6. Self-test: parchear 0x2704-0x271E (checksum) o aceptar el nuevo valor; emular `fef500=0xA53E` y `fec100≠0` del MCU; el test de RAM de paleta (0x2752) escribe 0x1FFF palabras en 200000: redirigir.

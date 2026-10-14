# WR2 -> PS1: formatos de datos (H3-H5)

Lo que comparten las herramientas del PC (`tools/`) y el programa de la PS1 (`ps1/`). Completa el diseno (`diseno.md`
secciones 3, 4, 6 y 10); si algo difiere, manda este documento. Todo en little endian salvo lo que viene de la placa.

## 1. Colores

- Paleta del juego: 64 paletas de 32 entradas en 0x210000-0x210FFF (`rc_pal`, entrada `c * 32 + pen`, 16 bits
  `xRRRRRGGGGGBBBBB`: R en los bits 10-14, G en 5-9, B en 0-4). Viva: el juego la escribe (fundidos).
- Color de la PS1 (15 bits): `c15 = R | G << 5 | B << 10`; si sale 0 (negro) se pone **0x8000** (negro opaco). La
  entrada 0 de cada paleta (pen 0, transparente) es siempre **0x0000**.
- El color de fondo de la pantalla (`GP0 02h`) es la entrada 0 de la paleta 0 en 24 bits.

## 2. Tiles

El tile `t` (0-0x7FFF) son 16x16 pens de 5 bits (0-31) en `work/gfx1.bin` (`tools/gfx.py`, 5 planos). Pen 0 es
transparente en el plano y en los sprites.

### 2.1 Registro A (menus: exacto, 8 bpp en la VRAM): 162 bytes

| Bytes | Que |
|---|---|
| 0-1 | codigo (u16) |
| 2-129 | nibbles bajos (bits 0-3 del pen): byte `2 + y * 8 + x / 2`; el pixel par en el nibble bajo |
| 130-161 | bit 4 del pen: byte `130 + y * 2 + x / 8`, bit `7 - (x & 7)` (el pixel de la izquierda en el bit alto) |

En la VRAM el hueco A es de 8 x 16 halfwords (texels de 8 bits = pen); la CLUT es la paleta del juego (seccion 4).

### 2.2 Registro B (carrera, demo, mapa: 4 bpp con CLUT propia): 144 bytes

| Bytes | Que |
|---|---|
| 0-127 | indices de 4 bits: byte `y * 8 + x / 2`, el pixel par en el nibble bajo; 0 = transparente |
| 128-129 | codigo (u16) |
| 130-139 | receta: pen de cada indice 1-15, 5 bits cada uno, empaquetados desde el bit 0 del byte 130 (indice 1 en los bits 0-4, indice 2 en 5-9...), 75 bits |
| 140 | paleta con la que mas se usa (0-63) |
| 141 | banderas: bit 0 sprite (0 plano), bit 1 celda de efecto, bit 2 indice = pen (sin perdida), bit 3 media paleta alta (pens 16-31 solo) |
| 142-143 | ultimo avance FEFD08 en el tramo + margen (u16; 0xFFFF = siempre) |

Si el tile usa <= 15 pens distintos (sin contar el 0) la receta es exacta; si no, `tools/tiles4.py` funde pens por
cercania de color con la paleta mas vista. Las celdas de efecto (color & 0x3F == 0x3F) usan pens 1-15: indice = pen y se
dibujan con las CLUT fijas de RESTA/SUMA. La CLUT de un hueco B con la paleta `c` es
`[0, c15(pal[c * 32 + receta[1]]), ..., c15(pal[c * 32 + receta[15]])]` (16 entradas); se rehace si cambia la paleta.

### 2.3 Ficheros por codigo (emergencias)

`TILESA.BIN`: 32 768 registros A por codigo, 12 por sector (seccion 3): el codigo `t` esta en el sector `t / 12`,
registro `t % 12`. `TILES.BIN`: igual con registros B, 14 por sector (`t / 14`, `t % 14`). `tools/tilesbin.py`.

## 3. Sectores de paquete (2 048 bytes)

Cabecera de 32 bytes y registros seguidos (14 B o 12 A por sector; el resto a 0):

| Bytes | Que |
|---|---|
| 0-3 | `WR2T` |
| 4 | tipo: 0 registros A, 1 registros B, 2 tramo de ROM, 3 mapa (B), 4 VO (B, solo a la VRAM) |
| 5 | registros en el sector (A y B) |
| 6-7 | paquete (u16): 0x0100 + pantalla (MENUS), 0x0200 + demo (DEMO), 0x0300 + tramo (ST), 0x0400 + coche (GAME), 0x0500 TILESA, 0x0501 TILES, 0x0600 ROM |
| 8-11 | indice del sector dentro del paquete (u32) |
| 12-15 | primer avance FEFD08 de los registros del sector (u32; B) |
| 16-19 | tipo 2: direccion de la ROM del tramo (u32); tipo 3/4: 0 |
| 20-23 | tipo 2: bytes de ROM en este sector (<= 2 016) |
| 24-31 | 0 |

Un tramo de ROM (tipo 2) va en sectores seguidos: bytes 32-2047 son ROM en el orden de la placa (big endian, como
`prog.bin`; la PS1 los cambia de orden al copiar, `rom_sw.bin`).

## 4. VRAM (1 024 x 512 halfwords)

- FB0 (0,0) y FB1 (0,256), 368 x 240; se dibuja con desplazamiento -8 (las columnas 8-375 de la placa).
- **C0**: x 368-383, y 0-511: 512 CLUT de 16 entradas (una por fila), para B.
- **S0**: y 240-255, x 0-367. Filas 240-245: las 64 paletas del juego como CLUT de 32 entradas: paleta `c` en
  `x = 32 * (c % 11)`, `y = 240 + c / 11`. Fila 246: RESTA (x 0-15) y SUMA (x 16-31) de 16, RESTA32 (x 32-63) y SUMA32
  (x 64-95). Filas 247-255: CLUT libres de 16 (x multiplo de 16).
- **S1**: y 496-511: x 0-63 letra de 4 bpp (CARGANDO y cifras); x 64-367 CLUT libres.
- **Bloques** b0-b19 de 64 x 256 halfwords: `x = 384 + 64 * (b % 10)`, `y = 256 * (b / 10)`.
  - B: hueco `s` (0-4 351) en el bloque `s >> 8` (una pagina de textura de 4 bpp, 256 x 256 texels): U = `16 * (s & 15)`,
    V = `16 * ((s >> 4) & 15)`; se sube a `(x + 4 * (s & 15), y + 16 * ((s >> 4) & 15))`, 4 x 16 halfwords.
  - A: pagina `p` (0-9) = bloques `(2p, 2p + 1)` (una pagina de textura de 8 bpp): hueco `s` (0-2 559) en la pagina
    `s >> 8`, U = `16 * (s & 15)`, V = `16 * ((s >> 4) & 15)`; se sube a `(x(2p) + 8 * (s & 15), y(2p) + 16 * ((s >> 4) & 15))`,
    8 x 16 halfwords.
  - Reparto por fase (diseno 4.1): CARRERA/DEMO/MAPA b0-b16 tiles B (4 352 huecos) y b17-b19 CLUT; MENU paginas A 0-9;
    menus de partida: paginas A + 4 bloques B fijos del coche.

## 5. Ficheros del CD (en orden de uso; `ps1/mkiso.py` con MKISO_MAS)

`SYSTEM.CNF`, `WR2PS1.EXE`, `ROM.BIN` (0x000000-0x0DD7FF en el orden de la placa, 907 264 B), `ROMRES.BIN` (tramos
residentes, tipo 2), `MENUS.PAK` (seccion A por pantalla), `DEMO0-3.PAK`, `GAME{N,L,R}.PAK`, `ST00-11.PAK`, `TILES.BIN`,
`TILESA.BIN`, `SPU*.BIN` (H6), `LOGO.BIN`. Cada `.PAK` lleva un indice al principio: sector 0 = tipo 5 (indice):
bytes 32+: entradas de 16 bytes `{u32 clave, u32 sector inicial, u32 sectores, u32 registros}`, 125 por sector,
terminadas con clave 0xFFFFFFFF (puede ocupar varios sectores seguidos). Claves: MENUS = `modo << 16 | FEC146`;
DEMO/ST: 1 = mapa, 2 = VO, 3 = L2, 4 = ROM.

## 6. Como se dibuja (resumen de diseno 4.3)

1. `GP0 02h` con la entrada 0 de la paleta 0.
2. Plano 0 (VRAM 0xC000; `vregs[0] = 0x0C90`): 24 x 16 posiciones desde X = `(VRAM[0x2802/2] + 0x14 + 8) & 0x3FF`
   (ya con las 8 columnas), Y = `(VRAM[0x2800/2] + 1) & 0x1FF`; palabra 0: color bits 9-14, espejo X bit 7, Y bit 6;
   palabra 1: codigo. Sin espejo, rectangulo con textura 16 x 16 (`0x65`/`0x7D`); con espejo, cuadrilatero `0x2D`.
3. Sprites de la copia del vblank anterior con el banco de `vregs[1]` bit 4 actual, entradas 0-511 (bit 15 de la
   palabra 0 = 1 fuera), celdas como en `rc/blklog.c`; color = `((w0 >> 9) & 0x7F) + (celda >> 12)`; efecto si
   `(color & 0x3F) == 0x3F` (pens 1-7 RESTA p, 8 SUMA 8, 9-15 SUMA 16 - p).
4. Paletas: se convierten las entradas que cambiaron y se suben (A: la CLUT de 32; B: se rehacen las CLUT de los
   huecos visibles con esa paleta).

## 7. Decisiones de los constructores (`tools/paquetes.py`, `tiles4.py`, `rombank.py`, `tilesbin.py`)

1. Bit 3 del registro B (media paleta alta) solo si el tile no tiene pen 0 ni pen 16 y todos sus pens estan en 17-31:
   indice = pen - 16, receta k -> k + 16 (se puede dibujar con las entradas 16-31 de la CLUT del juego). Bit 2: todos los
   pens en 0-15 (las celdas de efecto llevan bits 1 y 2; ninguna usa pens > 15). Bit 0 (sprite): el codigo nunca se vio
   en el plano. Bit 1 (efecto): solo si se vio unicamente como efecto.
2. Fusion: cada indice es un pen real de la paleta mas vista; el grupo con menos pixeles toma el color del otro (pens
   del mismo color se funden gratis). Medido en el attract: 34 % exactos, 11 % exactos en color, el resto con perdida
   (PSNR mediano 38,1 dB: plano 37,7, sprites 39,2). Un tile con paletas distintas en pantallas distintas lleva un
   registro en cada paquete; no hay variantes dentro de un paquete.
3. Avance (bytes 142-143): ultimo FEFD08 en que se ve el tile en cualquier instancia de la demo + `--margen` (16),
   saturado a 0xFFFF; mapa 0; TILES.BIN 0xFFFF. Cabecera bytes 12-15 de VO/L2: el menor primer avance del sector.
4. VO/L2: orden por primer cuadro relativo al principio de la instancia de la demo (minimo entre instancias); VO = los
   primeros `--vo` (4 352), L2 = el resto y, al final, los codigos escritos en el plano nunca visibles (clase 3).
5. Sectores de ROM (tipo 2): cada tramo lleva su cola de 4 bytes (bytes de ROM = largo + 4); un tramo son sectores
   seguidos con direccion contigua, cada cabecera con la direccion de su primer byte; byte 5 a 0; en el indice,
   registros de la seccion ROM = tramos.
6. Indice (tipo 5): byte 5 = entradas del sector (sin el terminador); paquete = base (0x0100 MENUS, 0x0200 + r DEMO);
   indice del sector = 0. Sectores absolutos del fichero. ROMRES.BIN y TILES*.BIN no llevan indice. En MENUS el campo
   paquete de cada seccion es 0x0100 + ordinal de la pantalla (orden de clave).
7. MENUS: pantallas vistas en el attract y en la partida de Portugal (modos 2, 3, 6, 8, A, B, D, E, F); fuera los modos
   0, 1, 5 y 9. La mayor, 3/0A (fotos), 2 465 tiles.
8. Paleta de un tile: la mas vista en el BLKLOG (ningun sprite usa color >= 64); los colores, del volcado mas vivo
   (suma RGB maxima) dentro de las instancias de la pantalla, para esquivar fundidos.
9. TILESA.BIN: 2 731 sectores, el ultimo con 8 registros. ROMRES.BIN: los tramos residentes de `work/romres.txt`
   (`R inicio fin`; las lineas `B fec3c6 fec3c8 inicio fin` son los bancos por tramo, para `rc_host ROMCHK`).

# Thunder Hoop NG — port de Thunder Hoop (Gaelco, 1992) a Neo Geo

Estado: **v0.1, hito 1** — en MAME (`aes`) arranca, pasa los tests del arcade y muestra la intro completa
con todos sus textos, el título y el modo demo. Sin sonido todavía.

## Cómo funciona

* El programa 68000 del arcade corre **nativo** en el 68000 de Neo Geo (misma CPU y reloj). `tools/mkrom.py`
  reubica las referencias absolutas a la RAM de trabajo (FF8000-FFFFFF → 104000), paleta, sprites, E/S y
  registros de vídeo a la RAM de Neo Geo (mapa en `src/ram.i`), también en las tablas de datos y en los
  punteros de 24 bits de los scripts de objetos (se reubican al vuelo donde el intérprete los enmascara).
* La VRAM del arcade está "cifrada" (Gaelco, parámetros 0x0E / 0x4228): el hardware descifra cada escritura.
  Cada sitio del programa que escribe en 0x100000-0x103FFF se sustituye por un opcode de línea A cuyo stub
  (generado) descifra por software con tablas (`src/crypt.s`, `tools/mkcrypt.py`, verificado contra el
  algoritmo de MAME). Detalle importante del 68000: `clr.l` escribe primero la palabra baja, así que el
  hardware ve dos "primeras palabras", no un par.
* Protecciones: la calibración de velocidad (bucle 0x49E, comprobada en el jefe 2) se fija a 0xDC1; la suma
  de la ROM que calcula al arrancar se fija a la original; el byte 0xC0 de la tabla de vectores (región) se
  conserva (0xFB = Europa).
* Vídeo (`src/video.s`): los dos tilemaps de 32x32 celdas de 16x16 son 21 sprites-columna cada uno; los
  sprites del arcade (511 entradas) van a sprites Neo Geo con asignación fija (solo se reescriben las
  entradas que cambian); el texto (fuente 8x8, códigos 0xF400-0xFFFF) va a la capa fix.
* Gráficos: `tools/gfx2crom.py` (C ROM 2 x 8 MB + tabla de tiles 8x8 + S ROM con la fuente).

## Compilar

    make game ARCADE=<carpeta con el set thoop de MAME>
    mame aes -cart thng -hashpath build/hash -rompath "build/roms;<bios>"

## Pendiente

* Cuelgue en la tercera demo del attract (índice de `trap #1` fuera de tabla en 0xCAB8).
* Encuadre vertical: el arcade usa 240 líneas (HUD arriba y abajo), Neo Geo 224; ahora se recortan las 16 de
  arriba (`CROP_Y`), lo que oculta el marcador superior en partida.
* Sonido (OKI → ADPCM-A, como en World Rally NG), prioridades sprites/tiles, monedas/créditos del BIOS,
  MVS, hardware real.

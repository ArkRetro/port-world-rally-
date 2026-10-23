# Thunder Hoop NG — port de Thunder Hoop (Gaelco, 1992) a Neo Geo

Estado: **v0.5** — en MAME (`aes`) arranca, pasa los tests del arcade, muestra la intro completa con sus
textos, el título y el modo demo, y se puede jugar (START, sin monedas: free play), con música y efectos.

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
* Los sitios de escritura en VRAM salen del análisis estático más un "tap" de MAME sobre el port que avisa
  de cualquier escritura en 0x100000-0x103FFF hecha por código arcade sin stub (`analysis/vram_sites.txt`).
  Los inmediatos no se parchean: el juego comprueba algunos en ROM (antimanipulación); solo cambia el opcode.
* Protecciones: la calibración de velocidad (bucle 0x49E, comprobada en el jefe 2) se fija a 0xDC1; la suma
  de la ROM que calcula al arrancar se fija a la original; el byte 0xC0 de la tabla de vectores (región) se
  conserva (0xFB = Europa).
* Vídeo (`src/video.s`): los dos tilemaps de 32x32 celdas de 16x16 son 21 sprites-columna cada uno; los
  sprites del arcade (511 entradas) van a sprites Neo Geo con asignación fija (solo se reescriben las
  entradas que cambian); el texto (fuente 8x8, códigos 0xF400-0xFFFF) va a la capa fix.
* Sonido: el secuenciador del arcade (el mismo de Gaelco que World Rally) sigue intacto; sus seis escrituras
  al OKI6295 se enganchan (`src/sound.s`, de World Rally NG) y van al Z80 (`z80/driver.s`), que toca las
  fases convertidas a ADPCM-A (`tools/oki2adpcma.py`: 109 fases, V ROM de 4 MB).
* Encuadre: el arcade muestra 240 líneas y Neo Geo 224, a tamaño real (escalar no sirve: los tilemaps son
  sprites de 512 líneas que dan la vuelta y la capa fix no escala).  En intro / título / menús se ven las
  filas 32-255; en partida (hay algo en la franja del marcador superior) el área de juego muestra las filas
  24-247 y los marcadores se acercan 8 líneas cada uno: se ven enteros arriba y abajo.
* Capas y prioridades (MAME `screen_update_squash`): todos los sprites del juego son de prioridad 1 y los
  tiles casi siempre de prioridad 2/3 (debajo de los sprites; tilemap 1 < tilemap 0, como en Neo Geo).  Las
  celdas de prioridad 1 del tilemap 0 tapan a los sprites con sus colores 1-7: 21 columnas "HI" encima de
  los sprites repiten esas celdas con tiles sin los colores 8-15 (C ROM 0x14000+); una columna sin celdas
  así tiene altura 0 y no gasta del límite de 96 sprites por línea.
* Sprites por línea: medido en el arcade, como mucho 33 objetos por línea (logo del título); con las 42
  columnas de los fondos, 75 (+ las HI que haya), por debajo de 96.  El texto va a la capa fix y no cuenta.
* Gráficos: `tools/gfx2crom.py` (C ROM 2 x 8 MB + tabla de tiles 8x8 + S ROM con la fuente).

## Compilar

    make game ARCADE=<carpeta con el set thoop de MAME>
    mame aes -cart thng -hashpath build/hash -rompath "build/roms;<bios>"

## Pendiente

* Prioridades sprites/tiles, monedas/créditos del BIOS,
  MVS, hardware real.

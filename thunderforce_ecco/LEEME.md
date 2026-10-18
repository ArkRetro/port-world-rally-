# Delfín de Ecco en el demo de Lightening Force

Parche para **Lightening Force - Quest for the Darkstar (USA)** (Thunder Force IV, Mega Drive, 1 MB) que cambia la nave del jugador por el delfín de *Ecco - The Tides of Time* **solo en el primer demo** (fase Strite). En los otros demos y en la partida normal la nave es la original.

No se incluyen ROMs.

```
python3 -I build_patch.py "Lightening Force (USA).bin" frame_000.png tf4_ecco.bin tf4_ecco.ips
```
Opciones: `--everywhere`, `--pixels`, `--intro DIR`, `--bosses DIR`, `--invincible` (ver abajo).
Con `--everywhere` el delfín sale en todo el juego (demos y partida normal), no solo en el primer demo.

El segundo argumento es la carpeta `frames/` del delfín extraído de Ecco (fotogramas mirando a la derecha). Se usa una pose para cada fotograma de la nave (`FRAMES` en el script: 5, 4, 1, 0, 3, 27, 24, de morro arriba a morro abajo), reducida para caber en 32×16, ampliada un 3% (`SCALE`, lo que sobra se recorta) y ajustada a la paleta de la nave. Después se le dibujan dos cañones pequeños hacia delante, uno en el lomo y otro en la tripa (`add_cannons`). Si se pasa un solo PNG, se usa para los 7 fotogramas.

## Cómo funciona
- La nave son 7 fotogramas de 4×2 tiles (32×16), paleta 0, tiles de VRAM 0x440–0x477, desde inclinada hacia arriba del todo (0x440) hasta hacia abajo del todo (0x470).
- Vienen comprimidos en el archivo de ROM `0x90000`, entrada 3 (descompresor en `0x000EAE`, portado en `tf4_unpack.py`), que se descomprime a RAM `FF0800`. La nave ocupa `FF0800–FF0EFF` y luego se pasa a VRAM por DMA.
- En `0x000B72`, justo después de descomprimir esa entrada, `lea $90000.l,a0` pasa a ser `jsr $100000.l`. La rutina nueva comprueba:
  - `$FFF2FE` ≠ 0: modo demo.
  - `$FFF0B8` = 0: fase 0. El demo rota las fases 0–3 (contador `$FFF074`, empieza en 3) y la primera tras encender es la 0.
  
  Si se cumplen las dos, copia el delfín encima de la nave en RAM y repite el `lea` original.
- La ROM se amplía a 2 MB para la rutina y los tiles (`0x100000`). Se actualizan el fin de ROM y el checksum de la cabecera, porque el juego lo comprueba en `0x34C`, sumando desde `0x200` hasta el fin de ROM sin la última palabra.

## Comprobado
En Genesis Plus GX: el delfín aparece en el primer demo (Strite), el segundo demo (Ruin) lleva la nave normal, y una partida empezada con Start en Strite también. Con `--everywhere`, el delfín aparece en los dos demos y en la partida.

## Logo PIXELS
Con `--pixels`, el logo de SEGA del arranque pone **PIXELS**, con el mismo estilo (letras azules con contorno y surco) y la misma animación (caída, brillo y fundido), porque la animación es de paleta y no se toca.

- `pixels_logo.py` dibuja PIXELS en 96×32 con los índices de color del logo original y lo guarda como flujo literal para el descompresor del juego, en `0x1F0000`.
- La entrada 0 del archivo `0x90000` (el logo de SEGA) apunta a ese flujo. El sprite "TM" (`0x8E4`) se deja vacío.

## Intro: el humano se convierte en delfín
Con `--intro DIR` (la carpeta `intro-transformacion`: `secuencia.json`, `tiles/`, `paletas/`), la transformación sale dentro de la intro del juego, cuando aparece la nave (Rynex). Primero sale el primer plano de la cara y luego los 21 pasos (unos 6 s). La intro se queda parada mientras tanto y sigue justo donde estaba; la música de Thunder Force sigue sonando porque la lleva el Z80. Pulsando Start en el mando 1 se salta.

- En el bucle de la intro, `0x00C572` (`move.w #$8144,$C00004`, encender la pantalla tras mandar los sprites) pasa a ser `jsr $101000.l` + `nop`.
- `intro.s` repite esa instrucción y mira la lista de sprites de la intro (`FFEC00`): cuando el sprite 2 es de 4×4 con el tile 0x1D0 o 0x1E0 (la nave), arranca la transformación una vez por intro (marca en `$FFF0C0`, RAM que el juego solo borra al encender; se rearma al principio de cada intro con el contador `$FF8C80`).
- Con `--intro` también cambian dos gráficos de la intro (`intro_gfx.py`, archivo de ROM `0x19000`):
  - Entrada 2: la nave girando sobre sí misma mientras pasan las letras LIGHTENING FORCE pasa a ser el delfín girando (con sus cañones). Son 7 fotogramas de 64×32 con la paleta 2; cada uno es el delfín aplastado en vertical según el ángulo del giro (tabla de la intro en `0xD670`).
  - Entrada 3: el logo «Techno soft» pasa a «Blostesoft», con el mismo degradado por filas y la misma sombra. La «®» y «PRESENTED BY» se quedan.
  - Cada entrada se guarda sin comprimir (flujo literal) en `0x1F1000` y `0x1F5000`, y la tabla del archivo apunta allí.
- Las letras gigantes LIGHTENING FORCE que pasan al principio pasan a decir **HECHO CON PiXELS** (`intro_letters.py`), con el mismo estilo de tubo y el mismo desplazamiento.
  - Salen de las entradas 0 y 1 del archivo `0x19000`: la entrada 0 tiene 256 bloques de 4×4 tiles y, en `+0x3058`, la tira de letras (8 bloques por cada columna de 32 píxeles) que el código de `0xCA4C` manda al plano A; la entrada 1 son los tiles (VRAM `0xA000`).
  - H, E, C, O, N y L son las letras originales píxel a píxel; P sale de la R, la i de la I con un punto, y la X y la S se dibujan con el mismo borde (7 colores hacia dentro).
  - La pantalla de título también usa esas entradas, así que no se tocan: la intro (`0x00C828`) lee una copia de la cabecera del archivo en `0x1F8000` cuyas entradas 0 y 1 son las nuevas, sin comprimir. Se respetan los tiles y el bloque vacío (0) que el juego usa fuera de las letras.
- El logo LIGHTENING FORCE pasa a decir **HECHO CON AMOR** (`title_logo.py`) en toda su animación (se estira, sale con la estela de colores y se queda en el título con el cromado).
  - Archivo `0x1C100`: entradas 0 y 2 (mapa en bloques de 4×4 y 128 tiles del logo, plano A) y entradas 1 y 3 (la estela, plano B, 512 tiles).
  - H, E, N, R y C son las letras originales; A, O y M se hacen con los palos de la T unidos por barras. La estela se vuelve a calcular con el texto nuevo: cada capa es el logo ampliado desde el punto de fuga, con el color 15−k.
- En la imagen del título, la nave pasa a ser **Ecco saltando** (`title_dolphin.py`).
  - La imagen es la entrada 4 del archivo `0x1C100`: 1120 tiles (40×28) en el plano B con la paleta 3, cargada en `0x00CFDC` (y en `0x001122` y `0x00F898`).
  - Se borra la nave (los píxeles no negros unidos a ella, con sus huecos) y se pinta el fotograma 23 de Ecco ampliado 4 veces: el contorno con EPX y el sombreado suavizado y pasado a la escala de azules de la imagen con un tramado ordenado. La luna, los rayos y las estrellas se quedan.
  - Las entradas 0–4 nuevas se guardan sin comprimir en `0x140000`, con una copia de la cabecera del archivo, y las cinco lecturas del archivo apuntan a esa copia.
- La transformación se pinta con sprites de 2×2 tiles encima del fondo de estrellas, con la paleta 3, que se guarda y se restaura al final. Los tiles van a dos bancos de VRAM libres durante la intro (tiles 792 y 1026), así el paso siguiente se carga sin que se vea a medias.
- El script quita los bloques vacíos, genera la tabla de pasos y ensambla con `m68k-linux-gnu-as`/`ld` (paquete `binutils-m68k-linux-gnu`).

## Jefes finales nuevos
Con `--bosses DIR` (la carpeta `jefes-finales`: `jefes.json`, `jefeN/tiles`, `jefeN/paletas`), los jefes de las cuatro primeras fases son los personajes nuevos:

| Fase | Jefe original | Personaje | Línea de paleta |
|---|---|---|---|
| 0 Strite | caballito de mar | hombre de gafas (`jefe1`) | 1 |
| 1 Ruin | máquina de pinzas | Bruno con la taza (`jefe2`) | 1 |
| 2 Air Raid | nave dorada | Bart bailando (`jefe3`) | 1 |
| 3 Daser | criatura de brazos de bolas | Mickey aprendiz de brujo (`jefe4`) | 3 |

El jefe original sigue moviéndose, recibiendo disparos y atacando igual; solo cambia lo que se ve (`bosses.s`, configuración en `bosses.py`):
- `0x007190` (fin del constructor de la lista de sprites, `clr.b -5(a0); rts`) pasa a ser `jmp $1A0000`. Durante el combate (objetos con rutina `$30`, banco del jefe ya cargado) la rutina quita de la lista las piezas del jefe, dibuja el personaje anclado a una pieza (la cabeza de Strite, el centro de Ruin, el casco de Air Raid, el cuerpo de Daser) y cambia sus disparos por el proyectil del personaje (gafas, taza, disco de vinilo, corchea), que gira.
- Pose y paleta según lo que hace el jefe: reposo (con parpadeos, brillos, el baile de Bart), ataque mientras hay disparos suyos en pantalla, destello al bajar la energía y derrota cuando se queda sin energía o se rompe; después se hunde poco a poco mientras explota.
- Los gráficos van en el banco de cada fase (archivo `0x94000`, comprimido "nibble RLE", tiles de VRAM 0x000–0x1FF). Se rehace el banco: las piezas del jefe se cambian por el personaje, los efectos se conservan y lo que queda en pantalla con la línea de paleta del personaje (la cola de Strite, los brazos de Daser) se recolorea. Los bancos nuevos van en `0x1B0000`, `0x1B8000`, `0x1D0000` y `0x1D8000`.
- Bart tiene 10 dibujos de baile que no caben en un banco: están sin comprimir en `0x1C0000` y el que toca se copia por DMA a uno de dos huecos de VRAM (tiles 0x018 y 0x0AB), en dos mitades en dos fotogramas, y se enseña cuando está entero. La copia la hace el código del juego que manda la lista de sprites cada fotograma: en `0x001DBE`, `move.l #$7FCE00,d0` pasa a ser `jsr boss_vblank`.
- En Mickey, «respira» redibuja todo el cuerpo y «derrota» tiene otra disposición: no caben junto a la pose base (196 tiles), así que la derrota es la pose base con la paleta de derrota.
- RAM usada: `$FFF0C2`–`$FFF0E7` (sin usar por el juego).

**Protección anticopia**: entre fases (`0x005CAE`) el juego suma los longs de `0x200` a `0xAFFF`, hace XOR con `'DEMO'` y lo compara con `0x15FF8`; si no coincide se para. Los cambios en esa zona (el del delfín en `0xB72`, el logo PIXELS, los jefes) lo activaban, así que el script recalcula ese valor.

Comprobado en Genesis Plus GX con los estados de `jefes-finales/originales/estados`: los cuatro jefes aparecen con la carga de banco del propio juego, atacan, mueren y se pasa a la fase siguiente.

## Invulnerable
Con `--invincible` la nave no muere nunca: ni con disparos, ni con enemigos, ni con el escenario. El juego se salta los choques del jugador mientras corre su temporizador de invulnerabilidad (`$FF8014`, el de después de reaparecer); los dos `tst.w $14(a6)` de la rutina de choques (`0x006302` y `0x006426`) pasan a ser saltos fijos. Los ítems de arma se siguen cogiendo. Las ROMs entregadas llevan esta opción.


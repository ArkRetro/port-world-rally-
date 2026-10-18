# Delfín de Ecco en el demo de Lightening Force

Parche para **Lightening Force - Quest for the Darkstar (USA)** (Thunder Force IV, Mega Drive, 1 MB) que cambia la nave del jugador por el delfín de *Ecco - The Tides of Time* **solo en el primer demo** (fase Strite). En los otros demos y en la partida normal la nave es la original.

No se incluyen ROMs.

```
python3 -I build_patch.py "Lightening Force (USA).bin" frame_000.png tf4_ecco.bin tf4_ecco.ips
```
Opciones: `--everywhere`, `--pixels`, `--intro DIR` (ver abajo).
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
- La transformación se pinta con sprites de 2×2 tiles encima del fondo de estrellas, con la paleta 3, que se guarda y se restaura al final. Los tiles van a dos bancos de VRAM libres durante la intro (tiles 792 y 1026), así el paso siguiente se carga sin que se vea a medias.
- El script quita los bloques vacíos, genera la tabla de pasos y ensambla con `m68k-linux-gnu-as`/`ld` (paquete `binutils-m68k-linux-gnu`).

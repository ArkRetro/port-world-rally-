# Delfín de Ecco en el demo de Lightening Force

Parche para **Lightening Force - Quest for the Darkstar (USA)** (Thunder Force IV, Mega Drive, 1 MB) que cambia la nave del jugador por el delfín de *Ecco - The Tides of Time* **solo en el primer demo** (fase Strite). En los otros demos y en la partida normal la nave es la original.

No se incluyen ROMs.

```
python3 -I build_patch.py "Lightening Force (USA).bin" frame_000.png tf4_ecco.bin tf4_ecco.ips
```
Con `--everywhere` el delfín sale en todo el juego (demos y partida normal), no solo en el primer demo.

El segundo argumento es la carpeta `frames/` del delfín extraído de Ecco (fotogramas mirando a la derecha). Se usa una pose para cada fotograma de la nave (`FRAMES` en el script: 5, 4, 1, 0, 3, 27, 24, de morro arriba a morro abajo), reducida para caber en 32×16, ampliada un 3% (`SCALE`, lo que sobra se recorta) y ajustada a la paleta de la nave. Si se pasa un solo PNG, se usa para los 7 fotogramas.

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

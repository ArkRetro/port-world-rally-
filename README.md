# World Rally 1 y 2 para PlayStation

Ports no oficiales de los arcade de Gaelco **World Rally** (1993) y **World Rally 2: Twin Racing** (1995) a la
PlayStation, para PS1 real (CD grabado, PSIO o XStation), MiSTer y emuladores.

Incluye un disco recopilatorio con los dos juegos y un menú para elegir, cada juego en su propio disco, versiones USA y
EUR (PAL), y compatibilidad con mando normal, **NeGcon** de Namco y **JogCon**.

## Descarga

En [Releases](../../releases) está **WorldRallyPS1-Creador.zip**, el creador de imágenes para Windows. También está en
este repositorio, en [`release/`](release/).

Le das la carpeta con **tus** ROM de MAME (y, si quieres, tu BIOS de PlayStation) y crea en tu ordenador las
imágenes `.bin`/`.cue` listas para grabar o copiar a la SD. Instrucciones completas en
[`herramienta/windows/LEEME.txt`](herramienta/windows/LEEME.txt).

En Linux y Mac, o en Windows con Docker, se hace lo mismo con [`herramienta/`](herramienta/LEEME.md).

## Aviso legal: por qué este repositorio y el creador no distribuyen el juego

Este proyecto sigue el mismo modelo que los motores y ports libres de juegos clásicos (ScummVM, los ports de
decompilaciones, los parches de traducción): **se distribuye solo el trabajo propio y el juego lo pone quien lo tiene**.

**Lo que hay en este repositorio y en el creador:**

- Código fuente escrito para este port: el programa de PlayStation, el menú, los conversores y las herramientas.
- Documentación del funcionamiento de los juegos, obtenida estudiando su comportamiento, para poder reproducirlo.
- El programa de Windows que automatiza la conversión, más herramientas libres de terceros (ver más abajo).

**Lo que NO hay, ni en el repositorio ni en el creador:**

- Ningún fichero de las ROM de los arcade: ni código, ni gráficos, ni sonido, ni músicas, ni trozos de ellos.
- Ninguna BIOS de PlayStation.
- Ninguna imagen de disco ya creada.
- El descifrado de la memoria de vídeo de la placa de *World Rally* (el algoritmo y sus tablas). Como el resto de
  herramientas de terceros, se obtiene en tu máquina: el creador descarga de MAME (github.com/mamedev/mame, en un
  commit fijo y comprobado con su SHA-256) el fichero fuente donde está, `src/mame/gaelco/gaelcrpt.cpp` (licencia
  BSD-3-Clause), y `wrally_ps1/tools/gaelcrpt_extrae.py` lo lee y genera de él, en la carpeta de trabajo, el código
  que usa el port. Ese código generado no se distribuye.

**Cómo funciona el creador:** lee las ROM de MAME **que tú aportas**, extrae de ellas los gráficos, sonidos y datos
del juego y los combina en tu ordenador con el código del port para crear las imágenes. El contenido protegido viene
siempre de tu copia y nunca sale del repositorio ni del creador. Comprueba además cada ROM por su CRC, de modo que
solo trabaja con volcados conocidos.

**Por qué se plantea así:**

- El código del port es obra original. Publicar código propio, aunque sirva para ejecutar un juego, no es publicar
  el juego.
- Que el usuario aporte sus propios ficheros es el mismo principio por el que es legal distribuir emuladores como MAME
  y no lo es distribuir ROM.
- Las imágenes resultantes contienen material de las ROM. Por eso son **para tu uso personal** y **no debes
  compartirlas**: la responsabilidad de tener las ROM legalmente y de no distribuir las imágenes es de cada usuario.

Esto es una explicación del planteamiento del proyecto, no asesoramiento jurídico. La legislación sobre copias de
seguridad, ingeniería inversa e interoperabilidad varía según el país; si tienes dudas, consulta la de tu país.

**Marcas:** *World Rally* y *World Rally 2* son de Gaelco. *PlayStation* es de Sony Interactive Entertainment. *MAME*
es del MAME team. Este proyecto no está afiliado, respaldado ni patrocinado por ninguno de ellos. Las marcas se citan
solo para identificar los juegos y el hardware.

Si eres titular de derechos y crees que algo de este repositorio te afecta, abre una *issue* y se revisará.

## Licencia

El código y la documentación propios de este repositorio son **MIT** ([`LICENSE`](LICENSE), © 2026 ArkRetro y
colaboradores). La licencia cubre solo eso: no los juegos de Gaelco, sus ROM, gráficos, sonido ni marcas, ni las
herramientas de terceros que se descargan, que tienen las suyas. El detalle de lo que es de terceros está en
[`THIRD_PARTY.md`](THIRD_PARTY.md).

## Herramientas de terceros

El creador descarga en la primera ejecución, cada una comprobada con su SHA-256 y con su propia licencia (el
contenedor de [`herramienta/`](herramienta/LEEME.md) instala las mismas):

- Python embebido (python.org, licencia PSF) y numpy, scipy y pillow de PyPI.
- El compilador de MIPS de PSn00bSDK (GPL).
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso), para montar la imagen de CD (GPL-2.0).
- Los ficheros de enlazado de [nugget](https://github.com/pcsx-redux/nugget) (PCSX-Redux, MIT).
- w64devkit, con gcc y busybox para Windows.
- Tres ficheros fuente de [MAME](https://github.com/mamedev/mame) (`src/mame/gaelco/gaelcrpt.cpp`, `gaelcrpt.h` y
  `wrally.cpp`, BSD-3-Clause), de donde se saca el descifrado de la VRAM de Gaelco (ver el aviso legal).

Ninguna contiene material de los juegos.

## Contenido del repositorio

| Carpeta | Qué hay |
|---|---|
| [`release/`](release/) | El creador de imágenes para Windows, listo para usar |
| [`herramienta/`](herramienta/LEEME.md) | De las ROM de MAME a las imágenes, todo de una vez (Windows, y Docker para Linux y Mac) |
| [`wrally_ps1/`](wrally_ps1/) | Port de World Rally |
| [`wrally2_ps1/`](wrally2_ps1/) | Port de World Rally 2 (con su `doc/`) |
| [`recopilatorio/`](recopilatorio/) | Disco con los dos juegos y el menú |
| [`tools/mame/`](tools/mame/) | Utilidades de MAME para depurar |

Más detalles técnicos en [`LEEME.md`](LEEME.md) y en el `LEEME.md` de cada carpeta.

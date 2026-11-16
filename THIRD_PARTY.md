# Terceros: qué hay en el repositorio y qué se descarga

El código y la documentación propios del repositorio son MIT ([`LICENSE`](LICENSE)). Esto es lo que es de otros.

## Dentro del repositorio

No hay ficheros copiados de otros proyectos. Hay datos o comportamientos tomados de MAME (licencia BSD-3-Clause para
los ficheros citados), reescritos para el port:

| Dónde | Qué | Origen |
|---|---|---|
| `wrally2_ps1/rc/rc51c.py` (`CYC`) | Ciclos máquina por opcode del 8051 (dato del juego de instrucciones de Intel): los 256 valores son los de MAME | `mcs51_cycles` de `src/devices/cpu/mcs51/mcs51.cpp` de MAME (BSD-3-Clause, copyright-holders: Steve Ellenoff, Manuel Abadia, Couriersud); el aviso va junto a la tabla |
| `wrally_ps1/tools/oki.py` (`STEPS`, `IDX`) | Tabla de pasos e índices del ADPCM de OKI/Dialogic (estándar público) | la misma que usa `okiadpcm.cpp` de MAME y el estándar ADPCM de Dialogic |
| `wrally2_ps1/rc/gae1.c`, `gae1.h` | Modelo de los registros del sonido GAE1, escrito de nuevo | comportamiento de `src/devices/sound/gaelco.cpp` de MAME (BSD-3-Clause) |
| `wrally_ps1/perf/arcade.py`, `wrally2_ps1/ps1/video.c` y las herramientas de comparación | Dibujo y prioridades de las placas, escritos de nuevo | comportamiento de los drivers `wrally.cpp` / `gaelco2.cpp` de MAME |
| `wrally_ps1/snd/psxadpcm.c` (`F0`, `F1`) | Coeficientes de los cinco filtros del ADPCM de la SPU (dato del hardware) | documentación pública de la PlayStation (psx-spx) |
| `herramienta/roms.py` | CRC32 y tamaños de las ROM (para reconocerlas) | listas de software de MAME (`wrally.cpp`, `gaelco2.cpp`) |
| `tools/mame/` | Scripts propios (Lua, comandos del depurador, Python) para depurar con MAME, del port de Neo Geo; ningún fichero de MAME | propio |

Ningún fichero del repositorio es una copia literal de un fichero de MAME.

## Descifrado de la VRAM de Gaelco (World Rally 1): no está en el repositorio

Lo genera en la máquina del usuario `wrally_ps1/tools/gaelcrpt_extrae.py` (propio, MIT), que **lee** estos ficheros
de MAME y traduce la función `decrypt()` a C y Python en `wrally_ps1/work/gaelcrpt/` (fuera de git; no se
distribuye):

| Fichero | Licencia | De dónde |
|---|---|---|
| `src/mame/gaelco/gaelcrpt.cpp`, `gaelcrpt.h` | BSD-3-Clause (copyright-holders: Manuel Abadia) | github.com/mamedev/mame, commit `f34f02505e32c1993c6a782b6814232cbfc74e36` (mame0289) |
| `src/mame/gaelco/wrally.cpp` (solo `set_params`) | BSD-3-Clause (Manuel Abadia, Mike Coates, Nicola Salmoria, Miguel Angel Horna) | el mismo commit |

Los generados llevan en la cabecera su origen y licencia. Cada fichero se comprueba con su SHA-256 (en
`gaelcrpt_extrae.py`, `herramienta/windows/creador.py` y `herramienta/Dockerfile`).

## Lo que descargan el creador y el contenedor (cada uno con su licencia; nada se incluye en el repositorio)

| Herramienta | Licencia | Para qué |
|---|---|---|
| Python embebido (python.org) | PSF | el creador de Windows (va dentro del zip del creador, tal cual) |
| numpy, scipy, pillow (PyPI) | BSD-3-Clause / BSD-3-Clause / MIT-CMU (HPND) | conversión de gráficos y sonido |
| w64devkit (gcc, binutils, busybox) | Unlicense; GPL (gcc, binutils, busybox) | compilar el banco de pruebas y el `sh` de Windows |
| gcc de MIPS de PSn00bSDK | GPL (gcc, binutils) | compilar el EXE de PlayStation |
| mkpsxiso | GPL-2.0 | montar las imágenes de CD |
| nugget (PCSX-Redux): `ps-exe.ld`, `nooverlay.ld` | MIT | enlazado del EXE |
| MAME: `gaelcrpt.cpp`, `gaelcrpt.h`, `wrally.cpp` | BSD-3-Clause | descifrado de la VRAM (ver arriba) |
| Ubuntu y sus paquetes (solo el `Dockerfile`) | las suyas | entorno del contenedor |
| Musashi (opcional, lo pone el usuario: `perf/verificar68k.sh`, `banco.sh`) | MIT | comparar el 68000 recompilado instrucción a instrucción |

## Lo que no es de nadie de aquí y no se incluye

*World Rally* y *World Rally 2* (programa, gráficos, sonido, música y marcas) son de Gaelco; la BIOS y las marcas de
*PlayStation*, de Sony Interactive Entertainment. El usuario pone sus ROM y su BIOS; las imágenes que salen son para su
uso personal.

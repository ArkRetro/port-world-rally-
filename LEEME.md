# World Rally 1 y 2 para PlayStation (arkretro)

Ports de los arcade de Gaelco *World Rally* (1993) y *World Rally 2: Twin Racing* (1995) a la PS1, para MiSTer,
PS1 real (CD grabado, PSIO o XStation) y emuladores. Uso personal. El repositorio no lleva nada de las ROM ni de la
BIOS: las imágenes se generan en tu máquina a partir de **tus** ROM de MAME.

**Para sacar las imágenes:** `herramienta/` (su `LEEME.md`). Le das tus zip de MAME (y tu BIOS) y te devuelve los
`.bin`/`.cue` del disco con los dos juegos y el menú, y de cada juego suelto. Funciona con Docker en Windows, Linux y
Mac.

| Carpeta | Qué hay |
|---|---|
| `herramienta/` | De las ROM de MAME a las imágenes, todo de una vez |
| `wrally_ps1/` | Port del World Rally (su `LEEME.md`) |
| `wrally2_ps1/` | Port del World Rally 2 (su `LEEME.md` y `doc/`) |
| `recopilatorio/` | Disco con los dos y un menú para elegir |
| `tools/mame/` | Utilidades de MAME para depurar (del port de Neo Geo) |

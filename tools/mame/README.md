# World Rally en MAME (depuración para NeoSD)

```
python3 -I neo2mame.py WRally_neogeo_final_v6.neo roms/wrally   # + neogeo.zip (BIOS) en roms/
mame neogeo wrally -rompath roms -hashpath . -video none -sound none -nothrottle \
     -seconds_to_run 120 -autoboot_script probe.lua            # sonda Lua: cambios de banco / lecturas
QT_QPA_PLATFORM=offscreen mame neogeo wrally -rompath roms -hashpath . -video none \
     -debug -debugscript trace.cmd -seconds_to_run 3           # trace del 68k con ciclos
```
No se incluyen ROMs ni BIOS.

## Medir el watchdog (reset a BIOS)
```
QT_QPA_PLATFORM=offscreen mame neogeo wrally -rompath roms -hashpath . -video none -sound none -nothrottle \
  -debug -debuglog -debugscript watchdog.cmd -autoboot_script inputs.lua -seconds_to_run 40
python3 -I wdgaps.py debug.log
```
`inputs.lua` mete moneda/start/A para llegar a la partida. `cartwrites.cmd` registra escrituras al espacio del cartucho y al registro de banco.

`exceptions.cmd` marca los manejadores de excepcion del 68k (vectores 2-8 del P-ROM); `sysregs.cmd` registra escrituras a 0x3A00xx/0x380000. Resultado en MAME: ninguna excepcion ni acceso sin mapear.

`biosram_gamewrites.cmd` / `biosram_biosaccess.cmd`: escrituras del juego (PC<0xC00000) y accesos de la BIOS a 0x10F300-0x10FFFF (RAM reservada a la BIOS). Resultado: el juego escribe en todo ese rango.

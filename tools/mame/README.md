# World Rally en MAME (depuración para NeoSD)

```
python3 -I neo2mame.py WRally_neogeo_final_v6.neo roms/wrally   # + neogeo.zip (BIOS) en roms/
mame neogeo wrally -rompath roms -hashpath . -video none -sound none -nothrottle \
     -seconds_to_run 120 -autoboot_script probe.lua            # sonda Lua: cambios de banco / lecturas
QT_QPA_PLATFORM=offscreen mame neogeo wrally -rompath roms -hashpath . -video none \
     -debug -debugscript trace.cmd -seconds_to_run 3           # trace del 68k con ciclos
```
No se incluyen ROMs ni BIOS.

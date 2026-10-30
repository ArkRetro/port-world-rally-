#!/bin/sh
# Busca dos escrituras seguidas al mismo (aN) en el codigo del port (wr.elf). En el Neo Geo real
# (y en MiSTer) la VRAM necesita > 12 ciclos entre escrituras a REG_VRAMRW; MAME no lo emula.
# (Los aciertos dentro de tablas de datos, p.ej. <dec_first>, no son codigo.)
m68k-linux-gnu-objdump -d ${1:-/home/claude/wrally/neo/build/wr.elf} | awk '/^[0-9a-f]+ </{fn=$2} prev ~ /movew.*%a[0-7]@$/ && $0 ~ /movew.*%a[0-7]@$/ {split(prev,a,","); split($0,b,","); if (a[2]==b[2]) print fn, prev, "|", $0} {prev=$0}' | grep -v -E "<(dec_first|mcu_rom|col_tab|spr_xtab|spr_ytab|spr_atab|maptab|sw_tab|fin_tab|gd_info|scan_dec|alt1_tab|tile_empty_tab|tattr_tab|l1_attab|srch_idx|map_ptab|snd_dur)>"

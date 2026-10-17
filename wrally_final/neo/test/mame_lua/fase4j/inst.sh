#!/bin/sh
# inst.sh root : instala build/prom.bin (bytes intercambiados) en una copia de rdbg
R=/tmp/claude-0/$1
[ -d $R ] || cp -r /tmp/claude-0/rdbg $R
python3 -c "
import sys
p=bytearray(open('/home/claude/wrally/neo/build/prom.bin','rb').read()); p[0::2],p[1::2]=p[1::2],p[0::2]
open('$R/roms/neogeo/wrallyng/p1.bin','wb').write(p)"

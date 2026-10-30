#!/usr/bin/env python3
# uso: dis.py START END  (hex) -> líneas de main.asm con dirección en [START,END)
import sys
s=int(sys.argv[1],16); e=int(sys.argv[2],16)
for line in open('/tmp/claude-0/-home-user-port-world-rally-/0e54570e-879c-581d-b19c-42bb81ddf7e3/scratchpad/lab/work/mcu/main.asm'):
    try: a=int(line[:6],16)
    except: continue
    if a>=e: break
    if a>=s: sys.stdout.write(line)

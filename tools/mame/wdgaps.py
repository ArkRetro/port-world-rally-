#!/usr/bin/env python3
"""Lee debug.log (generado con watchdog.cmd) y muestra los mayores intervalos entre patadas al watchdog (68k @ 12 MHz)."""
import re, sys
c = [int(m) for m in re.findall(r'^WDK (\d+)', open(sys.argv[1]).read(), re.M)]
g = sorted(((c[i + 1] - c[i], i) for i in range(len(c) - 1)), reverse=True)
print(len(c), "patadas")
for d, i in g[:8]:
    print("gap %.4fs (%d ciclos) en t=%.2fs" % (d / 12e6, d, c[i] / 12e6))

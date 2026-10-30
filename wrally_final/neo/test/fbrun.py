import sys, time
from fbemu import *
from PIL import Image
e = Emu('/tmp/claude-0/fbt/roms/wrallyng.zip', '/tmp/claude-0/fbt/sys')
print('carga', e.ok)
shots = [300, 1500, 2100, 3600, 4600, 5600, 7000]
ims = []; t = time.time()
ST = 3100
for f in range(1, 7001):
    k = set()
    if ST <= f < ST + 10 or ST + 200 <= f < ST + 210 or ST + 400 <= f < ST + 410: k.add(START)
    if f > ST + 500:
        if f % 600 < 560: k.add(B)      # boton A Neo Geo = B retropad (clasico)
        if f % 240 < 30: k.add(LEFT)
        if 120 <= f % 240 < 150: k.add(RIGHT)
    e.run(1, k)
    if f in shots: ims.append(e.shot('/tmp/claude-0/fb_%d.png' % f))
print('tiempo', time.time() - t)
w, h = ims[0].size
W = Image.new('RGB', (w * 4, h * 2))
for i, im in enumerate(ims): W.paste(im, ((i % 4) * w, (i // 4) * h))
W.save('/tmp/claude-0/fbgrid.png'); print(w, h)

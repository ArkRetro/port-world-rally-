import sys
from fbemu import *
from PIL import Image
mode, rom, out = sys.argv[1], sys.argv[2], sys.argv[3]
e = Emu(rom, '/tmp/claude-0/fbt2/sys', {'fbneo-neogeo-mode': mode})
ims = []
for f in range(1, 1801):
    e.run(1)
    if f in (60, 150, 300, 600, 1200, 1800): ims.append(e.shot('/tmp/claude-0/x.png').copy())
w, h = ims[0].size
W = Image.new('RGB', (w * 3, h * 2))
for i, im in enumerate(ims): W.paste(im, ((i % 3) * w, (i // 3) * h))
W.save(out)

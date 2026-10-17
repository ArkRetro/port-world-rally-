import sys
from fbemu import *
from PIL import Image
mode = sys.argv[1]
opts = {'fbneo-neogeo-mode': mode}
e = Emu('/tmp/claude-0/fbt2/roms/wrallyng.zip', '/tmp/claude-0/fbt2/sys', opts)
print(mode, 'carga', e.ok)
ims = []
for f in range(1, 2101):
    e.run(1)
    if f in (60, 200, 400, 900, 1500, 2100): ims.append(e.shot('/tmp/claude-0/x.png').copy())
w, h = ims[0].size
W = Image.new('RGB', (w * 3, h * 2))
for i, im in enumerate(ims): W.paste(im, ((i % 3) * w, (i // 3) * h))
W.save('/tmp/claude-0/fbreal_%s.png' % mode)

"""Empaqueta World Rally NG para FBNeo (RomData): wrallyng.zip + wrallyng.dat"""
import zipfile, zlib, os, sys, nggfx
B = '/home/claude/wrally/neo/build/'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/out_ng/fbneo'
os.makedirs(OUT, exist_ok=True)
p = nggfx.swap16(open(B + 'prom.bin', 'rb').read())
s = bytearray(nggfx.font_srom())
if os.path.exists(B + 'eyecatch_fix.bin'):   # textos de la intro Neo Geo (del juego del usuario)
    _f = open(B + 'eyecatch_fix.bin', 'rb').read()
    for _i in range(0, len(_f), 34):
        _t = (_f[_i] << 8) | _f[_i + 1]
        s[_t * 32:_t * 32 + 32] = _f[_i + 2:_i + 34]
import eyetext; eyetext.patch(s)      # 'PORT ARKRETRO' / 'PRO BLOSTE'
s = bytes(s)
m = open(B + 'm1.bin', 'rb').read() if os.path.exists(B + 'm1.bin') else bytes(nggfx.m1_min())
v = open(B + 'v1.bin', 'rb').read() if os.path.exists(B + 'v1.bin') else bytes(0x100000)
c1, c2 = nggfx.split_c(open(B + 'crom.bin', 'rb').read())
files = [('wrallyng-p1.p1', p, '1 BRF_ESS BRF_PRG'), ('wrallyng-s1.s1', s, '2 BRF_GRA'),
         ('wrallyng-c1.c1', c1, '3 BRF_GRA'), ('wrallyng-c2.c2', c2, '3 BRF_GRA'),
         ('wrallyng-m1.m1', m, '4 BRF_ESS BRF_PRG'), ('wrallyng-v1.v1', v, '5 BRF_SND')]
with zipfile.ZipFile(os.path.join(OUT, 'wrallyng.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    for n, d, t in files: z.writestr(n, d)
dat = ['// World Rally (Gaelco 1993) - port a Neo Geo, uso personal',
       '// RomData para FinalBurn Neo: usa el driver "test240p" solo como plantilla de cartucho',
       'ZipName: wrallyng', 'DrvName: test240p', 'FullName: "WRally neogeo final"', '']
for n, d, t in files:
    dat.append('"%s", 0x%08x, 0x%08x, %s' % (n, len(d), zlib.crc32(d) & 0xffffffff, t))
open(os.path.join(OUT, 'wrallyng.dat'), 'w', newline='\r\n').write('\n'.join(dat) + '\n')
print(open(os.path.join(OUT, 'wrallyng.dat')).read())

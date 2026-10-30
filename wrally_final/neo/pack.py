import nggfx, os, sys
B = '/home/claude/wrally/neo/build/'
p = open(B + 'prom.bin', 'rb').read()
c = open(B + 'crom.bin', 'rb').read()
s = bytearray(nggfx.font_srom())
if os.path.exists(B + 'eyecatch_fix.bin'):   # textos de la intro Neo Geo (del juego del usuario)
    _f = open(B + 'eyecatch_fix.bin', 'rb').read()
    for _i in range(0, len(_f), 34):
        _t = (_f[_i] << 8) | _f[_i + 1]
        s[_t * 32:_t * 32 + 32] = _f[_i + 2:_i + 34]
import eyetext; eyetext.patch(s)      # 'PORT ARKRETRO' / 'PRO BLOSTE'
s = bytes(s)
m = bytes(nggfx.m1_min()) if not os.path.exists(B + 'm1.bin') else open(B + 'm1.bin', 'rb').read()
v = open(B + 'v1.bin', 'rb').read() if os.path.exists(B + 'v1.bin') else bytes(0x100000)
nggfx.install('wrallyng', 'World Rally (Neo Geo port)', p, s, m, v, c, '/home/claude/wrally/neo/mame')
nggfx.write_neo(B + 'wrally.neo', 'WRally neogeo final', p, s, m, v, c)
print('ok')

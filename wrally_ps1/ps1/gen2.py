"""Datos de la fase 2/3 (desde las ROM del usuario; nada de esto se distribuye):
 - rom_sw.bin: programa 68000 con las palabras en orden nativo (byte a en a^1)
 - cd/TILES.BIN: los 16384 tiles 16x16 de 4 bits para la VRAM de la PS1, 128 bytes cada uno (16 por sector)
 - cd/LEVELnn.BIN: tiles de los mapas de cada uno de los 12 recorridos
 - cd/RACE.BIN: sprites y tiles sueltos que se usan en carrera (observados en el banco de pruebas)
 - cd/MENU.BIN: lo que se ve fuera de la carrera en el camino con moneda (arranque, menus): se lee al arrancar
 - cd/MENU2.BIN: lo demas de la demostracion, por orden de aparicion (sin lo que dura uno o dos cuadros); se lee
   de fondo despues de arrancar, como RACE.BIN. Los tres tienen que caber en la cache de VRAM
   Paquete: sectores de cabecera (u16 n, u16 tiles[n]) y luego los datos de los tiles, 16 por sector."""
import numpy as np, os, sys, struct, collections
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from paths import rom, work
import mapdec
ROM = open(work('prog.bin'), 'rb').read()
sw = bytearray(len(ROM)); sw[0::2] = ROM[1::2]; sw[1::2] = ROM[0::2]
open('rom_sw.bin', 'wb').write(sw)
T = np.load(work('tiles.npy')).astype(np.uint16)
hw = (T[:, :, 0::4] | (T[:, :, 1::4] << 4) | (T[:, :, 2::4] << 8) | (T[:, :, 3::4] << 12)).astype('<u2')  # 16384x16x4
os.makedirs('cd', exist_ok=True)
open('cd/TILES.BIN', 'wb').write(hw.tobytes())
R = [0x1500a, 0x6d61e, 0x71628, 0x7d646, 0x75632, 0x8565a, 0x81650, 0x89664, 0x7963c, 0x8d66e, 0x91678, 0x95682]
def level_tiles(r1):
    s = set()
    for base, rt in ((0x19014, r1), (0x9968c, r1 + 0x2000)):
        for i in set(np.frombuffer(ROM[rt:rt + 0x2000], dtype='>u2').tolist()):
            a = base + i * 256
            w = np.frombuffer(ROM[a:a + 256], dtype='>u2').astype(np.int64)
            d0, d1 = mapdec.dec_pairs(w[0::2], w[1::2])
            s.update((d0 & 0x3fff).tolist())
    return s
def pack(name, tiles):
    tiles = sorted(tiles)
    ids = struct.pack('<H', len(tiles)) + struct.pack('<%dH' % len(tiles), *tiles)
    ids += b'\0' * (-len(ids) % 2048)
    data = b''.join(hw[t].tobytes() for t in tiles)
    data += b'\0' * (-len(data) % 2048)
    open('cd/' + name, 'wb').write(ids + data)
    return len(tiles), (len(ids) + len(data)) // 2048
lv = [level_tiles(r) for r in R]
extra = [set() for _ in range(12)]
# carrera: sprites y extras observados en el banco de pruebas (cuadros de carrera)
race = set()
lsp = [set() for _ in range(12)]
menu = {}
for fn in (work('tl_nc.bin'), work('tl_co.bin')):
    menu[fn] = mc = collections.Counter(); menu[fn + 'f'] = first = {}
    if not os.path.exists(fn): continue
    r = np.fromfile(fn, '<u4').reshape(-1, 3)
    byf = collections.defaultdict(list)
    for f, t, l in r: byf[int(f)].append((int(t), int(l)))
    for f, lst in sorted(byf.items()):
        l = lst[0][1]
        k = R.index(l) if l in R else -1
        lay = [t for t, _ in lst if t < 16384]
        if k < 0 or not lay or sum(1 for t in lay if t in lv[k]) / len(lay) < 0.6:
            for t in set(t & 0x3FFF for t, _ in lst): mc[t] += 1; first.setdefault(t, f)
            continue
        for t, _ in lst:
            if t >= 16384: lsp[k].add(t & 0x3FFF)
            elif t not in lv[k]: extra[k].add(t)
cnt = collections.Counter(t for s_ in lsp for t in s_)
race = set(t for t, c in cnt.items() if c >= 2)
assert len(race) <= 2000, len(race)          # race_ids[] en main.c
for k in range(12):
    n = pack('LEVEL%02d.BIN' % k, (lv[k] | extra[k] | lsp[k]) - race)
    assert n[0] <= 4000, n                   # packids[] en main.c
    print('LEVEL%02d' % k, n, 'total con RACE', n[0] + len(race))
print('RACE', pack('RACE.BIN', race))
NTS = 5120
co, nc, ncf = menu[work('tl_co.bin')], menu[work('tl_nc.bin')], menu[work('tl_nc.bin') + 'f']
m1 = set(t for t, _ in co.most_common(1280))
m2 = sorted((t for t in nc if t not in m1 and t not in race and nc[t] >= 3), key=lambda t: ncf[t])
m2 = m2[:max(0, min(4000, NTS - 200 - len(race | m1)))]
print('MENU', pack('MENU.BIN', m1), 'MENU2', pack('MENU2.BIN', m2))

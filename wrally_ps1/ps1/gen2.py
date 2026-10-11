"""Datos de la fase 2/3 (desde las ROM del usuario; nada de esto se distribuye):
 - rom_sw.bin: programa 68000 con las palabras en orden nativo (byte a en a^1)
 - cd/TILES.BIN: los 16384 tiles 16x16 de 4 bits para la VRAM de la PS1, 128 bytes cada uno (16 por sector)
 - cd/LEVELnn.BIN: tiles de los mapas de cada uno de los 12 recorridos (marcados con el bit 15 en la cabecera: el port
   mira si el fondo es el mapa para saber si esta en carrera) y lo demas que se ve en su carrera
 - cd/RACE.BIN: sprites que se usan en carrera en cualquier recorrido (observados en el banco de pruebas, tambien
   con la direccion oscilante: choques, derrapes)
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
def pack(name, tiles, mark=()):
    """mark: tiles con el bit 15 en la cabecera (en los de recorrido, los de su mapa)"""
    tiles = sorted(tiles)
    ids = struct.pack('<H', len(tiles)) + struct.pack('<%dH' % len(tiles), *[t | 0x8000 if t in mark else t for t in tiles])
    ids += b'\0' * (-len(ids) % 2048)
    data = b''.join(hw[t].tobytes() for t in tiles)
    data += b'\0' * (-len(data) % 2048)
    open('cd/' + name, 'wb').write(ids + data)
    return len(tiles), (len(ids) + len(data)) // 2048
lv = [level_tiles(r) for r in R]
# carrera: sprites y tiles de las capas fuera del mapa que se ven en los cuadros de carrera del banco de pruebas,
# y en que fraccion de los cuadros de carrera de cada recorrido (la mayor de los registros: lo que sale en la
# demostracion se ve siempre). Registros: attract (tl_nc), partida (tl_co) y partidas con la direccion oscilante
# (tl_sNN: choques, trompos, salidas de la pista)
NTS = 5120
CAP = NTS - 160                              # lo que puede quedar fijo en carrera: el resto, para lo que falte
spc = [collections.Counter() for _ in range(12)]; exc = [collections.Counter() for _ in range(12)]; nrf = [0] * 12
rate = [collections.Counter() for _ in range(12)]; nlog = [collections.Counter() for _ in range(12)]
seen = {0: set(), 1: set()}                  # sprites de carrera vistos sin y con direccion oscilante
menu = {0: collections.Counter(), 1: collections.Counter()}; first = {}   # 0 attract, 1 partidas
logs = [('tl_nc.bin', 0, 0), ('tl_co.bin', 1, 0)] + [(f, 1, 1) for f in sorted(os.listdir(work('.'))) if f.startswith('tl_s') and f.endswith('.bin')]
for fn, coin, steer in logs:
    fn = work(fn)
    if not os.path.exists(fn): continue
    r = np.fromfile(fn, '<u4').reshape(-1, 3)
    byf = collections.defaultdict(list)
    for f, t, l in r: byf[int(f)].append((int(t), int(l)))
    lu = [collections.Counter() for _ in range(12)]; ln = [0] * 12
    for f, lst in sorted(byf.items()):
        l = lst[0][1]
        k = R.index(l) if l in R else -1
        lay = [t for t, _ in lst if t < 16384]
        if k < 0 or not lay or sum(1 for t in lay if t in lv[k]) / len(lay) < 0.6:
            for t in set(t & 0x3FFF for t, _ in lst):
                menu[coin][t] += 1
                if not coin: first.setdefault(t, f)
            continue
        nrf[k] += 1; ln[k] += 1
        for t, _ in lst:
            if t >= 16384: spc[k][t & 0x3FFF] += 1; seen[steer].add(t & 0x3FFF); lu[k][t & 0x3FFF] += 1
            elif t not in lv[k]: exc[k][t] += 1; lu[k][t] += 1
    for k in range(12):
        for t, c in lu[k].items(): rate[k][t] = max(rate[k][t], c / ln[k]); nlog[k][t] += 1
# RACE: sprites que se ven en dos recorridos o mas, y los que solo salen con la direccion oscilante (los del coche
# al chocar o derrapar, que valen para cualquier recorrido)
nlev = collections.Counter(t for c in spc for t in c)
race = set(t for t, c in nlev.items() if c >= 2) | (seen[1] - seen[0])
# cada recorrido con RACE tiene que caber en CAP: fuera lo que menos se usa en el (lo que sale en menos registros y,
# entre eso, en menos cuadros; primero lo suyo, luego de RACE)
lvp = []
for k in range(12):
    use = {t: (nlog[k][t], rate[k][t]) for t in rate[k]}
    own = (lv[k] | set(exc[k]) | set(spc[k])) - race
    over = len(own | race) - CAP
    if over > 0:
        out = sorted((t for t in own if t not in lv[k]), key=lambda t: use.get(t, (0, 0)))[:over]
        own -= set(out); over -= len(out)
        print('LEVEL%02d: fuera %d tiles poco usados (vistos en hasta %d registros y el %.2f%% de los cuadros)' % ((k, len(out)) + ((use[out[-1]][0], 100 * use[out[-1]][1]) if out else (0, 0))))
    if over > 0:
        out = sorted(race, key=lambda t: use.get(t, (0, 0)))[:over]
        race -= set(out)
        print('RACE: fuera %d sprites poco usados en LEVEL%02d' % (len(out), k))
    lvp.append(own)
assert len(race) <= 2000, len(race)          # race_ids[] en main.c
for k in range(12):
    n = pack('LEVEL%02d.BIN' % k, lvp[k] - race, lv[k])
    assert n[0] <= 4000, n                   # lvl_ids[] en main.c
    print('LEVEL%02d' % k, n, 'total con RACE', len(lvp[k] | race))
print('RACE', pack('RACE.BIN', race))
# MENU: lo mas usado fuera de la carrera en las partidas; MENU2: lo demas del attract, por orden de aparicion (sin lo
# que dura uno o dos cuadros). Al arrancar tienen que caber con RACE
co, nc = menu[1], menu[0]
m1 = set(t for t, _ in co.most_common(1280))
m2 = sorted((t for t in nc if t not in m1 and t not in race and nc[t] >= 3), key=lambda t: first[t])
m2 = m2[:max(0, min(4000, NTS - 200 - len(race | m1)))]
print('MENU', pack('MENU.BIN', m1), 'MENU2', pack('MENU2.BIN', m2))

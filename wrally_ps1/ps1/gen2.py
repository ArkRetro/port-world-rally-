"""Datos de la fase 2/3 (desde las ROM del usuario; nada de esto se distribuye):
 - rom_sw.bin: programa 68000 con las palabras en orden nativo (byte a en a^1)
 - cd/TILES.BIN: los 16384 tiles 16x16 de 4 bits para la VRAM de la PS1, 128 bytes cada uno (16 por sector)
 - cd/LEVELnn.BIN: tiles de los mapas de cada uno de los 12 recorridos (marcados con el bit 15 en la cabecera: el port
   mira si el fondo es el mapa para saber si esta en carrera) y lo demas que se ve en su carrera (en los que no salen
   en los registros, lo mas usado del recorrido registrado de mapa mas parecido)
 - cd/RACE.BIN: lo que se ve en carrera en casi todos los rallies (el coche, el marcador) y los sprites que solo salen
   con la direccion oscilante (choques, derrapes), observados en el banco de pruebas
 - cd/MENU.BIN: lo mas visto fuera de la carrera en las partidas con moneda (arranque, menus, mapas de los tramos,
   game over, records): se lee al arrancar
 - cd/MENU2.BIN: lo demas que se ve fuera de la carrera (demostracion, partidas), lo mas visto primero hasta llenar la
   cache con los otros dos (sin lo que dura uno o dos cuadros); se lee de fondo despues de arrancar, como RACE.BIN
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
# demostracion se ve siempre). Registros: attract (tl_nc), partidas con moneda (tl_co en Sanremo, tl_rN en el rally
# del recorrido N hasta volver al attract) y partidas con la direccion oscilante (tl_sNN: choques, trompos, salidas)
NTS = 5120
CAP = NTS - 160                              # lo que puede quedar fijo en carrera: el resto, para lo que falte
N = 16384
lvm = np.zeros((12, N), bool)
for k in range(12): lvm[k, list(lv[k])] = True
spc = np.zeros((12, N), np.int64); exc = np.zeros((12, N), np.int64); nrf = np.zeros(12, np.int64)
rate = np.zeros((12, N)); nlog = np.zeros((12, N), np.int64)
seen = np.zeros((2, N), bool)                # sprites de carrera vistos sin y con direccion oscilante
menu = np.zeros((2, N), np.int64)            # cuadros fuera de la carrera: 0 attract, 1 partidas
for fn in sorted(f for f in os.listdir(work('.')) if f.startswith('tl_') and f.endswith('.bin')):
    coin, steer = fn != 'tl_nc.bin', fn.startswith('tl_s')
    f, t, l = np.fromfile(work(fn), '<u4').reshape(-1, 3).T.astype(np.int64)
    uf, fi, inv = np.unique(f, return_index=True, return_inverse=True)
    kf = np.array([R.index(x) if x in R else -1 for x in l[fi]]); kr = kf[inv]
    lay = t < N; tt = t & (N - 1)
    inm = lay & (kr >= 0) & lvm[np.maximum(kr, 0), tt]
    nl, ni = np.bincount(inv, lay, len(uf)), np.bincount(inv, inm, len(uf))
    rf = (kf >= 0) & (nl > 0) & (ni >= 0.6 * nl); rr = rf[inv]
    for k in set(kf[rf].tolist()):
        sel = rr & (kr == k); n = int(np.sum(rf & (kf == k)))
        cs = np.bincount(tt[sel & ~lay], minlength=N); ce = np.bincount(tt[sel & lay & ~inm], minlength=N)
        spc[k] += cs; exc[k] += ce; nrf[k] += n; seen[int(steer)] |= cs > 0
        rate[k] = np.maximum(rate[k], (cs + ce) / n); nlog[k] += (cs + ce) > 0
    key = np.unique(f[~rr] * N + tt[~rr])        # cada tile una vez por cuadro
    menu[int(coin)] += np.bincount(key % N, minlength=N)
def tl(a): return set(np.nonzero(a)[0].tolist())
# RACE: lo que se ve en carrera en al menos 3 de los 4 rallies (su primer tramo, de las partidas registradas) y los
# sprites que solo salen con la direccion oscilante (los del coche al chocar o derrapar, que valen para cualquiera)
PL = [5, 1, 7, 9]
race = tl(sum((spc[k] + exc[k]) > 0 for k in PL) >= 3) | tl(seen[1] & ~seen[0])
# lo de cada recorrido; los que no salen en los registros (tramos 2 y 3) toman lo mas usado (>= 1 % de los cuadros)
# del registrado con el mapa mas parecido
rec = [k for k in range(12) if nrf[k]]
ext, use = [], []
for k in range(12):
    j = k if nrf[k] else max(rec, key=lambda j: len(lv[k] & lv[j]) / len(lv[k] | lv[j]))
    e = tl(spc[j] + exc[j]) - lv[k]
    if j != k:
        e = set(x for x in e if rate[j][x] >= 0.01)
        print('LEVEL%02d: sin registros, %d tiles de LEVEL%02d' % (k, len(e - race), j))
    ext.append(e); use.append((nlog[j], rate[j]))
# cada recorrido con RACE tiene que caber en CAP: fuera lo que menos se usa en el (lo que sale en menos registros y,
# entre eso, en menos cuadros; primero lo suyo, luego de RACE). Dos pasadas: la primera solo recorta RACE (si el
# mapa y RACE no caben), la segunda reparte con RACE ya fijo
def own_of(k, race):
    own = (lv[k] | ext[k]) - race
    over = len(own | race) - CAP
    out = sorted(own - lv[k], key=lambda t: (use[k][0][t], use[k][1][t]))[:max(over, 0)]
    return own - set(out), over - len(out), out
for k in range(12):
    over = own_of(k, race)[1]
    if over > 0:
        out = sorted(race, key=lambda t: (use[k][0][t], use[k][1][t]))[:over]
        race -= set(out)
        print('RACE: fuera %d tiles poco usados en LEVEL%02d' % (len(out), k))
lvp = []
for k in range(12):
    own, over, out = own_of(k, race)
    assert over <= 0, (k, over)
    if out: print('LEVEL%02d: fuera %d tiles poco usados (vistos en hasta %d registros y el %.2f%% de los cuadros)' % (k, len(out), use[k][0][out[-1]], 100 * use[k][1][out[-1]]))
    lvp.append(own)
assert len(race) <= 2000, len(race)          # race_ids[] en main.c
for k in range(12):
    n = pack('LEVEL%02d.BIN' % k, lvp[k], lv[k])
    assert n[0] <= 4000, n                   # lvl_ids[] en main.c
    print('LEVEL%02d' % k, n, 'total con RACE', len(lvp[k] | race))
print('RACE', pack('RACE.BIN', race))
# MENU: lo mas visto fuera de la carrera en las partidas. MENU2: lo demas que se ve fuera de la carrera (3 cuadros o
# mas), lo mas visto primero, hasta llenar la cache con MENU y RACE (que se lee antes, de fondo, al arrancar) dejando
# 200 huecos
co, nc = menu[1], menu[0]
m1 = set(sorted(tl(co), key=lambda t: -co[t])[:1280])
tot = co + nc
m2 = sorted(tl(tot >= 3) - m1 - race, key=lambda t: (-tot[t], t))
cap = max(0, min(4000, NTS - 200 - len(race | m1)))
print('MENU2: %d de %d (fuera los vistos en menos de %d cuadros)' % (min(cap, len(m2)), len(m2), tot[m2[cap]] if cap < len(m2) else 0))
print('MENU', pack('MENU.BIN', m1), 'MENU2', pack('MENU2.BIN', m2[:cap]))

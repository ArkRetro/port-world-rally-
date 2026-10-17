"""Construye World Rally para Neo Geo: parchea el programa original, genera stubs y empaqueta."""
import struct, pickle, os, subprocess, sys
sys.path.insert(0, '/home/claude/wrally/cov')
import capstone
from capstone import m68k as M
import gcrypt, nggfx

SRC = '/home/claude/wrally/neo/src'
BLD = '/home/claude/wrally/neo/build'
os.makedirs(BLD, exist_ok=True)
D = bytearray(open('/home/claude/wrally/prog.bin', 'rb').read())
ORIG = bytes(D)
R = pickle.load(open('/home/claude/wrally/cov/reloc.pkl', 'rb'))
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
md.detail = True

# 0) sonido: muestras ADPCM-A y controlador Z80
if not os.path.exists(f'{BLD}/v1.bin') or os.environ.get('SND'):
    subprocess.check_call('cd /home/claude/wrally/neo/snd && python3 sndbuild.py', shell=True)
# intro Neo Geo (graficos del logo y melodia extraidos de un juego del usuario, si existen)
EYEC = os.path.exists(f'{BLD}/eyecatch_tiles.bin') and os.path.exists(f'{BLD}/jingle.bin')
JADDR = 0x260000
if EYEC:
    _j = open(f'{BLD}/jingle.bin', 'rb').read()
    _j += b'\x80' * ((-len(_j)) % 256)
    assert (JADDR >> 20) == ((JADDR + len(_j) - 1) >> 20)
    _v = bytearray(open(f'{BLD}/v1.bin', 'rb').read())
    _v[JADDR:JADDR + len(_j)] = _j
    open(f'{BLD}/v1.bin', 'wb').write(_v)
    _cr = bytearray(open(f'{BLD}/crom.bin', 'rb').read())
    _t = open(f'{BLD}/eyecatch_tiles.bin', 'rb').read()
    _cr[0xC000 * 128:0xC000 * 128 + len(_t)] = _t
    open(f'{BLD}/crom.bin', 'wb').write(_cr)
    _js, _je = JADDR >> 8, (JADDR + len(_j) - 1) >> 8
else:
    _js = _je = 0
open('/home/claude/wrally/neo/snd/jingle.inc', 'w').write(
    f'JS_LO equ {_js & 255}\nJS_HI equ {_js >> 8}\nJE_LO equ {_je & 255}\nJE_HI equ {_je >> 8}\n')
subprocess.check_call(f'cd /home/claude/wrally/neo/snd && pasmo z80drv.asm {BLD}/m1drv.bin', shell=True)
_m = open(f'{BLD}/m1drv.bin', 'rb').read()
open(f'{BLD}/m1.bin', 'wb').write(_m + b'\xff' * (0x20000 - len(_m)))
# 1) reubicacion de memoria
for off, (old, new, kind, txt) in R['patches'].items():
    assert struct.unpack('>I', D[off:off+4])[0] == old
    D[off:off+4] = struct.pack('>I', new)
print('reubicaciones', len(R['patches']))

# 2) sitios de escritura cifrada a VRAM
VSITES = sorted(set(int(l.split()[0], 16) for l in open('/home/claude/wrally/neo/data/vw2.txt')) |
                {0xb54a, 0xdc12, 0xe03e, 0xe1f0, 0xe59e, 0xece2, 0x10320, 0x106f4})
def reg(r): return '%' + r
# sitios que escriben datos de tilemap en bloque (sin punteros)
BULK = {0x9916, 0x99b2, 0x9c4e, 0x9cea, 0x9e56, 0x9e76, 0x11d02, 0x11d34, 0x11d70, 0x11d9e, 0x11fe6, 0x11ffc}
OKI_SITES = [0x1402c, 0x142a2, 0x1437a, 0x14394, 0x143a8, 0x221e, 0x2226, 0x2230, 0x2238]
stubs = []
table = []
JSR_SITES = {}
for idx, a in enumerate(VSITES + OKI_SITES):
    i = next(md.disasm(bytes(D[a:a+10]), a))
    if a in OKI_SITES:
        insn = bytes(D[a:a + i.size])
        assert i.mnemonic == 'move.b' and struct.unpack('>I', insn[-4:])[0] == 0x10A00F, (hex(a), i.op_str)
        words = ','.join('0x%04x' % struct.unpack('>H', insn[k:k+2])[0] for k in range(0, len(insn), 2))
        stubs.append(f'''| {a:06x}: {i.mnemonic} {i.op_str} (OKI)
vstub_{idx}:
        .word   {words}
        move.w  %sr,-(%sp)
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        jsr     oki_hook
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        move.w  (%sp)+,%ccr
        jmp     0x{a + i.size:x}
''')
        table.append('vstub_%d' % idx)
        D[a:a+2] = struct.pack('>H', 0xA000 | idx)
        continue
    assert i.mnemonic == 'move.l', (hex(a), i.mnemonic)
    dst = i.operands[1]
    am = dst.address_mode
    insn = bytes(D[a:a + i.size])
    # direccion efectiva destino tras ejecutar
    depth = 0; cut = None
    for k, ch in enumerate(i.op_str):
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        elif ch == ',' and depth == 0: cut = k
    dtxt = i.op_str[cut+1:].strip()
    if am == M.M68K_AM_ABSOLUTE_DATA_LONG:
        ea = 'lea 0x%x,%%a0' % struct.unpack('>I', insn[-4:])[0]
    elif am == M.M68K_AM_REGI_ADDR:
        ea = 'movea.l %s,%%a0' % reg(dtxt.strip('()'))
    elif am == M.M68K_AM_REGI_ADDR_POST_INC:
        ea = 'lea -4(%s),%%a0' % reg(dtxt.strip('()+'))
    elif am == M.M68K_AM_REGI_ADDR_PRE_DEC:
        ea = 'movea.l %s,%%a0' % reg(dtxt.strip('-()'))
    elif am in (M.M68K_AM_AREGI_INDEX_8_BIT_DISP, M.M68K_AM_AREGI_INDEX_BASE_DISP):
        inner = dtxt[dtxt.index('(')+1:dtxt.index(')')].split(', ')
        disp = dtxt[:dtxt.index('(')] or '0'
        disp = int(disp.replace('$', '0x'), 0) if disp != '0' else 0
        ea = 'lea %d(%s,%s),%%a0' % (disp, reg(inner[0]), reg(inner[1]))
    elif am == M.M68K_AM_REGI_ADDR_DISP:
        disp = int(dtxt[:dtxt.index('(')].replace('$', '0x'), 0)
        ea = 'lea %d(%s),%%a0' % (disp, reg(dtxt[dtxt.index('(')+1:-1]))
    else:
        raise SystemExit('modo no soportado %x %s' % (a, i.op_str))
    src = i.operands[0]
    assert src.address_mode not in (M.M68K_AM_PCI_DISP, M.M68K_AM_PCI_INDEX_8_BIT_DISP), hex(a)
    words = ','.join('0x%04x' % struct.unpack('>H', insn[k:k+2])[0] for k in range(0, len(insn), 2))
    if i.size >= 6 and 'sp' not in i.op_str and 'a7' not in i.op_str:
        # cabe un JSR: sin la excepcion de linea A (unos 230 ciclos menos por escritura)
        stubs.append(f'''| {a:06x}: {i.mnemonic} {i.op_str} (jsr)
vstub_{idx}:
        .word   {words}
        move.w  %sr,-(%sp)
        move.b  %d0,0x300001
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        {ea}
        jsr     {'vram_fix_a0' if a in BULK else 'vram_fixp_a0'}
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        move.w  (%sp)+,%ccr
        rts
''')
        JSR_SITES[a] = 'vstub_%d' % idx
        table.append('vstub_%d' % idx)
        D[a:a+6] = bytes.fromhex('4EB900000000')
        for k in range(6, i.size, 2): D[a+k:a+k+2] = b'\x4e\x71'
        continue
    stubs.append(f'''| {a:06x}: {i.mnemonic} {i.op_str}
vstub_{idx}:
        .word   {words}
        move.w  %sr,-(%sp)
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        {ea}
        jsr     {'vram_fix_a0' if a in BULK else 'vram_fixp_a0'}
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        move.w  (%sp)+,%ccr
        jmp     0x{a + i.size:x}
''')
    table.append('vstub_%d' % idx)
    D[a:a+2] = struct.pack('>H', 0xA000 | idx)
open(f'{BLD}/gen_stubs.s', 'w').write('        .text\n        .globl stub_table\n' + ''.join(stubs) +
    '        .align 2\nstub_table:\n' + ''.join('        .long %s\n' % t for t in table))
print('sitios VRAM', len(VSITES), 'OKI', len(OKI_SITES))

# 3) MCU
def patch(a, old, new):
    assert bytes(D[a:a+len(old)]) == old, (hex(a), bytes(D[a:a+len(old)]).hex())
    D[a:a+len(new)] = new
# move.w #1,$fec102 (ya reubicado a 0x10C102) -> jsr mcu_hook ; nop   (direccion se rellena tras enlazar)
MCU_SITE = 0x3634
patch(MCU_SITE, bytes.fromhex('33FC00010010C102'), bytes.fromhex('4EB9000000004E71'))
for a in (0x249C, 0x2812):   # move.w #1,$fef500 -> move.w #$A53E,...
    patch(a, bytes.fromhex('33FC00010010F500'), bytes.fromhex('33FCA53E0010F500'))
# rellenos completos de capa (2048 escrituras cifradas cada uno) -> relleno directo
FILL_SITES = {0x11FDC: 'fill_l1', 0x11FF2: 'fill_l0'}
# copia de listas de sprites (IRQ del juego) -> conversion directa
patch(0x44E6, bytes.fromhex('43F900109006'), bytes.fromhex('4EF900000000'))
# rutinas completas de copia de mapa -> version nativa (sin trampa por long)
MAPC_SITES = {0x98DA: 'map_row_l1', 0x9976: 'map_col_l1', 0x9C12: 'map_row_l0', 0x9CAE: 'map_col_l0'}
for a in MAPC_SITES: patch(a, bytes.fromhex('34390010C256'), bytes.fromhex('4EF900000000'))
# fin de cuadro del bucle principal: "tst.w $FEC1A6 / beq.s *-6" -> jsr frame_hook / nop
patch(0x32B8, bytes.fromhex('4A790010C1A667F8'), bytes.fromhex('4EB9000000004E71'))
# test de RAM del arranque (16 KB x2 con IRQ desactivadas): version que alimenta el watchdog
patch(0x2878, bytes.fromhex('321030BCAAAA'), bytes.fromhex('4EF900000000'))
for a in FILL_SITES:
    patch(a, bytes.fromhex('303C07FF41F9'), bytes.fromhex('4EF900000000'))

# 4) vectores: copiamos los originales y sustituimos los necesarios (se completan tras enlazar)
open(f'{BLD}/game.bin', 'wb').write(D)

# 5) datos: tablas de descifrado y ROM de la MCU
def bsw(v, bits): return gcrypt.bitswap(v, *bits)
sw = bytearray()
for s in range(4):
    for hb in range(2):
        for x in range(256):
            v = (x << 8) if hb == 0 else x
            sw += struct.pack('>H', bsw(v, gcrypt.SW[s]))
fin = bytearray()
FB = (2,6,0,11,14,12,7,10,5,4,8,3,9,1,13,15)
for hb in range(2):
    for x in range(256):
        v = (x << 8) if hb == 0 else x
        fin += struct.pack('>H', bsw(v, FB))
first = bytearray()
for e in range(65536):
    first += struct.pack('>H', gcrypt.decrypt(0, 0, e))
open(f'{BLD}/sw.bin', 'wb').write(sw); open(f'{BLD}/fin.bin', 'wb').write(fin)
open(f'{BLD}/first.bin', 'wb').write(first)
ct = bytearray()
def c5(v4): return (v4 << 1) | (v4 >> 3)
for x in range(4096):
    b, r, g = (x >> 8) & 15, (x >> 4) & 15, x & 15
    R5, G5, B5 = c5(r), c5(g), c5(b)
    ct += struct.pack('>H', ((R5 & 1) << 14) | ((G5 & 1) << 13) | ((B5 & 1) << 12) | ((R5 >> 1) << 8) | ((G5 >> 1) << 4) | (B5 >> 1))
open(f'{BLD}/col.bin', 'wb').write(ct)
mcu = open('/home/claude/wrally/rom/wrdallas.bin', 'rb').read()[:0x8000]
open(f'{BLD}/mcu.bin', 'wb').write(mcu)
# informacion de descifrado que solo depende de la primera palabra (ep) y su descifrado (dp)
def _B(x, n): return (x >> n) & 1
_gi = bytearray()
for ep in range(65536):
    dp = struct.unpack_from('>H', first, ep * 2)[0]
    sw_ = (_B(dp, 8) << 1) | _B(dp, 7)
    ty = (_B(dp, 12) << 1) | _B(dp, 2)
    if ty == 0: k1 = 0x3a
    elif ty == 1: k1 = _B(dp,0) | (_B(dp,1)<<1) | (_B(dp,1)<<2) | (_B(ep,3)<<3) | (_B(ep,8)<<4) | (_B(ep,15)<<5)
    elif ty == 2: k1 = _B(ep,5) | (_B(dp,5)<<1) | (_B(ep,7)<<2) | (_B(ep,3)<<3) | (_B(ep,13)<<4) | (_B(ep,14)<<5)
    else: k1 = _B(ep,0) | (_B(ep,9)<<1) | (_B(ep,6)<<2) | (_B(dp,4)<<3) | (_B(ep,2)<<4) | (_B(dp,11)<<5)
    k1 ^= 0x1f
    if ty == 0: k2 = 0
    elif ty == 1: k2 = _B(dp,2) | (_B(ep,4)<<1) | (_B(dp,14)<<2) | (_B(dp,12)<<4)
    elif ty == 2: k2 = _B(ep,6) | (_B(dp,6)<<1) | (_B(dp,15)<<2) | (_B(dp,7)<<4)
    else: k2 = _B(dp,2) | (_B(dp,9)<<1) | (_B(ep,5)<<2) | (_B(dp,1)<<3) | (_B(ep,10)<<4)
    _gi += struct.pack('>H', sw_ | (ty << 2) | (k1 << 4) | (k2 << 10))
open(f'{BLD}/gdinfo.bin', 'wb').write(_gi)
# mapas de las etapas ya descifrados: las copias de mapa del juego leen de la ROM (datos cifrados);
# guardamos al lado el resultado descifrado de cada long, por paginas de 4 KB (las que usan los mapas)
sys.path.insert(0, '/home/claude/wrally/neo/tools')
import mapdec, numpy as _np
_orig = open('/home/claude/wrally/prog.bin', 'rb').read()
_obs = [int(x, 16) for x in """19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27 29 2a 2e 30 31 32 33 34 39 3a 3b 3c 3d 3e 3f
 40 41 42 43 44 45 46 47 49 4a 4b 4c 4d 4e 4f 50 51 52 53 54 55 56 57 58 59 5a 5b 5c 5d 5e 5f 64 65 66 67 68
 99 9a 9b 9c 9d 9e 9f a0 a1 a2 a3 a5 a6 a7 a8 a9 aa ab ac af b0 b1 b2 b3 b4 b5 b6 b7 ba bb bc bd c1 c2 c3 c4
 c5 c6 c7 c8 c9 ca cb cc ce dd df e1 e2 e4 e5 ea ee ef f0""".split()]
_set = set(_obs)
for _a, _b in zip(_obs, _obs[1:]):
    if _b - _a <= 3: _set.update(range(_a, _b))
# todas las paginas de bloques que referencian las tablas de filas de los 12 niveles
# (bases fijas 0x19014 capa 1 / 0x9968c capa 0; tablas de filas sin cifrar, 8 KB por capa)
for _r1 in (0x1500a, 0x6d61e, 0x71628, 0x7d646, 0x75632, 0x8565a, 0x81650, 0x89664, 0x7963c, 0x8d66e, 0x91678, 0x95682):
    for _base, _rt in ((0x19014, _r1), (0x9968c, _r1 + 0x2000)):
        for _idx in set(_np.frombuffer(_orig[_rt:_rt + 0x2000], dtype='>u2').tolist()):
            _a = _base + _idx * 256
            _set.update(range(_a >> 12, ((_a + 255) >> 12) + 1))
MAP_PAGES = sorted(_set)
_ptab = [0xFFFF] * 256
_mt = bytearray()
for _i, _pg in enumerate(MAP_PAGES):
    _ptab[_pg] = _i
    _w = _np.frombuffer(_orig[_pg * 4096:(_pg + 1) * 4096], dtype='>u2').astype(_np.int64)
    _d0, _d1 = mapdec.dec_pairs(_w[0::2], _w[1::2])
    _o = _np.empty(2048, dtype='>u2'); _o[0::2] = _d0; _o[1::2] = _d1
    _mt += _o.tobytes()
open(f'{BLD}/maptab.bin', 'wb').write(_mt)
open(f'{BLD}/mapptab.bin', 'wb').write(struct.pack('>256H', *_ptab))

# tiles animados del recorrido 0x9E00: las dos tablas (0xF01FC / 0xF05FC, 256 longs) ya descifradas
_sd = bytearray()
for _b in (0xF01FC, 0xF05FC):
    for _i in range(256):
        _sd += struct.pack('>I', gcrypt.dec_long(struct.unpack('>I', ORIG[_b + 4 * _i:_b + 4 * _i + 4])[0]))
open(f'{BLD}/scand.bin', 'wb').write(_sd)
# capa 1 (fondo): en el original el pen 0 es opaco; en Neo Geo es transparente y solo se puede
# tapar con un unico color de fondo. Variante de cada tile que usa el pen 0 y no el 15: pen 0 -> 15,
# dibujada con una paleta gemela (Neo 128+p) igual a la del juego salvo el pen 15 = color del pen 0.
# Van en el C-ROM libre a partir de 0xC100 (0xC000-0xC0FF: intro Neo Geo); tabla tile -> codigo
import gfxconv as _gc
_tt = _np.load(f'{BLD}/tiles.npy')
_u0 = (_tt == 0).reshape(len(_tt), -1).any(1); _u15 = (_tt == 15).reshape(len(_tt), -1).any(1)
_alt = _np.where(_u0 & ~_u15)[0]
assert 0xC100 + len(_alt) <= 0x10000, len(_alt)
_vt = _np.where(_tt[_alt] == 0, 15, _tt[_alt]).astype(_np.uint8)
_cr = bytearray(open(f'{BLD}/crom.bin', 'rb').read())
_cr[0xC100 * 128:(0xC100 + len(_alt)) * 128] = _gc.to_neo(_vt)
open(f'{BLD}/crom.bin', 'wb').write(_cr)
_at = _np.zeros(len(_tt), '>u2'); _at[_alt] = 0xC100 + _np.arange(len(_alt))
open(f'{BLD}/alt1.bin', 'wb').write(_at.tobytes())
print('capa 1: variantes con pen 0 opaco', len(_alt))
open(f'{BLD}/gen_data.s', 'w').write(f'''        .section .rodata
        .globl sw_tab, fin_tab, dec_first, mcu_rom, col_tab, snd_dur
        .align 2
sw_tab: .incbin "{BLD}/sw.bin"
fin_tab: .incbin "{BLD}/fin.bin"
dec_first: .incbin "{BLD}/first.bin"
mcu_rom: .incbin "{BLD}/mcu.bin"
col_tab: .incbin "{BLD}/col.bin"
snd_dur: .incbin "{BLD}/snd_dur.bin"
        .globl spr_xtab, spr_ytab, spr_atab, gd_info, tattr_tab, tile_empty_tab
tile_empty_tab: .incbin "{BLD}/ttr.bin"
tattr_tab: .incbin "{BLD}/tattr.bin"
gd_info: .incbin "{BLD}/gdinfo.bin"
spr_xtab: .incbin "{BLD}/sxt.bin"
spr_ytab: .incbin "{BLD}/syt.bin"
spr_atab: .incbin "{BLD}/sat.bin"
        .globl l1_attab
l1_attab: .incbin "{BLD}/l1at.bin"
        .globl srch_idx
srch_idx: .incbin "{BLD}/srch.bin"
        .globl scan_dec
scan_dec: .incbin "{BLD}/scand.bin"
        .globl alt1_tab
alt1_tab: .incbin "{BLD}/alt1.bin"
        .globl map_ptab, maptab
map_ptab: .incbin "{BLD}/mapptab.bin"
        .section .rodata.zmaptab,"a"
        .balign 4
maptab: .incbin "{BLD}/maptab.bin"
''')
# tablas de sprites (deben coincidir con video.c)
FULLW = int(os.environ.get('FULLW', '1'))
_cx, _tw = (8, 13) if FULLW else (32, 16)
_xt = b''
for w in range(1024):
    x = (w - 15 - _cx) & 0x3FF
    if x >= 1008: x -= 1024
    if FULLW: x = ((x * 13) >> 4) + 10
    _xt += struct.pack('>H', ((x & 0x1FF) << 7) if -_tw < x < 320 else 1)
_yt = b''
for b_ in range(256):
    y = ((240 - b_) & 0xFF) - 20
    _yt += struct.pack('>H', ((((496 - y) & 0x1FF) << 7) | 1) if -16 < y < 224 else 0)
_at = b''
for c in range(32):
    _at += struct.pack('>HH', 0x8000, 64 << 8) if c & 0x10 else struct.pack('>HH', 0, (48 + c) << 8)
open(f'{BLD}/tattr.bin', 'wb').write(b''.join(struct.pack('>H', ((16 + (b & 0x1F)) << 8) | ((b >> 6) & 3)) for b in range(256)))
# busqueda del juego en 0xA07A (registros terminados en 0xFF): indice por tabla y clave (0..63)
_srch = b''
for _T in (0xE15B2, 0xE1A12, 0xE1382, 0xE17E2):
    _idx = [0] * 64; _pos = _T
    for _n in range(400):
        _k = struct.unpack('>H', ORIG[_pos:_pos+2])[0]
        if _k < 64 and not _idx[_k]: _idx[_k] = _pos
        _pos += 2
        while struct.unpack('>H', ORIG[_pos:_pos+2])[0] != 0xFF: _pos += 2
        _pos += 2
        if _pos - _T > 0x1800: break
    _srch += b''.join(struct.pack('>I', v) for v in _idx)
open(f'{BLD}/srch.bin', 'wb').write(_srch)
_l1 = b''.join(_at[(((b_ ^ 0x20) >> 2) & 0x1F) * 4:(((b_ ^ 0x20) >> 2) & 0x1F) * 4 + 4] for b_ in range(256))
open(f'{BLD}/l1at.bin', 'wb').write(_l1)   # lista 1 nativa: atributo por byte bajo de desc0
open(f'{BLD}/sxt.bin', 'wb').write(_xt); open(f'{BLD}/syt.bin', 'wb').write(_yt); open(f'{BLD}/sat.bin', 'wb').write(_at)

import numpy as np
_t = np.load(f'{BLD}/tiles.npy')
BLANK = int(np.where(_t.reshape(len(_t), -1).max(1) == 0)[0][0])
_empty = (_t.reshape(len(_t), -1).max(1) == 0)[:0x4000]
_tb = np.zeros(0x800, np.uint8)
for _i in np.where(_empty)[0]: _tb[_i >> 3] |= 1 << (_i & 7)
open(f'{BLD}/ttr.bin', 'wb').write(_tb.tobytes())
open(f'{SRC}/gen_cfg.h', 'w').write(f'#define BLANK_TILE 0x{BLANK:04x}\n')
# 6) compilar codigo Neo Geo (P2 en 0x200000)
CF = '-m68000 -O2 -fomit-frame-pointer -ffreestanding -nostdlib -fno-builtin -Wall -fno-pic -std=gnu99 -Wno-misleading-indentation' + f' -DFULLW={FULLW}'
objs = []
subprocess.check_call(f'python3 /home/claude/wrally/neo/rc/rc51.py {BLD}/gen_mcu.s', shell=True)
if os.environ.get('MCU_CHK'): CF += ' -DMCU_CHK'
if os.environ.get('FSKIP'): CF += ' -DFSKIP=' + os.environ['FSKIP']
if os.environ.get('TAILN'): CF += ' -DTAILN=' + os.environ['TAILN']
if os.environ.get('NORS'): CF += ' -DNORS'
if os.environ.get('NOLAGFIX'): CF += ' -DNOLAGFIX'
if os.environ.get('TIMERX'): CF += ' -DTIMERX=' + os.environ['TIMERX']
if os.environ.get('DBGSPD'): CF += ' -DDBGSPD -Wa,--defsym,DBGSPD=1'
if os.environ.get('DIAG3'): CF += ' -DDIAG3 -Wa,--defsym,DIAG3=1'
if os.environ.get('DIAG4'): CF += ' -DDIAG4 -Wa,--defsym,DIAG4=1'
if os.environ.get('DIAG5'): CF += ' -DDIAG5'
if os.environ.get('SLOWTEST'): CF += ' -DSLOWTEST=' + os.environ['SLOWTEST']
if EYEC: CF += ' -Wa,--defsym,EYEC=1'
if os.environ.get('PROF'): CF += ' -DPROF -Wa,--defsym,PROF=1'
if os.environ.get('NORF'): CF += ' -Wa,--defsym,NORF=1'
if os.environ.get('NOTSYN'): CF += ' -Wa,--defsym,NOTSYN=1'
if os.environ.get('SRCHCHK'): CF += ' -Wa,--defsym,SRCHCHK=1'
if os.environ.get('CAPCNT'): CF += ' -Wa,--defsym,CAPCNT=1'
if os.environ.get('PROFB'): CF += ' -Wa,--defsym,PROFB=1 -DPROFB_C'
for f in ['shell_wr.s', 'main.c', 'video.c', 'fast.s', 'sound.c', 'crypt.c', 'mcs51.c', f'{BLD}/gen_stubs.s', f'{BLD}/gen_data.s', f'{BLD}/gen_mcu.s']:
    src = f if f.startswith('/') else f'{SRC}/{f}'
    o = f'{BLD}/' + os.path.basename(f).rsplit('.', 1)[0] + '.o'
    subprocess.check_call(f'm68k-linux-gnu-gcc {CF} -c {src} -o {o}', shell=True)
    objs.append(o)
LD = f'''OUTPUT_ARCH(m68k)
ENTRY(user_entry)
MEMORY {{ hdr (rx) : ORIGIN = 0x100, LENGTH = 0x300
         p2 (rx) : ORIGIN = 0x200000, LENGTH = 1M
         ram (rwx) : ORIGIN = 0x10A100, LENGTH = 0x1E00 }}
SECTIONS {{
  .hdr : {{ KEEP(*(.text.header)) }} > hdr
  .text : {{ *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); }} > p2
  .data : {{ *(.data .data.*) . = ALIGN(4); }} > ram AT > p2
  .bss (NOLOAD) : {{ *(.bss .bss.* COMMON) . = ALIGN(4); }} > ram
  /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }}
}}
'''
open(f'{BLD}/wr.ld', 'w').write(LD)
subprocess.check_call(f'm68k-linux-gnu-gcc {CF} -T {BLD}/wr.ld -o {BLD}/wr.elf {" ".join(objs)} -lgcc -Wl,-Map={BLD}/wr.map -Wl,--no-warn-rwx-segments 2>&1 | grep -v GNU-stack || true', shell=True)
syms = {}
for l in subprocess.check_output(f'm68k-linux-gnu-nm {BLD}/wr.elf', shell=True).decode().split('\n'):
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
hdr = subprocess.check_output(f'm68k-linux-gnu-objcopy -O binary -j .hdr {BLD}/wr.elf /dev/stdout', shell=True)
p2 = bytearray(subprocess.check_output(f'm68k-linux-gnu-objcopy -O binary -j .text {BLD}/wr.elf /dev/stdout', shell=True))
assert len(p2) <= 0x100000
dsz = subprocess.check_output(f'm68k-linux-gnu-size -A {BLD}/wr.elf', shell=True).decode()
assert '.data' not in dsz or ' 0 ' in [l for l in dsz.split('\n') if l.startswith('.data')][0], dsz
print('P2 usado %d bytes' % len(p2))

# cabecera y vectores
assert all(b in (0, 0xff) for b in D[0x100:0x400])
D[0x100:0x100 + len(hdr)] = hdr
D[MCU_SITE+2:MCU_SITE+6] = struct.pack('>I', syms['mcu_hook'])
for a, n in FILL_SITES.items(): D[a+2:a+6] = struct.pack('>I', syms[n])
for a, n in MAPC_SITES.items(): D[a+2:a+6] = struct.pack('>I', syms[n])
if not os.environ.get('NOL1'): D[0x457C:0x457C+6] = bytes.fromhex('4EF9') + struct.pack('>I', syms['l1_build'])   # lista 1 nativa
assert D[0xA07A:0xA080] == bytes.fromhex('B051 6700 000E'.replace(' ',''))
if not os.environ.get('NOSRCH'): D[0xA07A:0xA080] = bytes.fromhex('4EF9') + struct.pack('>I', syms['srch_hook'])   # busqueda con indice
for a, n in JSR_SITES.items(): D[a+2:a+6] = struct.pack('>I', syms[n])
assert D[0x9E00:0x9E06] == bytes.fromhex('207C00100000')
if not os.environ.get('NOSCAN'): D[0x9E00:0x9E06] = bytes.fromhex('4EF9') + struct.pack('>I', syms['scan_hook'])   # recorrido de tiles animados
# fundido a un color de todas las paletas (0xAFE0..0xB022, al recolocar la camara tras un choque): el
# juego reescribe despues el mapa entero en el mismo cuadro; se aplican ya las paletas para que no se
# vea el mapa nuevo con los colores viejos (en la recreativa la RAM de paletas se ve al momento)
print('B026', bytes(D[0xB026:0xB02C]).hex())
assert bytes(D[0xB026:0xB028]) == bytes.fromhex('3039')
PALB_SRC = struct.unpack('>I', bytes(D[0xB028:0xB02C]))[0]
D[0xB026:0xB02C] = bytes.fromhex('4EB9') + struct.pack('>I', syms['palb_hook'])
# fin de las rutinas que cargan paletas enteras (inicio de tramo, tras el fundido): paletas ya.
# Trampolines: la instruccion desplazada se copia al trampolin (relleno de NOP en fast.s)
def _tramp(site, n, name):
    o = syms[name] - 0x200000 + 14
    assert bytes(p2[o:o + n]) == bytes.fromhex('4E71' * (n // 2)), name
    p2[o:o + n] = D[site:site + n]
    D[site:site + n] = bytes.fromhex('4EB9') + struct.pack('>I', syms[name]) + bytes.fromhex('4E71' * ((n - 6) // 2))
assert bytes(D[0x8BCC:0x8BCE]) == bytes.fromhex('3039')     # move.w abs.l,d0
_tramp(0x8BCC, 6, 'palt6')
assert bytes(D[0x8860:0x8862]) == bytes.fromhex('23FC')     # move.l #imm,abs.l
_tramp(0x8860, 10, 'palt10')
assert bytes(D[0x124D6:0x124DC]) == bytes.fromhex('51C8FFFA4E75')   # dbra d0,$124d2 / rts
D[0x124D6:0x124DC] = bytes.fromhex('4EF9') + struct.pack('>I', syms['palt_hud'])
# suma de comprobacion de la ROM (0x8900, 94 longs desde 0x9268, cada cuadro): constante
assert bytes(D[0x8900:0x891E]) == bytes.fromhex('428145f900009268203c0000005dd292588a51c8fffa23c10010f5024e75')
# (la suma tiene que ser la del programa ORIGINAL: el juego la compara con la constante de 0xEDEEC en
#  cinco sitios y, si no coincide, activa su proteccion anticopia: el coche pierde agarre y direccion y,
#  pasados unos puntos de control, queda marcado para el resto de la partida. Antes se calculaba
#  sobre el programa ya parcheado)
_ck = sum(struct.unpack('>94I', ORIG[0x9268:0x9268 + 376])) & 0xFFFFFFFF
assert _ck == struct.unpack('>I', ORIG[0xEDEEC:0xEDEF0])[0], hex(_ck)
D[0x8900:0x891E] = (bytes.fromhex('223C') + struct.pack('>I', _ck) + bytes.fromhex('45F9000093E0 203C0000FFFF 23C10010F502 4E75 4E71 4E71'.replace(' ','')))
# generador aleatorio (0x5E16): sin guardar 14 registros (mismos resultados y flags)
assert bytes(D[0x5E16:0x5E3C]) == bytes.fromhex('48e7fefe30390010c2665240c0fc004b80fc01014840534033c00010c2663e004cdf7f7f4e75')
D[0x5E16:0x5E3C] = bytes.fromhex('2F00 30390010C266 5240 C0FC004B 80FC0101 4840 5340 33C00010C266 3E00 201F 4A47 4E75 4E71'.replace(' ',''))
print('sitios VRAM con jsr', len(JSR_SITES))
# otras sumas de comprobacion de la proteccion (sobre el programa: con los parches daban otro valor y el
# juego se saboteaba a si mismo): se cargan los resultados del programa ORIGINAL y los registros
# quedan como al terminar cada bucle
_c1 = sum(struct.unpack('>%dI' % 0xD620, ORIG[0x1900A:0x1900A + 0xD620 * 4])) & 0xFFFFFFFF
_c2 = sum(struct.unpack('>%dI' % 0xA1, ORIG[0xB25E:0xB25E + 0xA1 * 4])) & 0xFFFFFFFF
_c3 = sum(struct.unpack('>%dH' % 0x80000, ORIG[0:0x100000])) & 0xFFFF
def _sumpatch(a, old, new):
    old = bytes.fromhex(old)
    assert bytes(D[a:a + len(old)]) == old, (hex(a), bytes(D[a:a + len(old)]).hex())
    new = new + bytes.fromhex('4E71') * ((len(old) - len(new)) // 2)
    assert len(new) == len(old)
    D[a:a + len(new)] = new
# 0xDA42: clr.l d0 / lea $1900A,a4 / move.l #$2D61F,d1 / add.l (a4)+,d0 / dbra d1  -> 0xFEF80C
_sumpatch(0xDA42, '428049f90001900a223c0002d61fd09c51c9fffc',
          bytes.fromhex('203C') + struct.pack('>I', _c1) + bytes.fromhex('49F90004E88A') + bytes.fromhex('223C0002FFFF'))
# 0x11BE2: clr.l d0 / lea $B25E,a6 / move.l #$A0,d3 / add.l (a6)+,d0 / dbra d3  -> 0xFEF846
_sumpatch(0x11BE2, '42804df90000b25e263c000000a0d09e51cbfffc',
          bytes.fromhex('203C') + struct.pack('>I', _c2) + bytes.fromhex('4DF90000B4E2') + bytes.fromhex('263C0000FFFF'))
# 0x2704: suma de palabras de toda la ROM (0-0xFFFFF) -> d1 (pantalla de arranque, 0xFEF366)
_sumpatch(0x2704, '207c000000004241343c000f303c7fffd25851c8fffc51cafff4',
          bytes.fromhex('207C00100000') + bytes.fromhex('323C') + struct.pack('>H', _c3) + bytes.fromhex('303CFFFF343CFFFF'))
print('sumas de proteccion', hex(_c1), hex(_c2), hex(_c3))
_c4 = sum(struct.unpack('>68I', ORIG[0x8FE6:0x8FE6 + 68 * 4])) & 0xFFFFFFFF
_l4 = ORIG[0x8FE6 + 67 * 4:0x8FE6 + 68 * 4]
# 0xC14A: clr.l d0 / lea $8FE6,a5 / move.l #$43,d4 / move.l (a5)+,d1 / add.l d1,d0 / dbra d4  -> 0xFEF520
_sumpatch(0xC14A, '42804bf900008fe6283c00000043221dd08151ccfffa',
          bytes.fromhex('203C') + struct.pack('>I', _c4) + bytes.fromhex('4BF9000090F6') + bytes.fromhex('78005344') + bytes.fromhex('223C') + _l4)
_c5 = sum(struct.unpack('>20I', ORIG[0xBD2E:0xBD2E + 20 * 4])) & 0xFFFFFFFF
assert _c5 == struct.unpack('>I', ORIG[0xEFEEE:0xEFEF2])[0], hex(_c5)
_l5 = ORIG[0xBD2E + 19 * 4:0xBD2E + 20 * 4]
# 0x8EF4: clr.l $FEF53C / lea $BD2E,a2 / move.l #$13,d3 / move.l (a2)+,d2 / add.l d2,$FEF53C / dbra d3
_sumpatch(0x8EF4, '42b90010f53c45f90000bd2e263c00000013241ad5b90010f53c51cbfff6',
          bytes.fromhex('23FC') + struct.pack('>I', _c5) + bytes.fromhex('0010F53C') + bytes.fromhex('45F90000BD7E') + bytes.fromhex('76005343') + bytes.fromhex('243C') + _l5)
print('sumas de proteccion 2', hex(_c4), hex(_c5))
if os.environ.get('ENDTEST'):     # prueba: la demo empieza por la secuencia final (coche dando vueltas)
    assert bytes(D[0xFC6E:0xFC72]) == bytes.fromhex('6100e806')
    D[0xFC6E:0xFC72] = bytes.fromhex('6100f836')

if os.environ.get('VELFIX'):
    assert bytes(D[0x9168:0x916e]) == bytes.fromhex('42790010f518'), bytes(D[0x9168:0x916e]).hex()
    D[0x9168:0x916e] = bytes.fromhex('4EB9') + struct.pack('>I', syms['stage_init_fix'])
if os.environ.get('DIAG4'):
    for _a, _ln, _h in ((0x9154,10,1),(0x917c,10,2),(0xad96,10,3),(0xff18,10,4),(0x666e,6,5),(0x51e8,6,6),(0x6a2c,6,7),(0x6dac,6,8),(0x734a,6,9)):
        D[_a:_a+_ln] = bytes.fromhex('4EB9') + struct.pack('>I', syms['d4_h%d' % _h]) + bytes.fromhex('4E71') * ((_ln - 6) // 2)
if os.environ.get('DIAG3'):
    assert bytes(D[0x907a:0x9080]) == bytes.fromhex('2039001017b4'), bytes(D[0x907a:0x9080]).hex()
    D[0x907a:0x9080] = bytes.fromhex('4EB9') + struct.pack('>I', syms['dbg_h1'])
    assert bytes(D[0x3e10:0x3e16]) == bytes.fromhex('263900100008'), bytes(D[0x3e10:0x3e16]).hex()
    D[0x3e10:0x3e16] = bytes.fromhex('4EB9') + struct.pack('>I', syms['dbg_h2'])
D[0x32B8+2:0x32B8+6] = struct.pack('>I', syms['frame_hook'])
D[0x2878+2:0x2878+6] = struct.pack('>I', syms['ram_test'])
D[0x44E6+2:0x44E6+6] = struct.pack('>I', syms['spr_hook'])
vec = list(struct.unpack('>64I', ORIG[:256]))
vec[0] = 0x0010FF80
vec[10] = syms['lineA_handler']
vec[25] = syms['vblank_irq']
vec[26] = syms['timer_irq']
vec[27] = syms['irq_ignore']
if os.environ.get('WRDBG'):
    vec[2] = syms['crash_2']; vec[3] = syms['crash_3']; vec[4] = syms['crash_4']
    for k in (5, 6, 7, 8, 9, 11, 24, 28, 29, 30, 31): vec[k] = syms['crash_x']
D[0:256] = struct.pack('>64I', *vec)
P = bytes(D) + p2 + b'\xff' * (0x100000 - len(p2))
open(f'{BLD}/prom.bin', 'wb').write(P)
pickle.dump(syms, open(f'{BLD}/syms.pkl', 'wb'))
print('P listo', len(P))

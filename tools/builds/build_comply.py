#!/usr/bin/env python3
"""World Rally Neo Geo port v6 -> "compliance" build (structural fixes only, game logic untouched).

Usage:  python3 build_comply.py [--src-p1 P1] [--src-neo NEO] [--out-neo OUT.neo] [--item NAME] [--workdir DIR]

Patches (each one asserts the exact original bytes first, so the script refuses to run on another base):
 (a) P1 trampolines: vectors 10 (line-A), 25 (VBlank/IRQ1), 26 (timer/IRQ2), 27 (level 3) point to
     'jmp $target.l' stubs at CPU 0x1000-0x1017 (fixed P1 area, like Kizuna) instead of into the banked P2 window.
 (b) Cartridge header jump table rebuilt as the BIOS expects it (6-byte JMP slots):
        0x122 USER          jmp $000220.l   (v6: 4EFA00FC = jmp (PC)+$FC -> 0x220)
        0x128 PLAYER_START  jmp $000234.l   (v6 packed 4EFA010C at 0x126 -> 0x234 rts; BIOS landed mid-instruction)
        0x12E DEMO_END      jmp $000234.l   (v6 4EFA0104 at 0x12E -> 0x234, worked by accident)
        0x134 COIN_SOUND    jmp $000234.l   (v6: 0x134 = $FFFF -> line-F exception)
     0x132-0x139 were $FF (unused) in v6.
 (c) USER entry 0x226: 'move.w #0,$2FFFF0' (only write to cartridge space) -> 4x NOP.
Nothing else is changed (asserted by a full diff).
"""
import argparse, os, struct, zlib, hashlib, sys

SP = '/tmp/claude-0/-home-user-port-world-rally-/307720d2-25b6-5a9f-8356-703187f15fbf/scratchpad'
LAB = SP + '/lab'
ap = argparse.ArgumentParser()
ap.add_argument('--src-p1', default=f'{LAB}/roms/wrv6/wrv6-p1.p1')
ap.add_argument('--src-neo', default=f'{SP}/rar/WRally_neogeo_final_v6.neo')
ap.add_argument('--out-neo', default=f'{SP}/WRally_v6_comply.neo')
ap.add_argument('--item', default='wrcomp')
ap.add_argument('--workdir', default=f'{LAB}/work/build-comply')
ap.add_argument('--other-roms', default=f'{LAB}/roms/wrv6', help='dir with wrv6-{s1,m1,v1,c1,c2} for MAME symlinks')
ap.add_argument('--no-mame', action='store_true', help='only write the .neo')
a = ap.parse_args()

src = open(a.src_p1, 'rb').read()
assert len(src) == 0x200000, hex(len(src))
A = bytearray(len(src)); A[0::2] = src[1::2]; A[1::2] = src[0::2]      # CPU (big-endian) byte order

def off(addr):                       # CPU address -> offset in the 2 MB P image
    assert addr < 0x100000 or 0x200000 <= addr < 0x300000, hex(addr)
    return addr if addr < 0x100000 else addr - 0x100000
def get(addr, n): return bytes(A[off(addr):off(addr) + n])
def put(addr, b): A[off(addr):off(addr) + len(b)] = b
def r16(addr): return struct.unpack('>H', get(addr, 2))[0]
def r32(addr): return struct.unpack('>I', get(addr, 4))[0]
H = lambda s: bytes.fromhex(s.replace(' ', ''))
L = lambda v: struct.pack('>I', v)
def chk(addr, hexs, what):
    b = H(hexs); g = get(addr, len(b))
    assert g == b, f'{what}: at {addr:06X} expected {b.hex()} found {g.hex()}'

JMPL = 0x4EF9; NOP = H('4E71'); RTS = H('4E75')
changes = []                          # (addr, len, description)

# ---------------- (a) P1 trampolines ----------------
assert all(x == 0xFF for x in get(0x097C, 0x2000 - 0x097C)), 'free area 0x097C-0x1FFF not blank'
TR = {10: (0x1000, 0x2000E0, 'line-A'), 25: (0x1006, 0x200C76, 'VBlank IRQ1'),
      26: (0x100C, 0x200098, 'timer IRQ2'), 27: (0x1012, 0x20008E, 'level-3 IRQ3')}
for vec, (ta, tgt, nm) in TR.items():
    assert r32(vec * 4) == tgt, (vec, hex(r32(vec * 4)))
    put(ta, struct.pack('>H', JMPL) + L(tgt))
    put(vec * 4, L(ta))
    changes.append((ta, 6, f'tramp vec{vec} {nm}: jmp ${tgt:06X}.l'))
    changes.append((vec * 4, 4, f'vector {vec} -> ${ta:06X}'))

# ---------------- (b) header jump table ----------------
chk(0x100, '4E454F2D47454F00', 'NEO-GEO magic')
chk(0x122, '4EFA00FC 4EFA010C 4EFA0108 4EFA0104 FFFFFFFFFFFFFFFF', 'v6 packed jump table + unused bytes')
def pcrel(insn_addr):                 # target of 'jmp (d16,PC)' at insn_addr
    assert r16(insn_addr) == 0x4EFA
    d = struct.unpack('>h', get(insn_addr + 2, 2))[0]
    return insn_addr + 2 + d
v6tgt = [pcrel(x) for x in (0x122, 0x126, 0x12A, 0x12E)]
assert v6tgt == [0x220, 0x234, 0x234, 0x234], [hex(x) for x in v6tgt]
chk(0x220, '13C000300001 33FC0000002FFFF0 4EF90020005A 4E75', 'USER code at 0x220 / rts at 0x234')
SLOTS = [(0x122, v6tgt[0], 'USER'), (0x128, v6tgt[1], 'PLAYER_START'), (0x12E, v6tgt[2], 'DEMO_END'),
         (0x134, v6tgt[3], 'COIN_SOUND')]
for sa, tgt, nm in SLOTS:
    put(sa, struct.pack('>H', JMPL) + L(tgt))
    changes.append((sa, 6, f'header {nm}: jmp ${tgt:06X}.l'))
assert get(0x234, 2) == RTS

# ---------------- (c) remove cartridge-space write in USER ----------------
chk(0x226, '33FC 0000 002F FFF0', "move.w #0,$2FFFF0")
put(0x226, NOP * 4)
changes.append((0x226, 8, 'USER: move.w #0,$2FFFF0 -> 4x nop'))

# ---------------- verify nothing else changed ----------------
out = bytearray(len(A)); out[0::2] = A[1::2]; out[1::2] = A[0::2]      # back to file (word-swapped) order
allowed = set()
for ad, n, _ in changes:
    for i in range(n): allowed.add(off(ad) + i)
diffcpu = [i for i in range(len(A)) if A[i] != src[i ^ 1]]
assert set(diffcpu) <= allowed, [hex(x) for x in sorted(set(diffcpu) - allowed)][:20]
print('CPU-order bytes changed:', len(diffcpu))
for ad, n, d in sorted(changes):
    print(f'  {ad:06X} [{n}] {bytes(src[(off(ad)+i)^1] for i in range(n)).hex()} -> {get(ad, n).hex()}  {d}')

# ---------------- .neo for hardware: only the P region is replaced ----------------
neo = bytearray(open(a.src_neo, 'rb').read())
assert neo[:4] == b'NEO\x01' and struct.unpack('<I', neo[4:8])[0] == 0x200000
assert neo[0x1000:0x1000 + 0x200000] == src, 'source .neo P region != source P1'
neo[0x1000:0x1000 + 0x200000] = out
open(a.out_neo, 'wb').write(neo)
rb = open(a.out_neo, 'rb').read()                 # read-back check: only the P region differs from the source .neo
src_neo = open(a.src_neo, 'rb').read()
assert len(rb) == len(src_neo) and rb[0x1000:0x201000] == bytes(out)
assert rb[:0x1000] == src_neo[:0x1000] and rb[0x201000:] == src_neo[0x201000:]
print(f'wrote {a.out_neo} size={len(neo)} crc32={zlib.crc32(neo):08x} md5={hashlib.md5(neo).hexdigest()}')
print(f'P region crc32={zlib.crc32(out):08x} sha1={hashlib.sha1(out).hexdigest()}')

if a.no_mame: sys.exit(0)
# ---------------- MAME softlist item ----------------
it = a.item; rd = f'{a.workdir}/roms/{it}'; os.makedirs(rd, exist_ok=True)
open(f'{rd}/{it}-p1.p1', 'wb').write(out)
for ext in ['s1', 'm1', 'v1', 'c1', 'c2']:
    d = f'{rd}/{it}-{ext}.{ext}'
    if os.path.lexists(d): os.remove(d)
    os.symlink(f'{a.other_roms}/wrv6-{ext}.{ext}', d)
xml = open(f'{LAB}/hash/neogeo.xml').read()
s = xml.index('<software name="wrv6">'); e = xml.index('</software>', s) + len('</software>')
ent = xml[s:e]
new = ent.replace('name="wrv6"', f'name="{it}"').replace('wrv6-', f'{it}-').replace('World Rally port v6', 'World Rally port v6 comply')
os.makedirs(f'{a.workdir}/hash', exist_ok=True)
hx = xml[:e] + '\n\t' + new + xml[e:]
open(f'{a.workdir}/hash/neogeo.xml', 'w').write(hx)
print(f'MAME item {it}: roms {rd}, hash {a.workdir}/hash/neogeo.xml (contains kiz, wrv6, wrfix, {it})')

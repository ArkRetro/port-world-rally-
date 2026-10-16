#!/usr/bin/env python3
"""P1-native build, stage 2: code AND native data tables in P1; P2 holds only the paged track data.

Starting from the HANDOVER v6 .neo:
  code   0x200000-0x20B4DD (46 302 B)  -> P1 0x020000   (as build_p1_native.py)
  data A 0x20B4DE-0x232FFF (161 KB)    -> P1 0x099000   (native tables: sprite/tile/text tables ...)
  data B 0x233000-0x261103 (184 KB)    -> P1 0x02B500   (more tables + the 512-byte page table 0x260F04)
  page data 0x261104-0x2F4103 (588 KB) -> P2 0x20C000   (converted track tiles, still read from P2)
All three P1 destinations lie in arcade-ROM pages that the port pages out to P2 (0x19000-0x68FFF,
0x99000-0xCEFFF) and that are never read (coverage: 16 min attract + 5 played games).
References are relocated like in stage 1: vectors, P1 jsr/jmp, abs.l operands and immediates in the
native code (per block), the line-A table.  The native data contains no pointers (checked: every
in-range long is sprite/page-index data), so the data blocks move verbatim.
The P2 copy of the old code is kept (dead); the old table area is overwritten by the page data.
--kill-p2 fills the old code copy with ILLEGAL and the unused P2 space with 0xFF (verification builds).

usage: build_p1_native2.py in.neo out.neo [--kill-p2] [--keep-bank-write] [--name "..."]
"""
import argparse, re, struct
import capstone

CODE = (0x200000, 0x20B4DE, 0x020000)
DATA_A = (0x20B4DE, 0x233000, 0x099000)
DATA_B = (0x233000, 0x261104, 0x02B500)
PAGES = (0x261104, 0x2F4104, 0x20C000)
BLOCKS = (CODE, DATA_A, DATA_B, PAGES)
LINEA_TABLE, LINEA_N = 0x20875E, 61
VECTORS = (10, 25, 26, 27)
USER_BANK_WRITE = 0x226


def swap(b):
    o = bytearray(len(b)); o[0::2] = b[1::2]; o[1::2] = b[0::2]
    return bytes(o)


def cpu_off(a):
    return a if a < 0x100000 else a - 0x100000


def reloc(v):
    for lo, hi, dst in BLOCKS:
        if lo <= v < hi:
            return v - lo + dst
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--kill-p2', action='store_true')
    ap.add_argument('--keep-bank-write', action='store_true')
    ap.add_argument('--name', default=None)
    a = ap.parse_args()
    neo = bytearray(open(a.src, 'rb').read())
    psize = struct.unpack('<I', neo[4:8])[0]
    assert psize == 0x200000
    old = bytes(swap(neo[0x1000:0x1000 + psize]))
    img = bytearray(old)

    # sanity: destinations inside the paged-out (dead) arcade pages, not overlapping each other
    dead = [(0x19000, 0x69000), (0x99000, 0xCF000)]
    for lo, hi, dst in (CODE, DATA_A, DATA_B):
        n = hi - lo
        assert any(d0 <= dst and dst + n <= d1 for d0, d1 in dead), 'block %06X does not fit in dead space' % lo
    assert CODE[2] + (CODE[1] - CODE[0]) <= DATA_B[2] and DATA_B[2] + (DATA_B[1] - DATA_B[0]) <= 0x69000

    # 1. reference census in the native code (linear disassembly) + line-A table
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    code = old[cpu_off(CODE[0]):cpu_off(CODE[1])]
    refs = []
    i = 0
    while i < len(code):
        ins = next(md.disasm(code[i:i + 10], CODE[0] + i), None)
        if ins is None:
            i += 2; continue
        b = ins.bytes
        for k in range(2, len(b) - 3, 2):
            v = struct.unpack('>I', b[k:k + 4])[0]
            # the page-data range is only ever referenced through its base (lea $261104); other
            # values in that range are pairs of 16-bit displacements, not addresses
            if reloc(v) is not None and (v < PAGES[0] or v == PAGES[0]):
                refs.append((ins.address + k, v))
        for t in re.findall(r'\$([0-9a-f]+)\(pc', ins.op_str):
            assert CODE[0] <= int(t, 16) < CODE[1], 'PC-relative reference leaves the code block at %06X' % ins.address
        i += ins.size
    for n in range(LINEA_N):
        at = LINEA_TABLE + 4 * n
        v = struct.unpack('>I', old[cpu_off(at):cpu_off(at) + 4])[0]
        assert CODE[0] <= v < CODE[1]
        refs.append((at, v))
    by_block = {}
    for at, v in refs:
        for lo, hi, dst in BLOCKS:
            if lo <= v < hi: by_block[lo] = by_block.get(lo, 0) + 1

    # 2. copy the blocks and relocate the references inside the code copy
    for lo, hi, dst in BLOCKS:
        img[cpu_off(dst):cpu_off(dst) + (hi - lo)] = old[cpu_off(lo):cpu_off(hi)]
    for at, v in refs:
        o = cpu_off(reloc(at))                       # the operand now lives in the P1 copy
        assert struct.unpack('>I', img[o:o + 4])[0] == v
        img[o:o + 4] = struct.pack('>I', reloc(v))

    # 3. P1 side: vectors, jsr/jmp abs.l into the code
    for vec in VECTORS:
        v = struct.unpack('>I', img[vec * 4:vec * 4 + 4])[0]
        assert CODE[0] <= v < CODE[1]
        img[vec * 4:vec * 4 + 4] = struct.pack('>I', reloc(v))
    p1refs = 0
    for k in range(0x200, 0x19000, 2):               # arcade code lives below 0x19000 (plus 0x69000+, checked below)
        v = struct.unpack('>I', img[k:k + 4])[0]
        if CODE[0] <= v < CODE[1] and img[k - 2:k] in (b'\x4e\xb9', b'\x4e\xf9'):
            img[k:k + 4] = struct.pack('>I', reloc(v)); p1refs += 1
    for k in range(0x69000, 0x100000, 2):
        v = struct.unpack('>I', img[k:k + 4])[0]
        if CODE[0] <= v < CODE[1] and img[k - 2:k] in (b'\x4e\xb9', b'\x4e\xf9'):
            img[k:k + 4] = struct.pack('>I', reloc(v)); p1refs += 1
    assert p1refs == 63, p1refs
    assert img[USER_BANK_WRITE:USER_BANK_WRITE + 8] == bytes.fromhex('33fc0000002ffff0')
    if not a.keep_bank_write:
        img[USER_BANK_WRITE:USER_BANK_WRITE + 8] = b'\x4e\x71' * 4

    # 4. P2 layout: old code copy kept, everything else outside the page data cleared
    img[cpu_off(CODE[1]):cpu_off(PAGES[2])] = b'\xff' * (PAGES[2] - CODE[1])
    pend = PAGES[2] + (PAGES[1] - PAGES[0])
    img[cpu_off(pend):0x200000] = b'\xff' * (0x300000 - pend)
    if a.kill_p2:
        img[cpu_off(CODE[0]):cpu_off(CODE[1])] = b'\x4a\xfc' * ((CODE[1] - CODE[0]) // 2)

    neo[0x1000:0x1000 + psize] = swap(img)
    if a.name:
        neo[0x2C:0x2C + 33] = a.name.encode()[:32].ljust(33, b'\0')
    open(a.dst, 'wb').write(neo)
    print('relocated %d code refs (%s), %d P1 jsr/jmp, %d vectors; page data now at P2 %06X-%06X'
          % (len(refs), ', '.join('%06X:%d' % kv for kv in sorted(by_block.items())), p1refs, len(VECTORS), PAGES[2], pend))


if __name__ == '__main__':
    main()

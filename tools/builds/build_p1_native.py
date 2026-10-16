#!/usr/bin/env python3
"""Move the World Rally port's native code from P2 (0x200000-0x20B4DD) into P1, so the
68000 never fetches instructions from the banked PORT window (0x200000-0x2FFFFF), the way
commercial carts (Kizuna) and Teo Tormo's RoboCop conversion work.

Input : a .neo file (HANDOVER v6 layout: P = 2 MB, native code 0x200000-0x20B4DE; the data tables that
        follow it and the rest of P2 stay where they are and are still read from P2, like Kizuna's data).
Output: a .neo file with the block copied to P1 at BASE and every reference relocated:
  * vectors 10/25/26/27 (line-A, VBlank, timer, level 3),
  * every jsr/jmp abs.l in P1 that targets the block,
  * every abs.l / immediate operand inside the block that targets the block (capstone disassembly),
  * the 61-entry line-A dispatch table (0x20875E),
  * the USER-entry bank-register write (move.w #0,$2FFFF0) becomes NOPs.
PC-relative references need no change because the block moves as one unit (the tool checks that
none of them leaves the block).  --kill-p2 overwrites the old P2 copy with ILLEGAL opcodes so that
any missed reference shows up at once in MAME (verification builds only).

usage: build_p1_native.py in.neo out.neo [--base 0x20000] [--kill-p2] [--name "..."]
"""
import argparse, struct, sys
import capstone

BLK_LO, BLK_HI, CODE_END = 0x200000, 0x20B4DE, 0x20B4DE   # code only; data tables stay in P2
LINEA_TABLE, LINEA_N = 0x20875E, 61
VECTORS = (10, 25, 26, 27)
USER_BANK_WRITE = 0x226          # move.w #0,$2FFFF0 (8 bytes)


def swap(b):
    o = bytearray(len(b)); o[0::2] = b[1::2]; o[1::2] = b[0::2]
    return bytes(o)


def cpu_off(a):                  # CPU address -> offset in the 2 MB CPU-order image
    return a if a < 0x100000 else a - 0x100000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--base', type=lambda x: int(x, 0), default=0x20000)
    ap.add_argument('--kill-p2', action='store_true')
    ap.add_argument('--name', default=None)
    ap.add_argument('--keep-bank-write', action='store_true', help='leave move.w #0,$2FFFF0 in place (timing experiments)')
    a = ap.parse_args()

    neo = bytearray(open(a.src, 'rb').read())
    psize = struct.unpack('<I', neo[4:8])[0]
    assert psize == 0x200000, 'expected a 2 MB P-ROM'
    img = bytearray(swap(neo[0x1000:0x1000 + psize]))        # CPU byte order
    base, delta = a.base, a.base - BLK_LO
    blk = bytes(img[cpu_off(BLK_LO):cpu_off(BLK_HI)])
    assert base + len(blk) <= 0x100000 and base % 2 == 0
    inblk = lambda v: BLK_LO <= v < BLK_HI

    # 1. reference census inside the block (linear disassembly of the code part)
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    refs = []                                                  # (operand address, old value)
    code = blk[:CODE_END - BLK_LO]
    i = 0
    while i < len(code):
        ins = next(md.disasm(code[i:i + 10], BLK_LO + i), None)
        if ins is None:
            i += 2; continue
        b = ins.bytes
        for k in range(2, len(b) - 3, 2):
            v = struct.unpack('>I', b[k:k + 4])[0]
            if inblk(v): refs.append((ins.address + k, v))
        if '(pc' in ins.op_str:
            import re
            for t in re.findall(r'\$([0-9a-f]+)\(pc', ins.op_str):
                assert inblk(int(t, 16)), 'PC-relative reference leaves the block at %06X: %s %s' % (ins.address, ins.mnemonic, ins.op_str)
        i += ins.size
    for n in range(LINEA_N):
        at = LINEA_TABLE + 4 * n
        v = struct.unpack('>I', blk[at - BLK_LO:at - BLK_LO + 4])[0]
        assert inblk(v)
        refs.append((at, v))

    # 2. copy the block into P1 and relocate the references inside it
    new = bytearray(blk)
    for at, v in refs:
        o = at - BLK_LO
        assert struct.unpack('>I', new[o:o + 4])[0] == v
        new[o:o + 4] = struct.pack('>I', v + delta)
    old_p1 = bytes(img[base:base + len(new)])
    img[base:base + len(new)] = new

    # 3. P1 side: vectors, jsr/jmp abs.l, other longs that point into the block
    for vec in VECTORS:
        v = struct.unpack('>I', img[vec * 4:vec * 4 + 4])[0]
        assert inblk(v), 'vector %d = %06X' % (vec, v)
        img[vec * 4:vec * 4 + 4] = struct.pack('>I', v + delta)
    p1refs, other = [], []
    for k in range(0x200, 0x100000 - 4, 2):
        if base <= k < base + len(new): continue
        v = struct.unpack('>I', img[k:k + 4])[0]
        if not inblk(v): continue
        op = img[k - 2:k]
        if op in (b'\x4e\xb9', b'\x4e\xf9'):
            p1refs.append((k, v)); img[k:k + 4] = struct.pack('>I', v + delta)
        else:
            other.append((k, v))
    assert img[USER_BANK_WRITE:USER_BANK_WRITE + 8] == bytes.fromhex('33fc0000002ffff0')
    if not a.keep_bank_write:
        img[USER_BANK_WRITE:USER_BANK_WRITE + 8] = b'\x4e\x71' * 4
    if a.kill_p2:
        img[cpu_off(BLK_LO):cpu_off(CODE_END)] = b'\x4a\xfc' * ((CODE_END - BLK_LO) // 2)

    neo[0x1000:0x1000 + psize] = swap(img)
    if a.name:
        neo[0x2C:0x2C + 33] = a.name.encode()[:32].ljust(33, b'\0')
    open(a.dst, 'wb').write(neo)
    print('block %06X-%06X (%d bytes) -> P1 %06X-%06X, delta %+X' % (BLK_LO, BLK_HI, len(blk), base, base + len(blk), delta))
    print('relocated: %d refs inside the block (incl. %d line-A entries), %d jsr/jmp in P1, %d vectors'
          % (len(refs), LINEA_N, len(p1refs), len(VECTORS)))
    print('P1 destination previously held %d non-FF/00 bytes' % sum(1 for x in old_p1 if x not in (0, 0xFF)))
    if other:
        print('WARNING: %d other P1 longs point into the block (left unchanged):' % len(other),
              ' '.join('%06X->%06X' % o for o in other[:20]))


if __name__ == '__main__':
    main()

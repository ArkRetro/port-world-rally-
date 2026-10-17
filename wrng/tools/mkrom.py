#!/usr/bin/env python3
"""mkrom.py - build the World Rally Neo Geo P ROM from the arcade 68k program plus the port runtime.

    mkrom.py stubs <wr_cpu.bin> <absrefs.csv> <out stubs.s>
    mkrom.py build <wr_cpu.bin> <absrefs.csv> <game.elf.bin> <game.sym> <out wrng-p1.p1> [--p1only]

What it does (see analysis/INFORME_68K.md for the addresses):
  1. decrypts the Gaelco "VRAM-encrypted" bulk data in place (metatiles, image blocks, animation
     tables, record table, empty-tile pairs, stage table + stage pointer blocks);
  2. moves the track data block [0x15000, 0xCE88C) to the second megabyte (P2, 0x200000+) and fixes
     the pointers that reach into it (stage blocks, anti-tamper checksum loop, one immediate);
  3. relocates every absolute reference to the arcade hardware regions into Neo Geo work RAM
     (work RAM FEC000 -> 104000, palette 200000 -> 10A000, sprites 440000 -> 108C00, I/O 700000 -> 10B800);
  4. replaces the remaining single-shot "oracle" VRAM writes (encrypted ROM long -> VRAM, read back
     decrypted) by calls to generated stubs that decrypt in software;
  5. applies the small code patches (DS5002FP signature, MCU request -> mcu_frame, palette RAM test);
  6. overlays the runtime (vector table + header at 0, code at RUNTIME_ORG) and recomputes the
     anti-tamper constant.
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis'))
import gaelcrypt  # noqa: E402

# ---- layout (keep in sync with src/ram.i) ----
RAM_WRAM = 0x104000      # arcade FEC000-FEFFFF
RAM_PALRAW = 0x10A000    # arcade 200000-203FFF (only 200000-200FFF really used)
RAM_SPR = 0x108C00       # arcade 440000-440FFF
RAM_IO = 0x10B800        # arcade 700000-7000FF
RUNTIME_ORG = 0x15000

TRACK_LO, TRACK_HI = 0x15000, 0xCE88C          # moved to P2 (maps + metatiles of both layers)
P2_BASE = 0x200000
TRACK_DELTA = P2_BASE - TRACK_LO

# ---- bulk encrypted data: (start, length) in 4-byte pairs ----
DEC_REGIONS = [
    (0x19014, 1268 * 256),          # metatiles layer 1 (max map index 1267)
    (0x9968C, 850 * 256),           # metatiles layer 0 (max map index 849)
    (0xE5674, 336 * 4),             # image blocks layer 1: headers
    (0xE5BB4, 0xEA1B0 - 0xE5BB4),   # image blocks layer 1: data
    (0xEA1BE, 336 * 4),             # image blocks layer 0: headers
    (0xEA6FE, 0xED32A - 0xEA6FE),   # image blocks layer 0: data
    (0xF01FC, 90 * 4),              # tile animation table (class 0x0E00)
    (0xF03FC, 90 * 4),              #   same, x-flipped frames (index | 0x80, used by 0x9E76)
    (0xF05FC, 90 * 4),              # tile animation table (class 0x1080)
    (0xF07FC, 90 * 4),              #   same, x-flipped frames
    (0xEFEF2, 72 * 4),              # default records table
    (0xDF64C, 8),                   # empty tile pairs (layer 1, layer 0)
]
STAGE_TABLE = 0xDFBBC
NSTAGES = 12

# VRAM-writing sites whose source data is decrypted at build time (plain copy into the shadow VRAM)
PLAIN_SITES = {0x9916, 0x99B2, 0x9C4E, 0x9CEA, 0x9E56, 0x9E76, 0x11D34, 0x11D9E, 0x11FE6, 0x11FFC, 0xB334,
               0x3DFE, 0x3E0A, 0x3E24, 0x3E74, 0x11D02, 0x11D70}
# VRAM-writing sites not found by the absolute-reference scan (address register destinations)
EXTRA_SITES = [0xE79E, 0xE7AC, 0x9916, 0x99B2, 0x9C4E, 0x9CEA, 0x9E56, 0x9E76, 0x11D34, 0x11D9E, 0x11FE6, 0x11FFC, 0xB334]
# arcade routines replaced whole by runtime versions (entry patched with jmp); their inner write sites get no stub
DETOURS = {0x11CF4: 'blk_copy_l1', 0x11D62: 'blk_copy_l0', 0x11FDC: 'clear_l1', 0x11FF2: 'clear_l0'}
DETOURED_SITES = {0x11D02, 0x11D34, 0x11D70, 0x11D9E, 0x11FE6, 0x11FFC, 0x9916, 0x99B2, 0x9C4E, 0x9CEA}
# map strip copies (scrolling: one new row / column of cells): the inner block "test C224, store (a0) to the
# cell (a1), step a1" is replaced by a jsr to a native routine (src/glue.s) instead of a trap per cell
STRIP_PATCHES = (
    (0x9904, '3e3900fec224024700f0be7c00906700000422905849', 'strip_h7'),
    (0x99A0, '3e3900fec224024700f0be7c0090670000042290d2fc0100', 'strip_v7'),
    (0x9C3C, '3c3900fec224024600f0bc7c00906700000422905849', 'strip_h6'),
    (0x9CD8, '3c3900fec224024600f0bc7c0090670000042290d2fc0100', 'strip_v6'),
)
# stubs whose decrypted value is a RAM pointer that must be relocated too
STUB_RELOC_RAM = {0xC33C, 0xC346, 0xB286}

ANTITAMPER_LEA = 0xDA44        # lea $1900a,a4
ANTITAMPER_CONST = 0xF0A00     # compared with the sum of 0xD620 longs from 0x1900A
ANTITAMPER_START, ANTITAMPER_LONGS = 0x1900A, 0xD620
IMM_IN_TRACK = [0xCCF6 + 2]    # move.l #$8005c,$fef52a : immediate operand at pc+2


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def put32(b, o, v):
    struct.pack_into('>I', b, o, v & 0xFFFFFFFF)


def decrypt_region(rom, start, length):
    assert length % 4 == 0
    for o in range(start, start + length, 4):
        put32(rom, o, gaelcrypt.decrypt_long(be32(rom, o)))


def load_absrefs(path):
    return list(csv.DictReader(open(path)))


def reloc_target(region, addr):
    if region == 'RAM':
        return addr - 0xFEC000 + RAM_WRAM
    if region in ('PALETTE', 'PAL'):
        return addr - 0x200000 + RAM_PALRAW
    if region in ('SPRITES', 'SPR'):
        return addr - 0x440000 + RAM_SPR
    if region == 'IO':
        return addr - 0x700000 + RAM_IO
    return None


# ------------------------------------------------------------------------------------------
# stubs
# ------------------------------------------------------------------------------------------
def stub_sites(rom, refs):
    sites = []
    for r in refs:
        if r['region'] == 'VRAM' and r['access'] == 'w' and r['mnemonic'] == 'move.l':
            sites.append(int(r['pc'], 16))
    sites += EXTRA_SITES
    return sorted(set(sites) - DETOURED_SITES)


def decode_site(md, rom, pc):
    insn = next(md.disasm(bytes(rom[pc:pc + 12]), pc))
    assert insn.mnemonic == 'move.l', (hex(pc), insn.mnemonic)
    return insn


def gen_stubs(rom, refs, out):
    """Every write of a tile pair to the arcade VRAM goes through a stub: optional software decryption
    (sites that read encrypted ROM longs the hardware would decrypt), then vram_store (shadow VRAM +
    write-through to the Neo Geo sprite column showing that tile).  Sites of 6+ bytes are patched with
    `jsr stub`; 2/4-byte sites with `trap #1` and dispatched by return address (trap_table)."""
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
    from capstone import m68k as M
    md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
    md.detail = True
    areg = lambda i: 'a%d' % (i - M.M68K_REG_A0)
    lines = ['* stubs.s - generated by tools/mkrom.py: hooks for every arcade VRAM write',
             '        .include "neogeo.i"', '        .include "ram.i"', '        .text',
             '        .global trap_table']
    traps = []
    for pc in stub_sites(rom, refs):
        insn = decode_site(md, rom, pc)
        src, dst = insn.operands
        post = []

        def ea(op, is_dst):
            if op.type == M.M68K_OP_REG:
                assert not is_dst
                return insn.reg_name(op.reg)
            if op.type == M.M68K_OP_IMM:
                assert not is_dst
                return '#0x%X' % (op.imm & 0xFFFFFFFF)
            am = op.address_mode
            if am == M.M68K_AM_REGI_ADDR:
                return '(%s)' % areg(op.imm)
            if am == M.M68K_AM_REGI_ADDR_POST_INC:
                post.append('        addq.l  #4,%s' % areg(op.imm))
                return '(%s)' % areg(op.imm)
            if am == M.M68K_AM_REGI_ADDR_DISP:
                return '%d(%s)' % (op.mem.disp, insn.reg_name(op.mem.base_reg))
            if am in (M.M68K_AM_AREGI_INDEX_8_BIT_DISP, M.M68K_AM_AREGI_INDEX_BASE_DISP):
                return '%d(%s,%s.%s)' % (op.mem.disp, insn.reg_name(op.mem.base_reg),
                                         insn.reg_name(op.mem.index_reg), 'l' if op.mem.index_size else 'w')
            if am in (M.M68K_AM_ABSOLUTE_DATA_LONG, M.M68K_AM_ABSOLUTE_DATA_SHORT):
                if is_dst:
                    assert 0x100000 <= op.imm < 0x104000, hex(pc)
                return '0x%X' % op.imm
            raise SystemExit('unsupported addressing mode %d at %06X' % (am, pc))

        s_src = ea(src, False)
        s_dst = ea(dst, True)
        decrypt = pc not in PLAIN_SITES
        lines.append('        .global stub_%06X' % pc)
        lines.append('stub_%06X:                     /* %s %s (%d bytes, %s) */' % (
            pc, insn.mnemonic, insn.op_str, insn.size, 'decrypt' if decrypt else 'plain'))
        lines.append('        movem.l d0-d2/a0-a2,-(sp)')
        lines.append('        move.l  %s,d2' % s_src)
        if decrypt:
            lines.append('        move.l  d2,d0')
            lines.append('        jsr     decrypt_long')
            if pc in STUB_RELOC_RAM:
                lines.append('        bsr     reloc_ram_ptr')
            lines.append('        move.l  d0,d2')
        lines.append('        lea     %s,a0' % s_dst)
        lines.append('        move.l  d2,d0')
        lines.append('        jsr     vram_store')
        lines.append('        movem.l (sp)+,d0-d2/a0-a2')
        lines += post
        lines.append('        rts')
        if insn.size < 6:
            traps.append((pc, insn.size))
    lines += ['',
              '* reloc_ram_ptr: d0 = decrypted long; if it is an arcade work-RAM address, relocate it',
              'reloc_ram_ptr:',
              '        cmpi.l  #0xFEC000,d0', '        bcs     1f', '        cmpi.l  #0xFF0000,d0', '        bcc     1f',
              '        sub.l   #0xFEC000-ARC_WRAM,d0', '1:      rts', '',
              '* trap_table: (return PC after the trap #1, stub, bytes to skip after the trap opcode)',
              '        .align  2', 'trap_table:']
    for pc, size in traps:
        lines.append('        .long   0x%06X, stub_%06X, %d' % (pc + 2, pc, size - 2))
    lines.append('        .long   0, 0, 0')
    open(out, 'w').write('\n'.join(lines) + '\n')
    print('stubs:', len(stub_sites(rom, refs)), 'traps:', len(traps))


# ------------------------------------------------------------------------------------------
# build
# ------------------------------------------------------------------------------------------
def load_syms(path):
    syms = {}
    for line in open(path):
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def build(rom_path, refs_path, rt_bin, sym_path, out, p1only=False):
    orig = open(rom_path, 'rb').read()
    assert len(orig) == 0x100000
    rom = bytearray(orig)
    refs = load_absrefs(refs_path)
    syms = load_syms(sym_path)
    rt = open(rt_bin, 'rb').read()

    # 1. bulk decryption
    for start, length in DEC_REGIONS:
        decrypt_region(rom, start, length)
    stage_blocks = set()
    for i in range(NSTAGES):
        e = STAGE_TABLE + 16 * i
        for off in (4, 8, 12):
            put32(rom, e + off, gaelcrypt.decrypt_long(be32(orig, e + off)))
        stage_blocks.add(be32(rom, e + 4))
    for blk in sorted(stage_blocks):
        for off in range(0, 16, 4):
            v = gaelcrypt.decrypt_long(be32(orig, blk + off))
            if not p1only and TRACK_LO <= v < TRACK_HI:
                v += TRACK_DELTA
            put32(rom, blk + off, v)
    print('stage blocks:', ' '.join('%X' % b for b in sorted(stage_blocks)))

    # 2. track data -> P2
    p2 = bytearray(b'\xFF' * 0x100000)
    if not p1only:
        p2[0:TRACK_HI - TRACK_LO] = rom[TRACK_LO:TRACK_HI]
        rom[TRACK_LO:TRACK_HI] = b'\xFF' * (TRACK_HI - TRACK_LO)
        # lea $1900a,a4 -> P2
        assert rom[ANTITAMPER_LEA:ANTITAMPER_LEA + 6] == bytes.fromhex('49F90001900A')
        put32(rom, ANTITAMPER_LEA + 2, ANTITAMPER_START + TRACK_DELTA)
        for o in IMM_IN_TRACK:
            v = be32(rom, o)
            assert TRACK_LO <= v < TRACK_HI, hex(v)
            put32(rom, o, v + TRACK_DELTA)

    # 3. relocations
    nrel = 0
    for r in refs:
        region = r['region']
        tgt = reloc_target(region, int(r['address'], 16))
        if tgt is None:
            continue
        pc = int(r['pc'], 16)
        off = pc + int(r['operand_offset'])
        kind = r['kind']
        if kind in ('absL', 'imm', 'imm32'):
            assert be32(rom, off) == int(r['address'], 16), (r['pc'], r['address'], hex(be32(rom, off)))
            put32(rom, off, tgt)
            nrel += 1
        else:
            raise SystemExit('unexpected operand kind %s at %s' % (kind, r['pc']))
    print('relocated operands:', nrel)

    # 4. oracle stubs
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
    md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
    for pc in stub_sites(orig, refs):
        insn = decode_site(md, orig, pc)
        size = insn.size
        if size >= 6:
            addr = syms['stub_%06X' % pc]
            patch = bytes.fromhex('4EB9') + struct.pack('>I', addr) + b'\x4E\x71' * ((size - 6) // 2)
        else:
            patch = bytes.fromhex('4E41') + b'\x4E\x71' * ((size - 2) // 2)       # trap #1
        assert len(patch) == size
        rom[pc:pc + size] = patch

    for pc, name in DETOURS.items():
        rom[pc:pc + 6] = bytes.fromhex('4EF9') + struct.pack('>I', syms[name])

    for pc, expect, name in STRIP_PATCHES:
        n = len(expect) // 2
        assert orig[pc:pc + n] == bytes.fromhex(expect), hex(pc)
        rom[pc:pc + n] = bytes.fromhex('4EB9') + struct.pack('>I', syms[name]) + b'\x4E\x71' * ((n - 6) // 2)

    # 4a. the arcade's "wait for the interrupt tick" loops (tst.w $FEC1A6 / beq.s *-6) -> jsr main_wait (src/glue.s):
    #     a tick that arrives in the middle of a frame is held back until the frame ends; the sound sequencer
    #     call of the interrupt handler moves to the VBlank (it keeps running every frame)
    nwait = 0
    for r in refs:
        if r['address'] == 'FEC1A6' and r['mnemonic'] == 'tst.w':
            pc = int(r['pc'], 16)
            assert rom[pc:pc + 8] == bytes.fromhex('4A79') + struct.pack('>I', reloc_target('RAM', 0xFEC1A6)) + bytes.fromhex('67F8'), hex(pc)
            rom[pc:pc + 8] = bytes.fromhex('4EB9') + struct.pack('>I', syms['main_wait']) + bytes.fromhex('4E71')
            nwait += 1
    assert nwait == 26, nwait
    assert rom[0x2FFE:0x3004] == bytes.fromhex('4EB900014122')
    rom[0x2FFE:0x3004] = bytes.fromhex('4E71') * 3
    # 4a1. 0x44E6 (arcade interrupt: copy the sprite lists to sprite RAM) -> converted straight into the
    #      Neo Geo sprite pools (src/video.s spr_lists)
    assert rom[0x44E6:0x44EC] == bytes.fromhex('43F9') + struct.pack('>I', reloc_target('SPR', 0x440006)), 'sprite copy 0x44E6'
    rom[0x44E6:0x44EC] = bytes.fromhex('4EF9') + struct.pack('>I', syms['spr_lists'])
    # 4a2. faster equivalents of two arcade routines that the race calls very often (same registers and flags):
    #  0xA07A search of a record by key -> indexed (src/glue.s srch_hook)
    assert rom[0xA07A:0xA080] == bytes.fromhex('B0516700000E'), 'search 0xA07A'
    rom[0xA07A:0xA080] = bytes.fromhex('4EF9') + struct.pack('>I', syms['srch_hook'])
    #  0x5E16 random number generator: it saved and restored 14 registers around a mulu/divu that only uses d0
    #  (movem leaves the flags of "move.w d0,d7": tst.w d7 gives the same N/Z, V=C=0)
    seed = struct.pack('>I', reloc_target('RAM', 0xFEC266))
    assert rom[0x5E16:0x5E3C] == (bytes.fromhex('48E7FEFE3039') + seed + bytes.fromhex('5240C0FC004B80FC01014840534033C0')
                                  + seed + bytes.fromhex('3E004CDF7F7F4E75')), 'random 0x5E16'
    rom[0x5E16:0x5E3C] = (bytes.fromhex('2F003039') + seed + bytes.fromhex('5240C0FC004B80FC01014840534033C0') + seed
                          + bytes.fromhex('3E00201F4A474E754E71'))
    # 4b. sound: OKI register writes -> Z80 command hooks (src/sound.s)
    for pc, name, expect in ((0x1402C, 'snd_hook_stopall', '13FC00780070000F'), (0x1437A, 'snd_hook_stop0', '13C00070000F'),
                             (0x142A2, 'snd_hook_stop1', '13C10070000F'), (0x14386, 'snd_hook_bank', '13C40070000D'),
                             (0x14394, 'snd_hook_phrase', '13C10070000F'), (0x143A8, 'snd_hook_play', '13C10070000F')):
        n = len(expect) // 2
        assert orig[pc:pc + n] == bytes.fromhex(expect), hex(pc)
        rom[pc:pc + n] = bytes.fromhex('4EB9') + struct.pack('>I', syms[name]) + b'\x4E\x71' * ((n - 6) // 2)

    # 4c. checksums of the arcade code: the port patches the summed ranges, and a wrong sum arms Gaelco's
    #     sabotage flags (FEF520 -> FEC154/FEF4BB/FEEAC6/FEF4DE/FEF38A, FEF53C, FEF502, FEF846; among the effects:
    #     jumps whose "gravity" is the car speed, a car that steers by itself and ignores the road edges).  The loops are replaced by the
    #     sums of the original bytes; the registers they leave are not used afterwards.
    def sum32(start, nlongs):
        return sum(be32(orig, start + 4 * i) for i in range(nlongs)) & 0xFFFFFFFF
    # 0xC14A: clr.l d0 ; lea $8FE6,a5 ; move.l #67,d4 ; loop: move.l (a5)+,d1 ; add.l d1,d0 ; dbra d4
    assert orig[0xC14A:0xC160] == bytes.fromhex('42804BF900008FE6283C00000043221DD08151CCFFFA'), 'checksum 0xC14A'
    rom[0xC14A:0xC160] = (bytes.fromhex('203C') + struct.pack('>I', sum32(0x8FE6, 68))         # move.l #sum,d0
                          + bytes.fromhex('4BF9') + struct.pack('>I', 0x8FE6 + 68 * 4)       # lea end,a5
                          + bytes.fromhex('283C0000FFFF') + b'\x4E\x71' * 2)                # move.l #$FFFF,d4
    # 0x8EF4: clr.l $FEF53C ; lea $BD2E,a2 ; move.l #19,d3 ; loop: move.l (a2)+,d2 ; add.l d2,$FEF53C ; dbra d3
    assert orig[0x8EF4:0x8F12] == bytes.fromhex('42B900FEF53C45F90000BD2E263C00000013241AD5B900FEF53C51CBFFF6'), \
        'checksum 0x8EF4'
    rom[0x8EF4:0x8F12] = (bytes.fromhex('23FC') + struct.pack('>II', sum32(0xBD2E, 20), reloc_target('RAM', 0xFEF53C))
                          + bytes.fromhex('45F9') + struct.pack('>I', 0xBD2E + 20 * 4)       # lea end,a2
                          + bytes.fromhex('263C0000FFFF')                                    # move.l #$FFFF,d3
                          + bytes.fromhex('243C') + orig[0xBD2E + 19 * 4:0xBD2E + 20 * 4]    # move.l #last,d2
                          + b'\x4E\x71')
    # 0x8900: clr.l d1 ; lea $9268,a2 ; move.l #93,d0 ; loop: add.l (a2),d1 ; addq.l #4,a2 ; dbra d0  -> FEF502
    assert orig[0x8900:0x8916] == bytes.fromhex('428145F900009268203C0000005DD29258 8A51C8FFFA'.replace(' ', '')), \
        'checksum 0x8900'
    rom[0x8900:0x8916] = (bytes.fromhex('223C') + struct.pack('>I', sum32(0x9268, 94))       # move.l #sum,d1
                          + bytes.fromhex('45F9') + struct.pack('>I', 0x9268 + 94 * 4)    # lea end,a2
                          + bytes.fromhex('203C0000FFFF') + b'\x4E\x71' * 2)            # move.l #$FFFF,d0
    # 0x11BE2: clr.l d0 ; lea $B25E,a6 ; move.l #160,d3 ; loop: add.l (a6)+,d0 ; dbra d3  -> FEF846
    assert orig[0x11BE2:0x11BF6] == bytes.fromhex('42804DF90000B25E263C000000A0D09E51CBFFFC'), 'checksum 0x11BE2'
    rom[0x11BE2:0x11BF6] = (bytes.fromhex('203C') + struct.pack('>I', sum32(0xB25E, 161))      # move.l #sum,d0
                            + bytes.fromhex('4DF9') + struct.pack('>I', 0xB25E + 161 * 4)    # lea end,a6
                            + bytes.fromhex('263C0000FFFF') + b'\x4E\x71')                 # move.l #$FFFF,d3
    # 0x2704 (self test "ROM Checksum"): word sum of the whole 1 MB program
    assert orig[0x2704:0x271E] == bytes.fromhex('207C000000004241343C000F303C7FFFD25851C8FFFC51CAFFF4'), 'checksum 0x2704'
    wsum = sum(struct.unpack('>524288H', bytes(orig[:0x100000]))) & 0xFFFF
    rom[0x2704:0x271E] = bytes.fromhex('323C') + struct.pack('>H', wsum) + b'\x4E\x71' * 11
    assert len(rom) == 0x100000, 'a checksum patch changed the ROM size'

    # 5. code patches
    for pc in (0x249C, 0x2812):                      # move.w #1,$FEF500 -> move.w #$A53E,...  (MCU signature)
        assert rom[pc:pc + 4] == bytes.fromhex('33FC0001'), hex(pc)
        rom[pc + 2:pc + 4] = bytes.fromhex('A53E')
    assert rom[0x3634:0x3638] == bytes.fromhex('33FC0001')      # move.w #1,$FEC102 (MCU request)
    rom[0x3634:0x363C] = bytes.fromhex('4EB9') + struct.pack('>I', syms['mcu_frame']) + b'\x4E\x71'
    assert rom[0x275C:0x2760] == bytes.fromhex('6100011A')      # bsr $2878 (palette RAM test)
    rom[0x275C:0x2760] = b'\x4E\x71\x4E\x71'

    # 6. runtime overlay: vectors+header (0x000-0x3FF) and the code at RUNTIME_ORG
    assert rt[0x100:0x107] == b'NEO-GEO'
    rom[0:0x400] = rt[0:0x400]
    code = rt[RUNTIME_ORG:]
    code = code.rstrip(b'\xFF') if False else code
    assert all(b == 0 for b in rt[0x400:RUNTIME_ORG]), 'runtime image has data between the header and RUNTIME_ORG'
    end = RUNTIME_ORG + len(code)
    assert end <= (TRACK_HI if not p1only else 0x15000), 'runtime too big: ends at %X' % end
    rom[RUNTIME_ORG:end] = code
    print('runtime: %X-%X (%d bytes)' % (RUNTIME_ORG, end, len(code)))

    # 7. anti-tamper constant: sum of 0xD620 longs starting at the (new) address of 0x1900A
    if p1only:
        data = rom
        base = ANTITAMPER_START
    else:
        data = p2
        base = ANTITAMPER_START - TRACK_LO
    s = 0
    for o in range(base, base + ANTITAMPER_LONGS * 4, 4):
        s = (s + be32(data, o)) & 0xFFFFFFFF
    put32(rom, ANTITAMPER_CONST, s)
    print('anti-tamper sum: %08X' % s)

    # 8. program size in the header
    put32(rom, 0x10A, 0x100000 if p1only else 0x200000)
    open(out, 'wb').write(bytes(rom) + (b'' if p1only else bytes(p2)))
    print('wrote', out, len(rom) + (0 if p1only else len(p2)), 'bytes')


def main():
    a = sys.argv[1:]
    if a and a[0] == 'stubs':
        rom = open(a[1], 'rb').read()
        gen_stubs(rom, load_absrefs(a[2]), a[3])
    elif a and a[0] == 'build':
        build(a[1], a[2], a[3], a[4], a[5], p1only='--p1only' in a)
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == '__main__':
    main()

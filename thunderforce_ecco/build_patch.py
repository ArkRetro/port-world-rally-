"""Lightening Force (USA): replace the player ship with Ecco's dolphin, demo mode only.

Usage: python3 build_patch.py <tf4.bin> <ecco frames dir | frame png> <out.bin> [out.ips] [--everywhere] [--intro DIR] [--pixels]
--everywhere: use the dolphin in the whole game, not only the first demo.
--pixels: the SEGA logo at power-on reads PIXELS (same style and animation).
--intro DIR: when the game's intro shows the Rynex, play the human-to-dolphin transformation
  (intro-transformacion folder) over it; the game waits and the music keeps playing.
- Ship tiles: ROM archive 0x90000 entry 3, unpacked to RAM FF0800; bytes 0x000-0x6FF
  (FF0800-FF0EFF) are the 7 ship frames (4x2 tiles each, full up to full down),
  DMA'd to VRAM tiles 0x440-0x477.
- Demo flag: word $FFF2FE (non-zero = demo, 0 = real game). Stage: word $FFF0B8.
  The demo cycles through stages 0-3 ($FFF074 counter, starts at 3 so the first demo
  after power-on is stage 0); the dolphin is only used when the demo plays stage 0.
- Hook: ROM 0x000B72 'lea $90000.l,a0' (right after unpacking entry 3) becomes
  'jsr $100000.l'; the new routine overwrites FF0800.. with the dolphin when the
  demo flag is set and the stage is 0, then redoes the lea and returns.
"""
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image

HOOK = 0xB72
NEW = 0x100000
SHIP_RAM = 0xFF0800   # ship frames: 7 x (4x2 tiles), VRAM tiles 0x440-0x477
NTILES = 56
# Ecco frame used for each ship frame, from full up (0x440) to full down (0x470)
FRAMES = [5, 4, 1, 0, 3, 27, 24]
# Extra scale over 'fits the 32x16 box'; anything past the box is clipped
SCALE = 1.03
# Palette line 0 of the stage (CRAM, 0000BBB0GGG0RRR0 -> 3-bit channels)
PAL0 = [(4,7,6),(0,0,0),(0,1,5),(2,3,7),(5,5,7),(6,6,7),(4,0,0),(7,0,0),
        (7,4,1),(7,7,1),(5,5,0),(0,0,0),(0,1,2),(1,2,3),(3,4,5),(7,7,7)]
PAL0 = [tuple(c*36 for c in p) for p in PAL0]
USABLE = [1,2,3,4,5,12,13,14,15]  # blues/greys/black/white; skip reds/yellows

def nearest(rgb):
    return min(USABLE, key=lambda i: sum((a-b)**2 for a, b in zip(rgb, PAL0[i])))

def frame_tiles(png):
    src = Image.open(png).convert('RGBA')
    k = min(32 / src.width, 16 / src.height) * SCALE
    w = max(1, round(src.width * k)); h = max(1, round(src.height * k))
    small = src.resize((w, h), Image.LANCZOS)
    canvas = Image.new('RGBA', (32, 16), (0, 0, 0, 0))
    canvas.paste(small, ((32 - w) // 2, (16 - h) // 2))  # negative offsets clip evenly
    px = [[0]*32 for _ in range(16)]
    for y in range(16):
        for x in range(32):
            r, g, b, a = canvas.getpixel((x, y))
            px[y][x] = nearest((r, g, b)) if a >= 128 else 0
    add_cannons(px)
    out = bytearray()
    for cx in range(4):            # VDP sprite tiles go column by column
        for cy in range(2):
            for y in range(8):
                row = px[cy*8 + y][cx*8:cx*8 + 8]
                for x in range(0, 8, 2):
                    out.append((row[x] << 4) | row[x+1])
    return bytes(out), px

def add_cannons(px):
    """Two small forward-facing guns: one on the back, one under the belly."""
    W, H = len(px[0]), len(px)
    k = W // 32                                              # 1 for the ship, 2 for the intro roll
    cols = [x for x in range(W) if any(px[y][x] for y in range(H))]
    if not cols:
        return
    left, width = cols[0], cols[-1] - cols[0] + 1
    def gun(x, under):
        rows = [y for y in range(H) if px[y][x]]
        if not rows:
            return
        mount = rows[-1] if under else rows[0]
        for t in range(k):                                   # barrel rows, outward from the body
            y = min(rows[-1] + 1 + t, H - 1) if under else max(rows[0] - 1 - t, 0)
            for bx in range(x, min(x + 6*k, W)):
                px[y][bx] = 15                               # barrel, bright steel
            px[y][x] = 1
            if x + 6*k < W:
                px[y][x + 6*k] = 1                           # muzzle
        for mx in range(x, x + 2*k):
            px[mount][mx] = 1                                # mount, black
    gun(left + width * 55 // 100, under=False)
    gun(left + width * 35 // 100, under=True)

def routine(data_addr, everywhere=False):
    c = bytearray()
    beq = []
    if everywhere:
        c += bytes.fromhex('4E71' * 5)             # nop padding where the demo/stage checks go
    else:
        c += bytes.fromhex('4A78F2FE')             # tst.w   $f2fe.w
        c += bytes.fromhex('6700')                  # beq.s   done (patched below)
        beq.append(len(c) - 1)
        c += bytes.fromhex('4A78F0B8')             # tst.w   $f0b8.w   (stage: 0 = first demo stage)
        c += bytes.fromhex('6600')                  # bne.s   done
        beq.append(len(c) - 1)
    c += bytes.fromhex('41F9') + data_addr.to_bytes(4, 'big')  # lea data.l,a0
    c += bytes.fromhex('43F9') + SHIP_RAM.to_bytes(4, 'big')   # lea $ff0800.l,a1
    c += bytes.fromhex('303C') + (NTILES*32//4 - 1).to_bytes(2, 'big')  # move.w #n-1,d0
    loop = len(c)
    c += bytes.fromhex('22D8')                      # move.l  (a0)+,(a1)+
    c += bytes.fromhex('51C8') + (loop - (len(c) + 2) & 0xFFFF).to_bytes(2, 'big')  # dbra d0,loop
    for b in beq:
        c[b] = len(c) - (b + 1)
    c += bytes.fromhex('41F900090000')              # lea $90000.l,a0 (original instruction)
    c += bytes.fromhex('4E75')                      # rts
    return bytes(c)

INTRO = 0x101000
INTRO_HOOK = 0xC572  # intro frame loop: move.w #$8144,$C00004 (display on, after the SAT DMA)
LOGO = 0x1F0000    # PIXELS logo stream; archive 0x90000 entry 0 is the SEGA logo
ROLL_DATA = 0x1F1000   # archive 0x19000 entry 2 (intro: Rynex roll), as a literal stream
BRAND_DATA = 0x1F5000  # archive 0x19000 entry 3 (intro: Technosoft logo)
FACE_TICKS = 60   # close-up of the face shown before the bust

def intro_steps(intro_dir):
    """Steps of the transformation intro: (tileset name, cols, rows, palette bytes, ticks)."""
    seq = json.load(open(os.path.join(intro_dir, 'secuencia.json')))['secuencia']
    pal = lambda n: open(os.path.join(intro_dir, 'paletas', n + '.pal'), 'rb').read()
    steps = [('cara_primer_plano', 10, 10, pal(seq[0]['fotograma']), FACE_TICKS)]
    steps += [(st['tiles_de'], 20, 20, pal(st['fotograma']), st['ticks_60hz']) for st in seq]
    return steps

def sprite_set(raw, n):
    """n x n tiles (row order) -> 2x2 sprites: (tile data, [(y, x, first tile)]), empty sprites dropped."""
    t = lambda r, c: raw[(r*n + c)*32:(r*n + c + 1)*32]
    ox, oy = X0 + (160 - n*8) // 2, Y0 + (160 - n*8) // 2   # centre in the 160x160 picture
    data, sprites = b'', []
    for r in range(0, n, 2):
        for c in range(0, n, 2):
            cells = [t(r, c), t(r+1, c), t(r, c+1), t(r+1, c+1)]   # sprite tiles go column by column
            if all(x == bytes(32) for x in cells):
                continue
            sprites.append((oy + r*8, ox + c*8, len(data) // 32))
            data += b''.join(cells)
    return data, sprites

X0, Y0 = 128 + 80, 128 + 32

def build_intro(intro_dir):
    """Assemble intro.s plus its data at INTRO; needs binutils-m68k-linux-gnu."""
    here = os.path.dirname(os.path.abspath(__file__))
    with tempfile.TemporaryDirectory() as tmp:
        lines = ['        .section .rodata', '        .globl intro_steps', '        .even']
        sets, recs = [], []
        for k, (name, cols, rows, palette, ticks) in enumerate(intro_steps(intro_dir)):
            if name not in sets:
                raw = open(os.path.join(intro_dir, 'tiles', name + '.bin'), 'rb').read()
                data, sprites = sprite_set(raw, cols)
                assert len(data) // 32 <= 232 and len(sprites) <= 80
                body = (len(data) // 32).to_bytes(2, 'big') + len(sprites).to_bytes(2, 'big') + data
                body += b''.join(v.to_bytes(2, 'big') for s in sprites for v in s)
                open(os.path.join(tmp, name + '.set'), 'wb').write(body)
                sets.append(name)
            open(os.path.join(tmp, 'pal%d.bin' % k), 'wb').write(palette)
            recs.append('        .long s_%s, p_%d\n        .word %d' % (name, k, ticks))
        lines.append('intro_steps:')
        lines += recs + ['        .long 0']
        lines += ['s_%s: .incbin "%s.set"' % (name, name) for name in sets]
        lines += ['p_%d: .incbin "pal%d.bin"' % (k, k) for k in range(len(recs))]
        open(os.path.join(tmp, 'intro_data.s'), 'w').write('\n'.join(lines) + '\n')
        asm = ['m68k-linux-gnu-as', '-m68000', '--register-prefix-optional', '-I', tmp]
        subprocess.run(asm + ['-o', os.path.join(tmp, 'intro.o'), os.path.join(here, 'intro.s')], check=True)
        subprocess.run(asm + ['-o', os.path.join(tmp, 'data.o'), os.path.join(tmp, 'intro_data.s')], check=True)
        subprocess.run(['m68k-linux-gnu-ld', '-Ttext=0x%X' % INTRO, '-e', 'intro_hook', '--oformat', 'binary',
                        '-o', os.path.join(tmp, 'intro.bin'), os.path.join(tmp, 'intro.o'), os.path.join(tmp, 'data.o')],
                       check=True)
        return open(os.path.join(tmp, 'intro.bin'), 'rb').read()

def main(rom_path, png, out_path, everywhere=False, intro_dir=None, pixels=False):
    rom = bytearray(open(rom_path, 'rb').read())
    assert len(rom) == 0x100000 and rom[HOOK:HOOK+6] == bytes.fromhex('41F900090000'), 'unexpected ROM'
    if os.path.isdir(png):                          # Ecco frames folder: one pose per ship frame
        data = b''.join(frame_tiles(os.path.join(png, 'frame_%03d.png' % i))[0] for i in FRAMES)
    else:                                           # single image for all 7 frames
        data = frame_tiles(png)[0] * 7
    code = routine(NEW + 0x100, everywhere)
    rom += b'\xFF' * 0x100000                       # grow to 2 MB
    rom[NEW:NEW+len(code)] = code
    rom[NEW+0x100:NEW+0x100+len(data)] = data
    rom[HOOK:HOOK+6] = bytes.fromhex('4EB9') + NEW.to_bytes(4, 'big')  # jsr $100000.l
    if intro_dir:                                   # intro frame loop: 'display on' -> jsr intro_hook
        intro = build_intro(intro_dir)
        assert INTRO + len(intro) <= LOGO
        rom[INTRO:INTRO+len(intro)] = intro
        assert rom[INTRO_HOOK:INTRO_HOOK+8] == bytes.fromhex('33FC814400C00004')
        rom[INTRO_HOOK:INTRO_HOOK+8] = bytes.fromhex('4EB9') + INTRO.to_bytes(4, 'big') + bytes.fromhex('4E71')
        # the Rynex rolling in the intro -> the dolphin rolling; 'Techno soft' -> 'Blostesoft'
        import intro_gfx, pixels_logo
        ecco = os.path.join(png, 'frame_000.png') if os.path.isdir(png) else png
        for idx, data, at in ((intro_gfx.ROLL_ENTRY, intro_gfx.dolphin_roll(rom, ecco, add_cannons), ROLL_DATA),
                              (intro_gfx.LOGO_ENTRY, intro_gfx.logo(rom), BRAND_DATA)):
            stream = pixels_logo.literal_stream(data)
            assert at + len(stream) <= len(rom)
            rom[at:at+len(stream)] = stream
            p = intro_gfx.ARCHIVE + 2 + idx*4
            rom[p:p+4] = (at - intro_gfx.ARCHIVE).to_bytes(4, 'big')
    if pixels:                                      # SEGA logo -> PIXELS, same palette animation
        import pixels_logo
        stream = pixels_logo.literal_stream(pixels_logo.tiles(pixels_logo.logo()))
        rom[LOGO:LOGO+len(stream)] = stream
        rom[0x90002:0x90006] = (LOGO - 0x90000).to_bytes(4, 'big')
        assert rom[0x8E4:0x8E8] == bytes.fromhex('00001111')
        rom[0x8E4:0x924] = bytes(0x40)                 # blank the 'TM' sprite (2 tiles, raw)
    rom[0x1A4:0x1A8] = (len(rom) - 1).to_bytes(4, 'big')               # ROM end
    # The game's own check (ROM 0x34C) sums words from 0x200 up to (ROM end & ~1), last word excluded
    cs = sum(int.from_bytes(rom[i:i+2], 'big') for i in range(0x200, (len(rom) - 1) & ~1, 2)) & 0xFFFF
    rom[0x18E:0x190] = cs.to_bytes(2, 'big')
    open(out_path, 'wb').write(rom)
    return bytes(rom)

def make_ips(orig, patched):
    """IPS patch from orig to patched; new bytes past the original end are records too."""
    orig = orig + b'\x00' * (len(patched) - len(orig))  # forces the grown tail to be written
    out = bytearray(b'PATCH'); i = 0
    while i < len(patched):
        if patched[i] == orig[i]:
            i += 1; continue
        r = i
        while r < len(patched) and r - i < 0xFFFF and patched[r] == patched[i]:
            r += 1
        if r - i >= 16:                                   # RLE record
            out += i.to_bytes(3, 'big') + b'\x00\x00' + (r - i).to_bytes(2, 'big') + patched[i:i+1]
            i = r; continue
        j = i
        while j < len(patched) and j - i < 0xFFFF and patched[j] != orig[j]:
            j += 1
        out += i.to_bytes(3, 'big') + (j - i).to_bytes(2, 'big') + patched[i:j]
        i = j
    return bytes(out + b'EOF')

if __name__ == '__main__':
    argv = sys.argv[1:]
    intro_dir = None
    if '--intro' in argv:
        i = argv.index('--intro'); intro_dir = argv[i + 1]; del argv[i:i + 2]
    args = [a for a in argv if a not in ('--everywhere', '--pixels')]
    patched = main(*args[:3], everywhere='--everywhere' in argv, intro_dir=intro_dir, pixels='--pixels' in argv)
    if len(args) > 3:
        # IPS only describes changes, so the file must grow: the 2 MB tail is written as 0xFF runs
        open(args[3], 'wb').write(make_ips(open(args[0], 'rb').read(), patched))

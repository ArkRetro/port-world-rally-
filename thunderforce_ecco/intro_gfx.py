"""Graphics swapped in the game's own intro: the rolling Rynex and the Technosoft logo.

Both live in the ROM archive at 0x19000 (same format and unpacker as 0x90000):
- entry 2 -> VRAM 0x0000: tiles 0x000-0x0DF are the Rynex roll, 7 frames of 64x32
  (two 4x4 sprites side by side, tiles column by column, palette line 2). The intro
  table at ROM 0xD670 shows them over 18 steps of 20 degrees, frames 0x00-0x80 for
  0-80 degrees, the same ones vertically flipped for 100-180, then 0xA0/0xC0
  (flipped and not) for the way back.
- entry 3 -> VRAM 0x8000: 'Techno soft' + (R), 9 sprites of palette line 1 (LOGO_SPRITES).
  Letters are shaded by row (index 7 at the top ... 6 at the bottom) with a drop
  shadow in index 14.
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

import tf4_unpack

ARCHIVE = 0x19000
ROLL_ENTRY, LOGO_ENTRY = 2, 3

# Palette line 2 during the roll, CRAM 0000BBB0GGG0RRR0 as (R, G, B) 3-bit channels
PAL2 = [(0,0,0),(0,0,0),(0,1,3),(2,3,6),(4,5,7),(6,6,7),(4,0,0),(6,0,1),
        (7,3,1),(7,6,1),(6,6,0),(0,0,0),(0,1,2),(1,2,3),(3,4,5),(7,7,7)]
PAL2 = [tuple(c*36 for c in p) for p in PAL2]
USABLE = [1,2,3,4,5,12,13,14,15]  # blues/greys/black/white

# angle shown by each stored roll frame (0x00, 0x20 ... 0xC0); see the table at 0xD670
ROLL_ANGLES = [0, 20, 40, 60, 80, -30, -65]

# (x, y, width, height in tiles, first tile) of the logo sprites, as the intro places them
LOGO_SPRITES = [(62, 96, 4, 3, 0x400), (94, 96, 3, 3, 0x40C), (114, 96, 3, 3, 0x415),
                (134, 96, 3, 3, 0x41E), (158, 96, 2, 3, 0x427), (174, 96, 3, 3, 0x436),
                (198, 96, 4, 3, 0x43F), (230, 96, 2, 3, 0x44B)]   # the (R) sprite is kept
LOGO_BOX = (62, 96, 244, 118)    # letters (without shadow), right edge stops before (R)
LOGO_ROWS = [7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 1, 1, 2, 3, 4, 4, 5, 5, 6, 6]
SHADOW = 14
FONT = '/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf'


def _entry(rom, idx):
    return bytearray(tf4_unpack.dec(rom, tf4_unpack.entry(rom, ARCHIVE, idx))[0])


def _put_sprite_tiles(data, px, x0, y0, w, h, tile):
    """Write the w x h tile sprite at (x0, y0) of index map px into data, column by column."""
    for cx in range(w):
        for cy in range(h):
            t = tile + cx*h + cy
            for yy in range(8):
                for xx in range(0, 8, 2):
                    a = px.get((x0 + cx*8 + xx, y0 + cy*8 + yy), 0)
                    b = px.get((x0 + cx*8 + xx + 1, y0 + cy*8 + yy), 0)
                    data[t*32 + yy*4 + xx//2] = (a << 4) | b


def dolphin_roll(rom, png, add_cannons):
    """Archive entry 2 with the Rynex roll frames replaced by the dolphin rolling."""
    src = Image.open(png).convert('RGBA')
    k = min(62 / src.width, 28 / src.height)
    w, h = round(src.width * k), round(src.height * k)
    canvas = Image.new('RGBA', (64, 32), (0, 0, 0, 0))
    canvas.paste(src.resize((w, h), Image.LANCZOS), ((64 - w) // 2, (32 - h) // 2))
    near = lambda rgb: min(USABLE, key=lambda i: sum((a-b)**2 for a, b in zip(rgb, PAL2[i])))
    base = [[0]*64 for _ in range(32)]
    for y in range(32):
        for x in range(64):
            r, g, b, a = canvas.getpixel((x, y))
            base[y][x] = near((r, g, b)) if a >= 128 else 0
    add_cannons(base)
    rows = [y for y in range(32) if any(base[y])]
    cy = (rows[0] + rows[-1]) / 2
    data = _entry(rom, ROLL_ENTRY)
    for f, angle in enumerate(ROLL_ANGLES):
        c = math.cos(math.radians(angle))
        c = math.copysign(max(abs(c), 0.18), c)           # edge-on: a thin dolphin, not nothing
        px = {}
        for y in range(32):
            sy = round(cy + (y - cy) / c)
            if 0 <= sy < 32:
                for x in range(64):
                    if base[sy][x]:
                        px[(x, y)] = base[sy][x]
        _put_sprite_tiles(data, px, 0, 0, 4, 4, f*32)
        _put_sprite_tiles(data, px, 32, 0, 4, 4, f*32 + 16)
    return bytes(data)


def logo(rom, text='Blostesoft'):
    """Archive entry 3 with 'Techno soft' replaced by text, same shading and shadow."""
    x0, y0, x1, y1 = LOGO_BOX
    big = ImageFont.truetype(FONT, 200)
    l, t, r, b = big.getbbox(text)
    img = Image.new('L', (r - l + 8, b - t + 8), 0)
    ImageDraw.Draw(img).text((4 - l, 4 - t), text, font=big, fill=255)
    img = img.resize((x1 - x0, y1 - y0), Image.LANCZOS)
    mask = {(x0 + x, y0 + y) for y in range(img.height) for x in range(img.width)
            if img.getpixel((x, y)) >= 110}
    px = {(x, y): LOGO_ROWS[y - y0] for x, y in mask}
    for x, y in mask:
        for d in (1, 2):
            if (x + d, y + d) not in mask:
                px.setdefault((x + d, y + d), SHADOW)
    data = _entry(rom, LOGO_ENTRY)
    for x, y, w, h, tile in LOGO_SPRITES:
        _put_sprite_tiles(data, px, x, y, w, h, tile - 0x400)
    return bytes(data)

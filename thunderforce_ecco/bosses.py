"""New final bosses for stages 1 to 4 (Strite, Ruin, Air Raid, Daser), from the jefes-finales folder.

The original boss keeps its code: movement, hit boxes, energy and attacks. Only the way
it looks changes (bosses.s, hooked at the end of the game's sprite-list builder):
- its pieces are taken out of the sprite list and the new character is drawn in their
  place, anchored to one piece (BOSSES[...]['anchor']);
- its projectiles are drawn as the character's spinning projectile;
- frames and palette line 1 follow what the boss is doing: idle sequence, attack while
  its projectiles are on screen, hit flash when its energy drops, defeat when it has
  no energy left.

Boss graphics come from one compressed bank per stage (ROM archive 0x94000, 'nibble
RLE', unpacked straight to VRAM tiles 0x000-0x1FF). The bank is rebuilt: the boss's own
tiles are replaced by the new character, effect tiles are kept, and the tiles that stay
on screen with the boss's palette line (Strite's tail, Daser's arms) are recoloured.
Bart's ten dance frames do not fit in a bank: they stay in ROM (dma=True) and bosses.s
copies the one to show to one of two VRAM slots.
"""
import json
import os

BANK_ARCHIVE = 0x94000
SCENE_PALETTES = 0x62200          # + stage * 0x80; line 1 is the boss's

BOSSES = {
    0: dict(name='jefe1',                       # Strite: the man with sunglasses
            anchor=[0x05D, 0x069, 0x075],      # head piece, 4x3 (three animation frames)
            anchor_offset=(-20, -40),          # head -> top left of the 128x128 character
            remove=(0x04D, 0x16C),             # body pieces
            projectile=(0x16D, 0x187),         # fireballs (3x3) -> spinning sunglasses
            projectile_offset=4,
            free=[(0x04D, 0x187)],             # tiles the new character can use
            recolour=[(0x001, 0x014), (0x188, 0x1D1), (0x1EA, 0x1F9)],   # tail, odds and ends
            idle=[('base', 'normal', 110), ('gafas_brillo', 'normal', 6), ('gafas_brillo', 'brillo2', 6),
                  ('gafas_brillo', 'normal', 6), ('gafas_brillo', 'brillo2', 6)],
            attack='ataque'),
    1: dict(name='jefe2',                       # Ruin: Bruno, with the green mug
            anchor=[0x03D],                    # centre piece, 4x4 (the one not flipped)
            anchor_offset=(-60, -28),
            remove=(0x001, 0x0A9),
            projectile=(0x0AA, 0x0BD),         # missiles (2x2) -> spinning mug
            projectile_offset=0,
            free=[(0x001, 0x0BD), (0x176, 0x1FF)],
            recolour=[(0x15A, 0x175)],
            idle=[('base', 'normal', 90), ('parpadeo', 'normal', 6), ('base', 'normal', 70),
                  ('taza_arriba', 'normal', 24), ('taza_arriba', 'brillo2', 6), ('taza_arriba', 'normal', 6),
                  ('parpadeo', 'normal', 6)],
            attack='ataque'),
    2: dict(name='jefe3',                       # Air Raid: Bart dancing
            anchor=[0x100],                    # lower hull piece, 4x4, palette 1
            anchor_offset=(32, -84),
            remove=(0x018, 0x167),             # hull (palette 2 on top, 1 below) and nozzles
            projectile=(0x18C, 0x1D7),         # fire disc (two 2x4 halves) -> spinning record
            projectile_offset=(0, 8), projectile_halves=True,
            free=[(0x18C, 0x1D7)],
            recolour=[],
            alias={'base': 'baile0', 'derrota': 'baile3'},
            idle=[('baile%d' % i, 'normal', 6) for i in range(10)],
            dma=True, slots=0x018),            # two slots for a frame from 0x018 on
    3: dict(name='jefe4',                       # Daser: Mickey the sorcerer's apprentice
            anchor=[0x120],                    # body piece, 4x4, palette 3
            anchor_offset=(-44, -40),
            line=3,
            remove=(0x085, 0x1B9),             # body; 0x085-0x0FF not seen on screen, taken too
            projectile=(0x1E0, 0x1EF),         # 2x2 shots -> swaying quaver
            projectile_offset=0,
            spin=(0, 0, 0x800, 0x800),
            free=[(0x100, 0x1B9), (0x085, 0x0FF)],
            recolour=[(0x009, 0x035), (0x081, 0x084), (0x1BA, 0x1D4)],   # arms, effects
            # 'respira' redraws the whole body and 'derrota' has its own layout: neither fits
            # next to base (196 tiles) in the bank, so the defeat is base with its palette
            idle=[('base', 'normal', 100), ('magia', 'normal', 6), ('magia', 'brillo2', 6),
                  ('magia', 'normal', 6), ('magia', 'brillo2', 6)]),
}
SPIN = (0, 0x800, 0x1800, 0x1000)
FRAME_ORDER = ['base', 'ataque', 'derrota']   # fixed slots 0-2 used by bosses.s, idle frames follow
PALETTE_ORDER = ['normal', 'brillo2', 'golpe', 'derrota']


def bank_entry(rom, stage):
    return BANK_ARCHIVE + int.from_bytes(rom[BANK_ARCHIVE + 2 + 4*stage:BANK_ARCHIVE + 6 + 4*stage], 'big')


def bank_decode(rom, a, nbytes=0x4000):
    """Nibble RLE: byte = pixel << 4 | n; n < 13: n+1 times, 13: next byte+1, 14: next word+1,
    15: next word+0x10001. Returns the bank aligned to VRAM 0x0000 (the stream starts at 0xFFFE)."""
    out = []
    while len(out) < nbytes*2:
        b = rom[a]; a += 1; v, n = b >> 4, b & 15
        if n < 13: cnt = n
        elif n == 13: cnt = rom[a]; a += 1
        elif n == 14: cnt = (rom[a] << 8) | rom[a+1]; a += 2
        else: cnt = ((rom[a] << 8) | rom[a+1]) + 0x10000; a += 2
        out += [v] * (cnt + 1)
    data = bytes((out[i] << 4) | out[i+1] for i in range(0, nbytes*2, 2))
    return data[2:] + b'\0\0'


def bank_encode(bank):
    nib = []
    for b in b'\x00\x40' + bank[:0x3FFE]:
        nib += [b >> 4, b & 15]
    out = bytearray(); i = 0
    while i < len(nib):
        v = nib[i]; j = i
        while j < len(nib) and nib[j] == v and j - i < 0x20000:
            j += 1
        c = j - i - 1
        if c < 13: out.append((v << 4) | c)
        elif c < 256: out += bytes([(v << 4) | 13, c])
        elif c < 0x10000: out += bytes([(v << 4) | 14, c >> 8, c & 255])
        else: c -= 0x10000; out += bytes([(v << 4) | 15, c >> 8, c & 255])
        i = j
    return bytes(out)


def _rgb(w):
    return ((w >> 1) & 7, (w >> 5) & 7, (w >> 9) & 7)


def _recolour(bank, ranges, old_pal, new_pal):
    """Map the pixels of tiles in ranges from old_pal to the nearest new_pal colour (1-14)."""
    near = {}
    for i in range(1, 16):
        o = _rgb(old_pal[i])
        near[i] = min(range(1, 15), key=lambda k: sum((a - b)**2 for a, b in zip(o, _rgb(new_pal[k]))))
    near[0] = 0
    out = bytearray(bank)
    for lo, hi in ranges:
        for p in range(lo*32, (hi + 1)*32):
            b = out[p]
            out[p] = (near[b >> 4] << 4) | near[b & 15]
    return out


class _Alloc:
    def __init__(self, ranges):
        self.ranges = [list(r) for r in ranges]

    def take(self, n):
        for r in self.ranges:
            if r[1] - r[0] + 1 >= n:
                t = r[0]; r[0] += n
                return t
        raise ValueError('boss tiles do not fit in the bank')


def build(rom, stage, boss_dir, dma_at=None):
    """New bank stream, palette, the boss's data block for bosses.s (as assembler lines) and,
    for a DMA boss, the frame tiles to store in ROM at dma_at."""
    cfg = BOSSES[stage]
    info = json.load(open(os.path.join(boss_dir, 'jefes.json')))['jefes'][cfg['name']]
    path = lambda *p: os.path.join(boss_dir, cfg['name'], *p)
    pal = {k: [int(c, 16) for c in v] for k, v in info['paletas'].items()}
    frames = info['fotogramas']
    tiles = lambda f: open(path('tiles', f + '_sprites.bin'), 'rb').read()
    alias = cfg.get('alias', {})
    line = cfg.get('line', 1)

    bank = bytearray(bank_decode(rom, bank_entry(rom, stage)))
    pa = SCENE_PALETTES + stage*0x80 + line*0x20
    old_pal = [int.from_bytes(rom[pa + 2*i:pa + 2*i + 2], 'big') for i in range(16)]
    bank = _recolour(bank, cfg['recolour'], old_pal, pal['normal'])
    for lo, hi in cfg['free']:
        bank[lo*32:(hi + 1)*32] = bytes((hi - lo + 1)*32)

    alloc = _Alloc(cfg['free'])
    def put(data):
        t = alloc.take(len(data) // 32)
        bank[t*32:t*32 + len(data)] = data
        return t

    size = lambda w, h: ((w - 1) << 2) | (h - 1)
    names = [alias.get(n, n) for n in FRAME_ORDER] + [f for f, _, _ in cfg['idle']]
    names = list(dict.fromkeys(names))
    base_name = alias.get('base', 'base')
    base = frames[base_name]; bt = tiles(base_name)
    proj = put(tiles('proyectil'))
    sprites, dma = {}, b''
    if cfg.get('dma'):
        # every frame whole in ROM, tiles numbered from the start of the frame
        L_dma = []
        for n in names:
            sprites[n] = [(s['x'], s['y'], size(s['w'], s['h']), s['tile']) for s in frames[n]['sprites']]
            data = tiles(n)
            L_dma.append((dma_at + len(dma), len(data) // 2))
            dma += data
        n_slot = max(len(tiles(n)) // 32 for n in names)
        slots = (cfg['slots'], cfg['slots'] + n_slot)
        assert slots[1] + n_slot - 1 <= cfg['remove'][1]
        assert dma_at // 0x20000 == (dma_at + len(dma) - 1) // 0x20000, 'DMA source crosses 128 KB'
    else:
        base_vram = [put(bt[s['tile']*32:(s['tile'] + s['w']*s['h'])*32]) for s in base['sprites']]

        def frame_sprites(name):
            """(dx, dy, size, tile); frames differing from base split the changed sprites in
            columns, frames with their own layout get all their tiles (or show base if full)."""
            if name == base_name:
                return [(s['x'], s['y'], size(s['w'], s['h']), t) for s, t in zip(base['sprites'], base_vram)]
            ft = tiles(name)
            if frames[name]['sprites'] != base['sprites']:
                try:
                    return [(s['x'], s['y'], size(s['w'], s['h']),
                             put(ft[s['tile']*32:(s['tile'] + s['w']*s['h'])*32])) for s in frames[name]['sprites']]
                except ValueError:
                    print('%s: no room for %s, it shows %s' % (cfg['name'], name, base_name))
                    return frame_sprites(base_name)
            out = []
            for s, t in zip(base['sprites'], base_vram):
                w, h, n = s['w'], s['h'], s['w']*s['h']
                a = s['tile']*32
                if ft[a:a + n*32] == bt[a:a + n*32]:
                    out.append((s['x'], s['y'], size(w, h), t)); continue
                for c in range(w):
                    ca = a + c*h*32
                    col = ft[ca:ca + h*32]
                    tc = t + c*h if col == bt[ca:ca + h*32] else put(col)
                    out.append((s['x'] + 8*c, s['y'], size(1, h), tc))
            return out
        # own-layout frames last: they are the ones left out if the bank is full
        order = sorted(names, key=lambda n: frames[n]['sprites'] != base['sprites'])
        for n in order:
            sprites[n] = frame_sprites(n)

    dx, dy = cfg['anchor_offset']
    pdx, pdy = cfg['projectile_offset'] if isinstance(cfg['projectile_offset'], tuple) else (cfg['projectile_offset'],)*2
    anchors = (cfg['anchor'] + [0xFFFF]*4)[:4]
    L = []
    lab = 'boss%d' % stage
    L.append('%s:' % lab)
    L.append('        .word %s' % ', '.join('0x%04X' % a for a in anchors))
    L.append('        .word %d, %d' % (dx, dy))
    L.append('        .word 0x%04X, 0x%04X' % cfg['remove'])
    L.append('        .word 0x%04X, 0x%04X, 0x%04X, %d' % (cfg['projectile'] + (proj, pdx)))
    L.append('        .long %s_frames, %s_pals, %s_idle' % (lab, lab, lab))
    L.append('        .word %d' % sum(t for _, _, t in cfg['idle']))
    L.append('        .word 0x%04X, 0x%02X, %d, %d' % (line << 13, line*0x20, pdy, int(cfg.get('projectile_halves', False))))
    L.append('        .long %s_spin' % lab)
    if dma:
        L.append('        .long %s_dma' % lab)
        L.append('        .word 0x%03X, 0x%03X' % slots)
    else:
        L.append('        .long 0')
        L.append('        .word 0, 0')
    L.append('%s_spin:' % lab)
    L.append('        .word ' + ', '.join('0x%04X' % f for f in cfg.get('spin', SPIN)))
    if dma:
        L.append('%s_dma:' % lab)
        L += ['        .long 0x%06X\n        .word %d' % d for d in L_dma]
    L.append('%s_frames:' % lab)
    L.append('        .long ' + ', '.join('%s_f%d' % (lab, i) for i in range(len(names))))
    for i, n in enumerate(names):
        spr = sprites[n]
        L.append('%s_f%d:   | %s' % (lab, i, n))
        L.append('        .word %d' % len(spr))
        L += ['        .word %d, %d, 0x%02X, 0x%03X' % s for s in spr]
    L.append('%s_pals:' % lab)
    for p in PALETTE_ORDER:
        L.append('        .word ' + ', '.join('0x%04X' % c for c in pal[p]))
    L.append('%s_idle:' % lab)
    L += ['        .word %d, %d, %d' % (names.index(f), PALETTE_ORDER.index(p), t) for f, p, t in cfg['idle']]
    return bank_encode(bytes(bank)), pal['normal'], L, dma, pa

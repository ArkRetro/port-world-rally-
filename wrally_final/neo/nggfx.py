"""Codificadores de formatos Neo Geo (C-ROM sprites, S-ROM fix, LO, M1) y empaquetado."""
import struct, zlib, os
from PIL import Image, ImageFont, ImageDraw

def c_tile(px):
    """px: 16x16 lista de filas de indices 0..15 -> 128 bytes (region intercalada C1/C2)"""
    out = bytearray(128)
    for half, x0 in ((0, 8), (1, 0)):          # 0x00: mitad derecha, 0x40: mitad izquierda
        for y in range(16):
            b = [0, 0, 0, 0]
            for x in range(8):
                p = px[y][x0 + x]
                b[0] |= ((p >> 0) & 1) << x
                b[2] |= ((p >> 1) & 1) << x
                b[1] |= ((p >> 2) & 1) << x
                b[3] |= ((p >> 3) & 1) << x
            o = half * 0x40 + y * 4
            out[o:o + 4] = bytes(b)
    return bytes(out)

def split_c(region):
    """region intercalada -> (c1, c2)"""
    return bytes(region[0::2]), bytes(region[1::2])

def s_tile(px):
    """px 8x8 -> 32 bytes fix: columnas (4,5),(6,7),(0,1),(2,3); nibble bajo = pixel izq"""
    out = bytearray(32)
    for blk, x0 in enumerate((4, 6, 0, 2)):
        for y in range(8):
            out[blk * 8 + y] = (px[y][x0] & 15) | ((px[y][x0 + 1] & 15) << 4)
    return bytes(out)

def font_srom(color=1, shadow=2):
    f = ImageFont.load_default()
    data = bytearray(0x20000)
    for ch in range(32, 127):
        im = Image.new('L', (8, 8), 0)
        d = ImageDraw.Draw(im)
        d.text((1, -2), chr(ch), fill=255, font=f)
        px = [[0] * 8 for _ in range(8)]
        for y in range(8):
            for x in range(8):
                if im.getpixel((x, y)) > 100:
                    px[y][x] = color
                elif x > 0 and y > 0 and im.getpixel((x - 1, y - 1)) > 100:
                    px[y][x] = shadow
        data[ch * 32:ch * 32 + 32] = s_tile(px)
    data[0xFF0 * 32:0xFF1 * 32] = b'\x22' * 32     # tile solido (color 2 = negro en la paleta 15)
    return data

def lo_rom():
    """tabla de reduccion vertical: [zoom<<8 | linea] -> linea fuente (4 bits fila + 4 bits tile)"""
    d = bytearray(0x20000)
    for z in range(256):
        h = z + 1
        for y in range(256):
            v = (y * 256 // h) if y < h else 0
            d[(z << 8) | y] = min(v, 255)
            d[0x10000 + ((z << 8) | y)] = min(v, 255)
    return d

def m1_min():
    m = bytearray(0x20000)
    m[0:4] = bytes([0xF3, 0xC3, 0x00, 0x01])
    m[0x38:0x3B] = bytes([0xFB, 0xED, 0x4D])
    m[0x66:0x6E] = bytes([0xF5, 0xDB, 0x00, 0xD3, 0x0C, 0xF1, 0xED, 0x45])
    m[0x100:0x107] = bytes([0x31, 0xFF, 0xFF, 0xED, 0x56, 0x18, 0xFE])
    return m

def swap16(b):
    w = bytearray(b)
    w[0::2], w[1::2] = b[1::2], b[0::2]
    return bytes(w)

def crc(b):
    return '%08x' % (zlib.crc32(b) & 0xffffffff)

def install(name, title, p, s, m, v, c, outroot):
    """escribe ficheros + softlist para MAME (hash/neogeo.xml) y un .neo"""
    rd = os.path.join(outroot, 'roms', 'neogeo', name)
    os.makedirs(rd, exist_ok=True)
    os.makedirs(os.path.join(outroot, 'hash_' + name), exist_ok=True)
    c1, c2 = split_c(c)
    files = {'p1.bin': swap16(p), 's1.bin': s, 'm1.bin': m, 'v1.bin': v, 'c1.bin': c1, 'c2.bin': c2}
    for k, b in files.items():
        open(os.path.join(rd, k), 'wb').write(b)
    def sz(n):
        x = 1
        while x < n: x <<= 1
        return x
    xml = f'''<?xml version="1.0"?>
<!DOCTYPE softwarelist SYSTEM "softwarelist.dtd">
<softwarelist name="neogeo" description="pruebas">
	<software name="{name}">
		<description>{title}</description>
		<year>2026</year>
		<publisher>homebrew</publisher>
		<sharedfeat name="release" value="MVS,AES" />
		<sharedfeat name="compatibility" value="MVS,AES" />
		<part name="cart" interface="neo_cart">
			<dataarea name="maincpu" width="16" endianness="big" size="{hex(sz(len(p)))}">
				<rom loadflag="load16_word_swap" name="p1.bin" offset="0x000000" size="{hex(len(p))}" crc="{crc(swap16(p))}" />
			</dataarea>
			<dataarea name="fixed" size="{hex(len(s))}">
				<rom offset="0x000000" size="{hex(len(s))}" name="s1.bin" crc="{crc(s)}" />
			</dataarea>
			<dataarea name="audiocpu" size="{hex(len(m))}">
				<rom offset="0x000000" size="{hex(len(m))}" name="m1.bin" crc="{crc(m)}" />
			</dataarea>
			<dataarea name="ymsnd:adpcma" size="{hex(sz(len(v)))}">
				<rom name="v1.bin" offset="0x000000" size="{hex(len(v))}" crc="{crc(v)}" />
			</dataarea>
			<dataarea name="sprites" size="{hex(sz(len(c)))}">
				<rom loadflag="load16_byte" name="c1.bin" offset="0x000000" size="{hex(len(c1))}" crc="{crc(c1)}" />
				<rom loadflag="load16_byte" name="c2.bin" offset="0x000001" size="{hex(len(c2))}" crc="{crc(c2)}" />
			</dataarea>
		</part>
	</software>
</softwarelist>
'''
    open(os.path.join(outroot, 'hash_' + name, 'neogeo.xml'), 'w').write(xml)

def write_neo(path, name, p, s, m, v, c, ngh=0x999):
    """formato .neo (TerraOnion/MiSTer): cabecera 4096 bytes + P S M V1 V2 C"""
    h = bytearray(4096)
    h[0:4] = b'NEO\x01'
    struct.pack_into('<IIIIII', h, 4, len(p), len(s), len(m), len(v), 0, len(c))
    struct.pack_into('<II', h, 0x1C, 2026, 0)   # year, genre
    struct.pack_into('<II', h, 0x24, 0, ngh)    # screenshot, NGH
    h[0x2C:0x2C + 33] = name.encode()[:32].ljust(33, b'\0')
    h[0x4D:0x4D + 17] = b'homebrew'.ljust(17, b'\0')
    # P en .neo: palabras 68k intercambiadas (little endian), como en los .neo existentes
    pw = bytearray(p)
    pw[0::2], pw[1::2] = p[1::2], p[0::2]
    open(path, 'wb').write(bytes(h) + bytes(pw) + s + m + v + c)

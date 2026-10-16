#!/usr/bin/env python3
"""Package a .neo file into the three flash-cart formats, using exactly the conventions of
Teo Tormo's RoboCop release (verified byte-for-byte against his robocop.neo / darksoft / backbit sets):

  NeoSD   : the .neo itself.
  Darksoft: <name>/prom (P region as stored in the .neo), srom, m1rom, vroma0 (V1+V2),
            crom0 = 2-byte interleave of c1/c2 (c1[0:2] c2[0:2] c1[2:4] c2[2:4] ...), fpga = "10".
  Backbit : <name>/<name>-p1.p1, -s1.s1, -m1.m1, -v1.v1, -c1.c1 (= C[0::2]), -c2.c2 (= C[1::2]) plus a
            MAME software-list neogeo.xml with the CRC/SHA1 of each file.

usage: pack_formats.py game.neo outdir shortname "Description" [--fpga 10]
"""
import argparse, hashlib, os, struct, zipfile, zlib


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('neo'); ap.add_argument('outdir'); ap.add_argument('name'); ap.add_argument('desc')
    ap.add_argument('--fpga', default='10'); ap.add_argument('--year', default='2026')
    ap.add_argument('--publisher', default='homebrew (World Rally Neo Geo port)')
    a = ap.parse_args()
    d = open(a.neo, 'rb').read()
    p, s, m, v1, v2, c = struct.unpack('<6I', d[4:28])
    o = 4096
    def take(n):
        nonlocal o
        r = d[o:o + n]; o += n
        return r
    P, S, M, V, C = take(p), take(s), take(m), take(v1 + v2), take(c)
    c1, c2 = C[0::2], C[1::2]
    crom = bytearray(len(C))
    for i in range(0, len(c1), 2):
        crom[2 * i:2 * i + 2] = c1[i:i + 2]; crom[2 * i + 2:2 * i + 4] = c2[i:i + 2]

    ds = os.path.join(a.outdir, 'darksoft', a.name); os.makedirs(ds, exist_ok=True)
    for fn, blob in (('prom', P), ('srom', S), ('m1rom', M), ('vroma0', V), ('crom0', bytes(crom)), ('fpga', a.fpga.encode())):
        open(os.path.join(ds, fn), 'wb').write(blob)

    bb = os.path.join(a.outdir, 'backbit', a.name); os.makedirs(bb, exist_ok=True)
    files = [('p1.p1', P), ('s1.s1', S), ('m1.m1', M), ('v1.v1', V), ('c1.c1', c1), ('c2.c2', c2)]
    for ext, blob in files:
        open(os.path.join(bb, '%s-%s' % (a.name, ext)), 'wb').write(blob)
    rom = lambda ext, blob, extra='': '<rom %sname="%s-%s" offset="0x000000" size="0x%x" crc="%08x" sha1="%s" />' % (
        extra, a.name, ext, len(blob), zlib.crc32(blob) & 0xffffffff, hashlib.sha1(blob).hexdigest())
    rom2 = lambda ext, blob: rom(ext, blob, 'loadflag="load16_byte" ').replace('offset="0x000000"', 'offset="0x000001"' if ext == 'c2.c2' else 'offset="0x000000"')
    xml = '''<?xml version="1.0"?>
<!DOCTYPE softwarelist SYSTEM "softwarelist.dtd">
<softwarelist name="neogeo" description="SNK Neo-Geo cartridges">
	<software name="%s">
		<description>%s</description>
		<year>%s</year>
		<publisher>%s</publisher>
		<sharedfeat name="release" value="MVS,AES" />
		<sharedfeat name="compatibility" value="MVS,AES" />
		<part name="cart" interface="neo_cart">
			<dataarea name="maincpu" width="16" endianness="big" size="0x%x">
				%s
			</dataarea>
			<dataarea name="fixed" size="0x%x">
				%s
			</dataarea>
			<dataarea name="audiocpu" size="0x%x">
				%s
			</dataarea>
			<dataarea name="ymsnd:adpcma" size="0x%x">
				%s
			</dataarea>
			<dataarea name="sprites" size="0x%x">
				%s
				%s
			</dataarea>
		</part>
	</software>
</softwarelist>
''' % (a.name, a.desc, a.year, a.publisher,
       len(P), rom('p1.p1', P, 'loadflag="load16_word_swap" '), len(S), rom('s1.s1', S),
       max(len(M), 0x20000), rom('m1.m1', M), len(V), rom('v1.v1', V), len(C), rom2('c1.c1', c1), rom2('c2.c2', c2))
    open(os.path.join(bb, 'neogeo.xml'), 'w').write(xml)

    for kind in ('darksoft', 'backbit'):
        zp = os.path.join(a.outdir, '%s_%s.zip' % (a.name, kind))
        with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
            root = os.path.join(a.outdir, kind)
            for r, _, fs in os.walk(root):
                for f in sorted(fs):
                    z.write(os.path.join(r, f), os.path.relpath(os.path.join(r, f), a.outdir))
        print('wrote', zp)


if __name__ == '__main__':
    main()

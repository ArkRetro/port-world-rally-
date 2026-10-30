#!/usr/bin/env python3
"""Build the distributable kit (thng-kit.zip) that tools/thng_build.py turns into thng.neo with the user's own
Thunder Hoop ROMs.  The kit holds only the port's own work: the runtime image (game.bin) and the Z80 driver with
every region that comes from the arcade ROMs zeroed (the stubs of the VRAM write sites, the 8x8 tile map, the
phrase lengths and the sample table), the fix font, the conversion scripts, the analysis tables (addresses) and
a manifest with the SHA-1 each zeroed region must have once rebuilt (the user's set is checked against it).

usage: mkkit.py <build dir> <out.zip>      (after "make neo")
"""
import hashlib
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from thng_build import ROMS, parse_inc  # noqa: E402
import mkrom  # noqa: E402

SCRIPTS = {                                  # kit path -> source path (relative to the thng directory)
    'tools/mkrom.py': 'tools/mkrom.py',
    'tools/m68k_mini.py': 'tools/m68k_mini.py',
    'tools/gfx2crom.py': 'tools/gfx2crom.py',
    'tools/oki2adpcma.py': 'tools/oki2adpcma.py',
    'tools/mkneo.py': 'tools/mkneo.py',
    'tools/mksoftlist.py': 'tools/mksoftlist.py',
    'analysis/gaelcrypt.py': 'analysis/gaelcrypt.py',
    'analysis/absrefs.csv': 'analysis/absrefs.csv',
    'analysis/trap_tables.csv': 'analysis/trap_tables.csv',
    'analysis/code_ranges.csv': 'analysis/code_ranges.csv',
    'analysis/swept_ranges.csv': 'analysis/swept_ranges.csv',
    'analysis/vram_sites.txt': 'analysis/vram_sites.txt',
}


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def main(build, out):
    root = os.path.join(HERE, '..')
    rd = lambda p: open(os.path.join(build, p), 'rb').read()
    syms = {}
    for line in open(os.path.join(build, 'game.sym')):
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    game = bytearray(rd('game.bin'))
    m1 = bytearray(rd('thng-m1.m1'))
    regions = []

    def cut(blob, name, offset, data, source):
        assert blob[offset:offset + len(data)] == data, (name, source)
        regions.append({'file': name, 'offset': offset, 'length': len(data), 'source': source, 'sha1': sha1(data)})
        blob[offset:offset + len(data)] = bytes(len(data))

    stubs, _ = mkrom.stub_blob(rd('th_cpu.bin'), os.path.join(root, 'analysis', 'vram_sites.txt'), syms)
    cut(game, 'game.bin', syms['linea_sites'], stubs, 'stubs')
    cut(game, 'game.bin', syms['CT_MAP8'], rd('map8.bin'), 'map8.bin')
    cut(game, 'game.bin', syms['sample_dur'], parse_inc(open(os.path.join(build, 'sample_dur.inc')).read()), 'sample_dur.inc')
    samples = parse_inc(open(os.path.join(build, 'samples.inc')).read())
    at = m1.find(samples)
    assert at >= 0 and m1.find(samples, at + 1) < 0, 'samples table not found once in the M1 ROM'
    cut(m1, 'thng-m1.m1', at, samples, 'samples.inc')
    neo = open(os.path.join(build, 'release', 'thng.neo'), 'rb').read()
    manifest = {
        'version': 1,
        'roms': dict(ROMS),
        'regions': regions,
        'neo_sha1': sha1(neo),
        'neo_name': 'Thunder Hoop NG v' + open(os.path.join(root, 'VERSION')).read().strip(),
    }
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps(manifest, indent=1))
        z.writestr('game.bin', bytes(game))
        z.writestr('game.sym', rd('game.sym'))
        z.writestr('thng-m1.m1', bytes(m1))
        z.writestr('font.s1', rd('font.s1'))              # the port's own fix font (tools/mkfont.py)
        for dst, src in SCRIPTS.items():
            z.write(os.path.join(root, src), dst)
    print('wrote %s (%d regions rebuilt from the user ROMs)' % (out, len(regions)))


if __name__ == '__main__':
    main(*sys.argv[1:3])

#!/usr/bin/env python3
"""Build the distributable kit (wrng-kit.zip) that tools/wrng_build.py turns into wrng.neo with the user's own
World Rally ROMs.  The kit holds only the port's own work: the runtime binary (game.bin) and the Z80 driver
with every region that comes from the arcade ROMs zeroed (the DS5002FP image, the tables computed from the
graphics, the sound and the program), the fix font, the conversion scripts, and a manifest with the SHA-1 each
zeroed region must have once rebuilt (the user's set is checked against it).

usage: mkkit.py <build dir> <out.zip>
"""
import hashlib
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from wrng_build import ROMS, parse_inc  # noqa: E402

# kit path -> source path (relative to the wrng directory)
SCRIPTS = {
    'tools/mkrom.py': 'tools/mkrom.py',
    'tools/gfx2crom.py': 'tools/gfx2crom.py',
    'tools/oki2adpcma.py': 'tools/oki2adpcma.py',
    'tools/mksrch.py': 'tools/mksrch.py',
    'tools/mkneo.py': 'tools/mkneo.py',
    'tools/mksoftlist.py': 'tools/mksoftlist.py',
    'analysis/gaelcrypt.py': 'analysis/gaelcrypt.py',
    'analysis/absrefs.csv': 'analysis/absrefs.csv',
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
    m1 = bytearray(rd('wrng-m1.m1'))
    regions = []

    def cut(blob, name, offset, data, source):
        assert blob[offset:offset + len(data)] == data, (name, source)
        regions.append({'file': name, 'offset': offset, 'length': len(data), 'source': source, 'sha1': sha1(data)})
        blob[offset:offset + len(data)] = bytes(len(data))

    cut(game, 'game.bin', syms['mcu_rom'], bytes(game[syms['mcu_rom']:syms['mcu_rom'] + 0x4090]), 'wrdallas.bin:0:0x4090')
    cut(game, 'game.bin', syms['pen0_table'], rd('pen0.bin'), 'pen0.bin')
    cut(game, 'game.bin', syms['sample_dur'], parse_inc(open(os.path.join(build, 'sample_dur.inc')).read()), 'sample_dur.inc')
    cut(game, 'game.bin', syms['srch_idx'], parse_inc(open(os.path.join(build, 'srch_idx.inc')).read()), 'srch_idx.inc')
    samples = parse_inc(open(os.path.join(build, 'samples.inc')).read())
    at = m1.find(samples)
    assert at >= 0 and m1.find(samples, at + 1) < 0, 'samples table not found once in the M1 ROM'
    cut(m1, 'wrng-m1.m1', at, samples, 'samples.inc')
    neo = open(os.path.join(build, 'release', 'wrng.neo'), 'rb').read()
    manifest = {
        'version': 1,
        'roms': {k: v for k, v in ROMS.items()},
        'gfx2crom_flags': ['--text', '4639-4651', '--rescue', '--shadow', '--light', '--extra', 'logo.bin'],
        'regions': regions,
        'neo_sha1': sha1(neo),
        'neo_name': 'World Rally NG v' + open('VERSION').read().strip(),
    }
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps(manifest, indent=1))
        z.writestr('game.bin', bytes(game))
        z.writestr('game.sym', rd('game.sym'))
        z.writestr('wrng-m1.m1', bytes(m1))
        z.writestr('wrng-s1.s1', rd('wrng-s1.s1'))
        z.writestr('logo.bin', rd('logo.bin'))           # the start-up screen (tools/mklogo.py)
        z.writestr('stub_sizes.json', rd('stub_sizes.json'))
        for dst, src in SCRIPTS.items():
            z.write(os.path.join(root, src), dst)
    print('wrote %s (%d regions rebuilt from the user ROMs)' % (out, len(regions)))


if __name__ == '__main__':
    main(*sys.argv[1:3])

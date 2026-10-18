#!/usr/bin/env python3
"""World Rally NG - builds the NeoSD .neo file (and, optionally, the MAME set) from the port kit and your own
copy of the World Rally arcade ROMs (the MAME set "wrallyd": merged in wrally.zip, or split in wrally.zip +
wrallyd.zip; zips or folders, the files are found by CRC32 whatever their names).

    python3 wrng_build.py wrng-kit.zip wrally.zip [wrallyd.zip ...] [-o wrng.neo] [--mame wrng.zip]

The Neo Geo BIOS is not needed: the .neo holds only the cartridge; the NeoSD runs it with the console's BIOS
(and MAME with its own neogeo.zip).  Needs Python 3.8+ only (no extra packages).
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

# arcade file -> CRC32 (MAME "wrallyd": its program ROMs, the rest shared with the parent "wrally")
ROMS = {
    'worldr17.c23': '050f5629',     # 68000 program, even bytes
    'worldr16.c22': '9e0d126c',     # 68000 program, odd bytes
    'worldr21.i13': 'b7fddb12',     # graphics
    'worldr20.i11': '58b2809a',
    'worldr19.i09': '018b35bb',
    'worldr18.i07': 'b37c807e',
    'worldr14.c01': 'e931c2ee',     # OKI samples
    'worldr15.c03': '11f0fe2c',
    'wrdallas.bin': '547d1768',     # DS5002FP program/data
}


def parse_inc(text):
    """Bytes of an assembler include made of .db / .byte / .word / .long lines (68000 and Z80 alike: words and
    longs big-endian, as the 68000 tables are; the Z80 table only uses .db)."""
    out = bytearray()
    for line in text.splitlines():
        line = line.split(';')[0].strip()
        if not line or line.startswith('*'):
            continue
        op, _, args = line.partition(' ')
        vals = [int(v.strip(), 0) for v in args.split(',') if v.strip()]
        if op in ('.db', '.byte'):
            out += bytes(v & 0xFF for v in vals)
        elif op == '.word':
            out += b''.join(struct.pack('>H', v & 0xFFFF) for v in vals)
        elif op == '.long':
            out += b''.join(struct.pack('>I', v & 0xFFFFFFFF) for v in vals)
        else:
            raise ValueError('unexpected line: ' + line)
    return bytes(out)


def find_roms(paths):
    """CRC32 -> data, from zips and folders (recursively)."""
    want = set(ROMS.values())
    found = {}

    def take(data):
        crc = '%08x' % (zlib.crc32(data) & 0xFFFFFFFF)
        if crc in want:
            found[crc] = data

    def scan_zip(zf):
        for info in zf.infolist():
            if '%08x' % info.CRC in want and '%08x' % info.CRC not in found:
                take(zf.read(info))

    for p in paths:
        if os.path.isdir(p):
            for d, _, files in os.walk(p):
                for f in files:
                    fp = os.path.join(d, f)
                    if f.lower().endswith('.zip'):
                        with zipfile.ZipFile(fp) as z:
                            scan_zip(z)
                    elif os.path.getsize(fp) in (32768, 524288):
                        take(open(fp, 'rb').read())
        elif zipfile.is_zipfile(p):
            with zipfile.ZipFile(p) as z:
                scan_zip(z)
        else:
            take(open(p, 'rb').read())
    return found


def run(tool, *args):
    r = subprocess.run([sys.executable, '-I', tool] + [str(a) for a in args], capture_output=True, text=True)
    if r.returncode:
        sys.exit('error in %s:\n%s%s' % (os.path.basename(tool), r.stdout, r.stderr))


def main():
    ap = argparse.ArgumentParser(description='World Rally NG: kit + your World Rally ROMs -> .neo')
    ap.add_argument('kit', help='wrng-kit.zip')
    ap.add_argument('roms', nargs='+', help='wrally.zip / wrallyd.zip (MAME), or folders with the ROM files')
    ap.add_argument('-o', '--out', default='wrng.neo', help='output .neo (default: wrng.neo)')
    ap.add_argument('--mame', metavar='ZIP', help='also write the MAME set (neogeo software "wrng") as this zip')
    a = ap.parse_args()

    found = find_roms(a.roms)
    missing = [n for n, c in ROMS.items() if c not in found]
    if missing:
        sys.exit('missing ROM files (MAME set wrallyd / wrally): ' + ', '.join(missing))
    print('ROMs found: %d/%d' % (len(found), len(ROMS)))

    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(a.kit) as z:
            z.extractall(tmp)
        man = json.load(open(os.path.join(tmp, 'manifest.json')))
        T = lambda *p: os.path.join(tmp, *p)
        rom = {n: found[c] for n, c in ROMS.items()}
        for n, d in rom.items():
            open(T(n), 'wb').write(d)
        cpu = bytearray(len(rom['worldr17.c23']) * 2)
        cpu[0::2] = rom['worldr17.c23']
        cpu[1::2] = rom['worldr16.c22']
        open(T('wr_cpu.bin'), 'wb').write(cpu)

        print('graphics...')
        run(T('tools', 'gfx2crom.py'), T('worldr21.i13'), T('worldr20.i11'), T('worldr19.i09'), T('worldr18.i07'),
            T('wrng-c1.c1'), T('wrng-c2.c2'), *man['gfx2crom_flags'], '--pen0', T('pen0.bin'))
        print('sound...')
        run(T('tools', 'oki2adpcma.py'), T('worldr14.c01'), T('worldr15.c03'), T('wr_cpu.bin'), T('wrng-v1.v1'),
            T('samples.inc'), T('okimap.csv'))
        run(T('tools', 'mksrch.py'), T('wr_cpu.bin'), T('srch_idx.inc'))

        src = {
            'wrdallas.bin:0:0x4090': rom['wrdallas.bin'][:0x4090],
            'pen0.bin': open(T('pen0.bin'), 'rb').read(),
            'sample_dur.inc': parse_inc(open(T('sample_dur.inc')).read()),
            'srch_idx.inc': parse_inc(open(T('srch_idx.inc')).read()),
            'samples.inc': parse_inc(open(T('samples.inc')).read()),
        }
        blobs = {f: bytearray(open(T(f), 'rb').read()) for f in {r['file'] for r in man['regions']}}
        for r in man['regions']:
            data = src[r['source']]
            if len(data) != r['length'] or hashlib.sha1(data).hexdigest() != r['sha1']:
                sys.exit('%s does not match the expected World Rally set (is it MAME "wrallyd"?)' % r['source'])
            blobs[r['file']][r['offset']:r['offset'] + r['length']] = data
        for f, b in blobs.items():
            open(T(f), 'wb').write(b)

        print('program...')
        run(T('tools', 'mkrom.py'), 'build', T('wr_cpu.bin'), T('analysis', 'absrefs.csv'), T('game.bin'),
            T('game.sym'), T('wrng-p1.p1'))

        # MAME on-disk convention (P ROM word-swapped), then the .neo
        stage = T('roms', 'wrng')
        os.makedirs(stage)
        for suffix in ('p1', 's1', 'm1', 'v1', 'c1', 'c2'):
            d = open(T('wrng-%s.%s' % (suffix, suffix)), 'rb').read()
            if suffix == 'p1':
                sw = bytearray(d)
                sw[0::2] = d[1::2]
                sw[1::2] = d[0::2]
                d = bytes(sw)
            open(os.path.join(stage, 'wrng-%s.%s' % (suffix, suffix)), 'wb').write(d)
        extra = ['--zip', os.path.abspath(a.mame)] if a.mame else []
        run(T('tools', 'mkneo.py'), stage, 'wrng', os.path.abspath(a.out), '-n', man['neo_name'], *extra)
        if a.mame:
            hashfile = os.path.join(os.path.dirname(os.path.abspath(a.mame)), 'neogeo.xml')
            sys.path.insert(0, T('tools'))
            import mksoftlist                   # its main() on the staged files (the script would stage again)
            mksoftlist.main(stage, 'wrng', man['neo_name'], hashfile)

    ok = hashlib.sha1(open(a.out, 'rb').read()).hexdigest() == man['neo_sha1']
    print('wrote %s%s' % (a.out, ' (identical to the official build)' if ok else ''))
    if a.mame:
        print('wrote %s and neogeo.xml beside it (MAME: mame neogeo wrng -rompath <folder of the zip> -hashpath '
              '<folder of neogeo.xml>)' % a.mame)


if __name__ == '__main__':
    main()

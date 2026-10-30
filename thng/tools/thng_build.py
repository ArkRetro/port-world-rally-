#!/usr/bin/env python3
"""Thunder Hoop NG - builds the NeoSD / MiSTer .neo file (and, optionally, the MAME set) from the port kit and
your own copy of the Thunder Hoop arcade ROMs (the MAME set "thoop": thoop.zip, or a folder with the files; they
are found by CRC32 whatever their names).

    python3 thng_build.py thng-kit.zip thoop.zip [-o thng.neo] [--mame thng.zip]

The Neo Geo BIOS is not needed: the .neo holds only the cartridge (the NeoSD / MiSTer run it with the BIOS of
the console / core).  Needs Python 3.8+ only (no extra packages).
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

# arcade file -> CRC32 (MAME "thoop")
ROMS = {
    'th18dea1.040': '59bad625',     # 68000 program, even bytes
    'th161eb4.020': '6add61ed',     # 68000 program, odd bytes
    'c09': '06f0edbf',              # graphics (4 x 1 MB)
    'c10': '2d227085',
    'c11': '7403ef7e',
    'c12': '29a5ca36',
    'sound': '99f80961',            # OKI samples
}


def parse_inc(text):
    """Bytes of an assembler include made of .db / .byte / .word / .long lines (68000 and Z80 alike: words and
    longs big-endian; the Z80 table only uses .db).  Comments: ';' and '*' at the start of a line."""
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
                    elif os.path.getsize(fp) in (262144, 524288, 1048576):
                        take(open(fp, 'rb').read())
        elif zipfile.is_zipfile(p):
            with zipfile.ZipFile(p) as z:
                scan_zip(z)
        else:
            take(open(p, 'rb').read())
    return found


def run(tool, *args):
    if sys.platform == 'emscripten':        # in the browser (thng.html, Pyodide): no processes, run it in place
        import runpy
        saved = sys.argv, list(sys.path)
        sys.argv = [tool] + [str(a) for a in args]
        try:
            runpy.run_path(tool, run_name='__main__')
        except SystemExit as e:
            if e.code not in (None, 0):
                sys.exit('error in %s: %s' % (os.path.basename(tool), e.code))
        finally:
            sys.argv, sys.path[:] = saved
        return
    r = subprocess.run([sys.executable, '-I', tool] + [str(a) for a in args], capture_output=True, text=True)
    if r.returncode:
        sys.exit('error in %s:\n%s%s' % (os.path.basename(tool), r.stdout, r.stderr))


def main():
    ap = argparse.ArgumentParser(description='Thunder Hoop NG: kit + your Thunder Hoop ROMs -> .neo')
    ap.add_argument('kit', help='thng-kit.zip')
    ap.add_argument('roms', nargs='+', help='thoop.zip (MAME), or folders with the ROM files')
    ap.add_argument('-o', '--out', default='thng.neo', help='output .neo (default: thng.neo)')
    ap.add_argument('--mame', metavar='ZIP', help='also write the MAME set (neogeo software "thng") as this zip')
    a = ap.parse_args()

    found = find_roms(a.roms)
    missing = [n for n, c in ROMS.items() if c not in found]
    if missing:
        sys.exit('missing ROM files (MAME set thoop): ' + ', '.join(missing))
    print('ROMs found: %d/%d' % (len(found), len(ROMS)))

    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(a.kit) as z:
            z.extractall(tmp)
        man = json.load(open(os.path.join(tmp, 'manifest.json')))
        T = lambda *p: os.path.join(tmp, *p)
        rom = {n: found[c] for n, c in ROMS.items()}
        for n, d in rom.items():
            open(T(n), 'wb').write(d)
        cpu = bytearray(0x100000)
        cpu[0:2 * len(rom['th18dea1.040']):2] = rom['th18dea1.040']
        cpu[1:2 * len(rom['th161eb4.020']):2] = rom['th161eb4.020']
        open(T('th_cpu.bin'), 'wb').write(cpu)

        print('graphics...')
        run(T('tools', 'gfx2crom.py'), T('c09'), T('c10'), T('c11'), T('c12'), T('thng-c1.c1'), T('thng-c2.c2'),
            T('map8.bin'), '--fix', T('font.s1'), T('thng-s1.s1'))
        print('sound...')
        run(T('tools', 'oki2adpcma.py'), T('sound'), T('th_cpu.bin'), T('thng-v1.v1'), T('samples.inc'), T('okimap.csv'))

        # the regions of the kit that come from the arcade ROMs
        sys.path.insert(0, T('tools'))
        import mkrom                                       # noqa: E402 (stub_blob: the stubs of the VRAM write sites)
        syms = {}
        for line in open(T('game.sym')):
            p = line.split()
            if len(p) == 3:
                syms[p[2]] = int(p[0], 16)
        stubs, _ = mkrom.stub_blob(cpu, T('analysis', 'vram_sites.txt'), syms)
        src = {
            'stubs': stubs,
            'map8.bin': open(T('map8.bin'), 'rb').read(),
            'sample_dur.inc': parse_inc(open(T('sample_dur.inc')).read()),
            'samples.inc': parse_inc(open(T('samples.inc')).read()),
        }
        blobs = {f: bytearray(open(T(f), 'rb').read()) for f in {r['file'] for r in man['regions']}}
        for r in man['regions']:
            data = src[r['source']]
            if len(data) != r['length'] or hashlib.sha1(data).hexdigest() != r['sha1']:
                sys.exit('%s does not match the expected Thunder Hoop set (is it MAME "thoop"?)' % r['source'])
            blobs[r['file']][r['offset']:r['offset'] + r['length']] = data
        for f, b in blobs.items():
            open(T(f), 'wb').write(b)

        print('program...')
        run(T('tools', 'mkrom.py'), 'build', T('th_cpu.bin'), T('analysis'), T('game.bin'), T('game.sym'), T('thng-p1.p1'))

        import mksoftlist                                   # noqa: E402
        stage = mksoftlist.stage(tmp, 'thng')               # MAME on-disk convention (P ROM word-swapped)
        extra = ['--zip', os.path.abspath(a.mame)] if a.mame else []
        run(T('tools', 'mkneo.py'), stage, 'thng', os.path.abspath(a.out), '-n', man['neo_name'], *extra)
        if a.mame:
            hashfile = os.path.join(os.path.dirname(os.path.abspath(a.mame)), 'neogeo.xml')
            mksoftlist.main(tmp, 'thng', man['neo_name'], hashfile)

    ok = hashlib.sha1(open(a.out, 'rb').read()).hexdigest() == man['neo_sha1']
    print('wrote %s%s' % (a.out, ' (identical to the official build)' if ok else ''))
    if a.mame:
        print('wrote %s and neogeo.xml beside it (MAME: mame neogeo thng -rompath <folder of the zip> -hashpath '
              '<folder of neogeo.xml>)' % a.mame)


if __name__ == '__main__':
    main()

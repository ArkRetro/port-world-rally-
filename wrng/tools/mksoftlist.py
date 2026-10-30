#!/usr/bin/env python3
"""Write a MAME neogeo software list with one item for the built ROM set.
Usage: mksoftlist.py <build dir> <item name> <description> <out xml>
Expects <build>/<item>-p1.p1, -s1.s1, -m1.m1, -v1.v1, -c1.c1, -c2.c2
"""
import hashlib
import os
import sys
import zlib


def info(path):
    d = open(path, 'rb').read()
    return len(d), '%08x' % (zlib.crc32(d) & 0xffffffff), hashlib.sha1(d).hexdigest()


def main(build, item, desc, out):
    regions = [('maincpu', 'p1', 'p1', 'load16_word_swap'), ('fixed', 's1', 's1', None), ('audiocpu', 'm1', 'm1', None),
               ('ymsnd:adpcma', 'v1', 'v1', None)]
    parts = []
    for region, suffix, ext, loadflag in regions:
        f = os.path.join(build, f'{item}-{suffix}.{ext}')
        size, crc, sha = info(f)
        lf = f' loadflag="{loadflag}"' if loadflag else ''
        attrs = ' width="16" endianness="big"' if region == 'maincpu' else ''
        parts.append(f'\t\t\t<dataarea name="{region}"{attrs} size="0x{size:x}">\n'
                     f'\t\t\t\t<rom name="{item}-{suffix}.{ext}" size="0x{size:x}" crc="{crc}" sha1="{sha}" offset="0x000000"{lf} />\n'
                     f'\t\t\t</dataarea>\n')
    c1 = os.path.join(build, f'{item}-c1.c1')
    c2 = os.path.join(build, f'{item}-c2.c2')
    s1, crc1, sha1 = info(c1)
    s2, crc2, sha2 = info(c2)
    parts.append(f'\t\t\t<dataarea name="sprites" size="0x{s1 + s2:x}">\n'
                 f'\t\t\t\t<rom name="{item}-c1.c1" size="0x{s1:x}" crc="{crc1}" sha1="{sha1}" offset="0x000000" loadflag="load16_byte" />\n'
                 f'\t\t\t\t<rom name="{item}-c2.c2" size="0x{s2:x}" crc="{crc2}" sha1="{sha2}" offset="0x000001" loadflag="load16_byte" />\n'
                 f'\t\t\t</dataarea>\n')
    xml = ('<?xml version="1.0"?>\n<!DOCTYPE softwarelist SYSTEM "softwarelist.dtd">\n<softwarelist name="neogeo" description="SNK Neo Geo (test)">\n'
           f'\t<software name="{item}">\n\t\t<description>{desc}</description>\n\t\t<year>2026</year>\n\t\t<publisher>homebrew</publisher>\n'
           '\t\t<sharedfeat name="compatibility" value="MVS,AES" />\n'
           '\t\t<part name="cart" interface="neo_cart">\n' + ''.join(parts) + '\t\t</part>\n\t</software>\n</softwarelist>\n')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'w').write(xml)
    print('wrote', out)


def stage(build, item):
    """Copy the ROMs into <build>/roms/<item>/ in MAME's on-disk convention: the P ROM byte-swapped
    (loadflag load16_word_swap), everything else as-is.  Returns the staging directory."""
    d = os.path.join(build, 'roms', item)
    os.makedirs(d, exist_ok=True)
    for suffix, ext in (('p1', 'p1'), ('s1', 's1'), ('m1', 'm1'), ('v1', 'v1'), ('c1', 'c1'), ('c2', 'c2')):
        data = open(f'{build}/{item}-{suffix}.{ext}', 'rb').read()
        if suffix == 'p1':
            sw = bytearray(data)
            sw[0::2] = data[1::2]
            sw[1::2] = data[0::2]
            data = bytes(sw)
        open(os.path.join(d, f'{item}-{suffix}.{ext}'), 'wb').write(data)
    return d


if __name__ == '__main__':
    build, item, desc, out = sys.argv[1:5]
    main(stage(build, item), item, desc, out)

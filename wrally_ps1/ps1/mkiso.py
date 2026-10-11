"""Genera las imagenes de CD (USA para MiSTer, EUR para la PSone) con datos y pistas de audio"""
import subprocess, sys, os
LIC = {'USA': 'Sony Computer Entertainment Amer  ica ', 'EUR': 'Sony Computer Entertainment Euro pe   '}
tracks = [l.strip() for l in open('../snd/tracks.txt') if l.strip()]
files = ['SYSTEM.CNF', 'WRPS1.EXE', 'TILES.BIN'] + ['LEVEL%02d.BIN' % k for k in range(12)] + ['RACE.BIN', 'SNDFX.BIN']
for reg, txt in LIC.items():
    t = b'          Licensed  by          ' + txt.encode()
    sec = []
    for i in range(12):
        d = bytearray(2048)
        if i == 4: d[:len(t)] = t
        sec.append(bytes([0, 0, 8, 0, 0, 0, 8, 0]) + bytes(d) + bytes(280))
    open('cd_lic_%s.dat' % reg, 'wb').write(b''.join(sec))
    x = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<iso_project image_name="wrps1_%s.bin" cue_sheet="wrps1_%s.cue">' % (reg, reg),
         '  <track type="data">',
         '    <license file="cd_lic_%s.dat"/>' % reg,
         '    <identifiers system="PLAYSTATION" application="PLAYSTATION" volume="WRPS1" volume_set="WRPS1" publisher="ARKRETRO" data_preparer="ARKRETRO" copyright="ARKRETRO"/>',
         '    <directory_tree>']
    x += ['      <file name="%s" type="data" source="cd/%s"/>' % (f, f) for f in files]
    x += ['      <dummy sectors="1024"/>', '    </directory_tree>', '  </track>']
    x += ['  <track type="audio" source="../snd/%s"/>' % t for t in tracks]
    x += ['</iso_project>']
    open('iso_%s.xml' % reg, 'w').write('\n'.join(x) + '\n')
    subprocess.check_call([os.environ.get('MKPSXISO', 'mkpsxiso'), '-y', 'iso_%s.xml' % reg], stdout=subprocess.DEVNULL)
    print(reg, 'ok')

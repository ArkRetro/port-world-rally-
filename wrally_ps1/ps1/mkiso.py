"""Genera las imagenes de CD (USA para MiSTer, EUR para la PSone) con datos y pistas de audio.
Lo que se lee al arrancar va seguido al principio para leerlo de una vez (tabla de pistas, efectos, paquete de
menus) y detras lo que se lee de fondo (paquete de carrera y resto de menus).
TRACKS.BIN lleva el principio y el final exactos de cada pista de audio (GetTD solo da minutos y segundos): se
construye la imagen, se lee la disposicion del .cue, se escribe la tabla y se vuelve a construir (TRACKS.BIN no
cambia de tamano, asi que la disposicion es la misma)."""
import subprocess, sys, os, struct
import licencia
LIC = licencia.TXT
tracks = [l.strip() for l in open('../snd/tracks.txt') if l.strip()]
# al arrancar: tabla de pistas y menus (seguidos); de fondo: efectos, carrera y menus 2 (seguidos). Detras de cada
# recorrido, una copia de RACE.BIN: lo que falte de el en la carga se lee sin volver a buscar. Las copias se llaman
# ZRACEnn para que vayan al final del directorio: la BIOS solo lee el primer sector del directorio raiz y ahi tienen
# que estar SYSTEM.CNF y el ejecutable (si no, se queda en el logo)
files = ['SYSTEM.CNF', 'LOGO.BIN', 'TRACKS.BIN', 'MENU.BIN', 'SNDFX.BIN', 'RACE.BIN', 'MENU2.BIN', 'POST.BIN', 'POST2.BIN', 'WRPS1.EXE', 'TILES.BIN']
for k in range(12): files += ['LEVEL%02d.BIN' % k, 'ZRACE%02d.BIN=RACE.BIN' % k]
def build(reg):
    open('cd_lic_%s.dat' % reg, 'wb').write(lic[reg])    # sectores 0-11: licencia y logo (licencia.py)
    x = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<iso_project image_name="wrps1_%s.bin" cue_sheet="wrps1_%s.cue">' % (reg, reg),
         '  <track type="data">',
         '    <license file="cd_lic_%s.dat"/>' % reg,
         '    <identifiers system="PLAYSTATION" application="PLAYSTATION" volume="WRPS1" volume_set="WRPS1" publisher="ARKRETRO" data_preparer="ARKRETRO" copyright="ARKRETRO"/>',
         '    <directory_tree>']
    x += ['      <file name="%s" type="data" source="cd/%s"/>' % (f.split('=')[0], f.split('=')[-1]) for f in files]
    x += ['      <dummy sectors="1024"/>', '    </directory_tree>', '  </track>']
    x += ['  <track type="audio" source="../snd/%s"/>' % t for t in tracks]
    x += ['</iso_project>']
    open('iso_%s.xml' % reg, 'w').write('\n'.join(x) + '\n')
    subprocess.check_call([os.environ.get('MKPSXISO', 'mkpsxiso'), '-y', 'iso_%s.xml' % reg], stdout=subprocess.DEVNULL)
def check_dir(reg):
    """SYSTEM.CNF y el ejecutable en el primer sector del directorio raiz (lo unico que lee la BIOS)"""
    f = open('wrps1_%s.bin' % reg, 'rb')
    def sec(n): f.seek(n * 2352 + 24); return f.read(2048)
    d = sec(le(sec(16), 158)); p = 0; nm = []
    while p < 2048 and d[p]: nm.append(d[p + 33:p + 33 + d[p + 32]].split(b';')[0].decode('latin1')); p += d[p]
    for n in ('SYSTEM.CNF', 'WRPS1.EXE'): assert n in nm, '%s: %s fuera del primer sector del directorio' % (reg, n)
def le(b, o): return struct.unpack_from('<I', b, o)[0]
def layout(reg):
    """pista -> (principio = indice 01, final = indice 00 de la siguiente), en sectores desde el principio del disco"""
    idx = {}; t = 0
    for l in open('wrps1_%s.cue' % reg):
        p = l.split()
        if p[:1] == ['TRACK']: t = int(p[1])
        if p[:1] == ['INDEX']:
            m, s, f = map(int, p[2].split(':'))
            idx.setdefault(t, {})[int(p[1])] = (m * 60 + s) * 75 + f
    end = os.path.getsize('wrps1_%s.bin' % reg) // 2352
    tab = {}
    for t in sorted(idx):
        nxt = idx.get(t + 1)
        tab[t] = (idx[t][1], (nxt.get(0, nxt[1]) if nxt else end))
    return tab
def write_tracks(tab):
    d = bytearray(2048)
    for t, (a, b) in tab.items():
        if 2 <= t < 32: struct.pack_into('<II', d, t * 8, a, b)
    open('cd/TRACKS.BIN', 'wb').write(bytes(d))
LIC = [r for r in LIC if r in os.environ.get('REGIONES', ' '.join(LIC)).split()]
lic = {reg: licencia.datos(reg) for reg in LIC}
write_tracks({})
build(LIC[0])
tab = layout(LIC[0])
write_tracks(tab)
for reg in LIC:
    build(reg)
    assert layout(reg) == tab, 'disposicion distinta en %s' % reg
    check_dir(reg)
    print(reg, 'ok')
print('pistas', ' '.join('%d:%d-%d' % (t, a, b) for t, (a, b) in sorted(tab.items()) if t >= 2))

"""Imagen del disco con los dos World Rally (USA para MiSTer y consolas americanas, EUR para las PAL) con los ficheros
de cada port tal como los dejan sus build.sh (wrally_ps1/ps1/cd y wrally2_ps1/ps1/cd) y la lista de su ultima imagen
(iso_USA.xml de cada uno: los ficheros en el mismo orden y, del WR1, las pistas de audio).
 - Raiz: SYSTEM.CNF (arranca INICIO.EXE, el menu), INICIO.EXE y los del WR1 con su EXE (WRPS1.EXE). El WR1 busca sus
   ficheros en la raiz y la BIOS solo lee el primer sector del directorio raiz: alli tienen que estar SYSTEM.CNF y los
   dos EXE que lanza (se comprueba al acabar).
 - Carpeta WR2: los del WR2 (su cd.c los busca alli si la carpeta esta; TILES.BIN se llama igual que el del WR1). El
   menu lanza cdrom:\\WR2\\WR2PS1.EXE;1.
 - Las pistas de CD-DA del WR1 detras de la de datos. TRACKS.BIN (principio y final de cada una) se rehace para este
   disco como en el mkiso.py del WR1: se construye, se lee la disposicion del .cue, se escribe la tabla y se vuelve a
   construir (TRACKS.BIN no cambia de tamano).
Para la tarjeta de un emulador de lector deja en sd/ '<nombre> (USA)' con .bin y .cue (XStation; tambien CD grabado,
MiSTer) y '<nombre> (USA) PSIO' con otra imagen (el EXE del WR1 para PSIO, WRPS1_PSIO.EXE) y su .cu2 (SD_NOMBRE, por
defecto 'World Rally 1 y 2 PS1'; REGIONES='USA EUR' por defecto; PSIO=0: sin la de PSIO). Los sectores de licencia
salen de licencia.py del WR1 (LICENCIA_USA / LICENCIA_EUR o LICENCIA). Uso personal."""
import os, re, sys, struct, shutil, subprocess
AQUI = os.path.dirname(os.path.abspath(__file__))
WR1 = os.path.join(AQUI, '..', 'wrally_ps1', 'ps1'); WR2 = os.path.join(AQUI, '..', 'wrally2_ps1', 'ps1')
sys.path.insert(0, WR1)
import licencia
os.chdir(AQUI)
IMG = 'wr12ps1_%s'


def lista(xml):
    """(nombre, fuente) de los ficheros y fuentes de las pistas de audio de un iso_*.xml de mkpsxiso"""
    t = open(xml).read()
    fich = re.findall(r'<file name="([^"]+)" type="data" source="([^"]+)"/>', t)
    audio = re.findall(r'<track type="audio" source="([^"]+)"/>', t)
    return fich, audio


f1, audio = lista(os.path.join(WR1, 'iso_USA.xml'))
f2, a2 = lista(os.path.join(WR2, 'iso_USA.xml'))
assert not a2 and f1 and f2, 'faltan las imagenes de los dos ports (build.sh de cada uno)'
assert ('WRPS1.EXE' in dict(f1)) and ('WR2PS1.EXE' in dict(f2)), 'los EXE de los ports se llaman distinto'


def ruta(base, f): return os.path.normpath(os.path.join(base, f))


def build(reg, img, psio):
    open('cd_lic_%s.dat' % reg, 'wb').write(lic[reg])        # sectores 0-11: licencia y logo (licencia.py)
    x = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<iso_project image_name="%s.bin" cue_sheet="%s.cue">' % (img, img),
         '  <track type="data">',
         '    <license file="cd_lic_%s.dat"/>' % reg,
         '    <identifiers system="PLAYSTATION" application="PLAYSTATION" volume="WR12PS1" volume_set="WR12PS1" publisher="ARKRETRO" data_preparer="ARKRETRO" copyright="ARKRETRO"/>',
         '    <directory_tree>',
         '      <file name="SYSTEM.CNF" type="data" source="SYSTEM.CNF"/>',
         '      <file name="INICIO.EXE" type="data" source="cd/INICIO.EXE"/>']
    for n, s in f1:
        if n == 'SYSTEM.CNF': continue
        if n == 'TRACKS.BIN': s = 'cd/TRACKS.BIN'
        elif n == 'WRPS1.EXE': s = ruta(WR1, 'cd/WRPS1_PSIO.EXE' if psio else 'cd/WRPS1.EXE')
        else: s = ruta(WR1, s)
        x.append('      <file name="%s" type="data" source="%s"/>' % (n, s))
    x.append('      <dir name="WR2">')
    x += ['        <file name="%s" type="data" source="%s"/>' % (n, ruta(WR2, s)) for n, s in f2 if n != 'SYSTEM.CNF']
    x += ['      </dir>', '      <dummy sectors="1024"/>', '    </directory_tree>', '  </track>']
    x += ['  <track type="audio" source="%s"/>' % ruta(WR1, a) for a in audio]
    x += ['</iso_project>']
    open('iso_%s.xml' % reg, 'w').write('\n'.join(x) + '\n')
    for e in ('.bin', '.cue'):              # mkpsxiso reescribe el fichero sin crear otro: la copia de sd/ es un enlace
        if os.path.exists(img + e): os.remove(img + e)
    subprocess.check_call([os.environ.get('MKPSXISO', 'mkpsxiso'), '-y', 'iso_%s.xml' % reg], stdout=subprocess.DEVNULL)


def le(b, o): return struct.unpack_from('<I', b, o)[0]


def check_dir(img):
    """SYSTEM.CNF y los EXE en el primer sector de su directorio (lo unico que lee la BIOS)"""
    f = open('%s.bin' % img, 'rb')
    def sec(n): f.seek(n * 2352 + 24); return f.read(2048)
    def nombres(d):
        p = 0; nm = {}
        while p < 2048 and d[p]:
            nm[d[p + 33:p + 33 + d[p + 32]].split(b';')[0].decode('latin1')] = le(d, p + 2); p += d[p]
        return nm
    raiz = nombres(sec(le(sec(16), 158)))
    for n in ('SYSTEM.CNF', 'INICIO.EXE', 'WRPS1.EXE', 'WR2'):
        assert n in raiz, '%s: %s fuera del primer sector del directorio raiz' % (img, n)
    assert 'WR2PS1.EXE' in nombres(sec(raiz['WR2'])), '%s: WR2PS1.EXE fuera del primer sector de WR2' % img


def cue_idx(img):
    """pista -> {indice: sector desde el principio del .bin} (del .cue de mkpsxiso)"""
    idx = {}; t = 0
    for l in open('%s.cue' % img):
        p = l.split()
        if p[:1] == ['TRACK']: t = int(p[1])
        if p[:1] == ['INDEX']:
            m, s, f = map(int, p[2].split(':'))
            idx.setdefault(t, {})[int(p[1])] = (m * 60 + s) * 75 + f
    return idx


def layout(img):
    """pista -> (principio = indice 01, final = indice 00 de la siguiente), en sectores desde el principio del disco"""
    idx = cue_idx(img); end = os.path.getsize('%s.bin' % img) // 2352; tab = {}
    for t in sorted(idx):
        nxt = idx.get(t + 1)
        tab[t] = (idx[t][1], (nxt.get(0, nxt[1]) if nxt else end))
    return tab


def write_tracks(tab):
    d = bytearray(2048)
    for t, (a, b) in tab.items():
        if 2 <= t < 32: struct.pack_into('<II', d, t * 8, a, b)
    open('cd/TRACKS.BIN', 'wb').write(bytes(d))


def msf(n, alt=False):
    """MM:SS:FF; alt: notacion de Systems Console (PSIO): un cuadro 0 se escribe SS-1:75 (como el mkiso.py del WR1)"""
    m, s, f = n // 4500, n // 75 % 60, n % 75
    if alt and f == 0:
        f = 75
        if s: s -= 1
        else: m, s = m - 1, 59
    return '%02d:%02d:%02d' % (m, s, f)


def cu2(img):
    """hoja .cu2 de PSIO, revision 2 (firmware 2.6.11 o posterior), como la del mkiso.py del WR1"""
    idx = cue_idx(img); n = os.path.getsize('%s.bin' % img) // 2352
    L = ['ntracks %d' % len(idx), 'size      ' + msf(n), 'data1     00:02:00']
    for t in range(2, len(idx) + 1):
        L.append('pregap%02d  %s' % (t, msf(idx[t].get(0, idx[t][1]) + 150, True)))
        L.append('track%02d   %s' % (t, msf(idx[t][1] + 150, True)))
    L += ['', 'trk end   ' + msf(n + 150, True)]
    return '\r\n'.join(L).encode('ascii')


SD = os.environ.get('SD_NOMBRE', 'World Rally 1 y 2 PS1')
def sd_dir(n):
    assert len(n) <= 45 and n.isascii() and n.strip() == n and not set(n) & set('.\\/:*?"<>|'), \
        'SD_NOMBRE: hasta 45 caracteres ASCII en total, sin puntos ni \\ / : * ? " < > |: %r' % n
    d = os.path.join('sd', n)
    if os.path.isdir(d): shutil.rmtree(d)
    os.makedirs(d)
    return d
def put(src, dst):
    if os.path.exists(dst): os.remove(dst)
    try: os.link(src, dst)
    except OSError: shutil.copyfile(src, dst)
def sd(reg, psio):
    img = IMG % reg; n = '%s (%s)' % (SD, reg); d = sd_dir(n)
    put(img + '.bin', os.path.join(d, n + '.bin'))
    cue = open(img + '.cue').read()
    assert cue.count('"%s.bin"' % img) == 1, 'el .cue debe tener una sola linea FILE'
    cue = cue.replace('"%s.bin"' % img, '"%s.bin"' % n).replace('\r\n', '\n').replace('\n', '\r\n')
    open(os.path.join(d, n + '.cue'), 'wb').write(cue.encode('ascii'))
    if psio:
        n += ' PSIO'; d = sd_dir(n); img += '_psio'
        put(img + '.bin', os.path.join(d, n + '.bin'))
        open(os.path.join(d, n + '.cu2'), 'wb').write(cu2(img))


REG = [r for r in licencia.TXT if r in os.environ.get('REGIONES', ' '.join(licencia.TXT)).split()]
PSIO = os.environ.get('PSIO', '1') != '0'
lic = {reg: licencia.datos(reg) for reg in REG}
os.makedirs('cd', exist_ok=True)
write_tracks({})
build(REG[0], IMG % REG[0], False)
tab = layout(IMG % REG[0])
write_tracks(tab)
for reg in REG:
    for psio in (False, True) if PSIO else (False,):
        img = IMG % reg + ('_psio' if psio else '')
        build(reg, img, psio)
        assert layout(img) == tab, 'disposicion distinta en %s' % img
        check_dir(img)
    sd(reg, PSIO)
    print(reg, 'ok', '%s.bin %d KB' % (IMG % reg, os.path.getsize('%s.bin' % (IMG % reg)) >> 10))
print('pistas', ' '.join('%d:%d-%d' % (t, a, b) for t, (a, b) in sorted(tab.items()) if t >= 2))

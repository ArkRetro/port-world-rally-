"""Imagenes de CD del World Rally 2 (USA para MiSTer y consolas americanas, EUR para las PAL): una sola pista de datos
(toda la musica va en la SPU, sin CD-DA). Los sectores de licencia salen de licencia.py del port del WR1 (variables
LICENCIA_USA / LICENCIA_EUR o LICENCIA: tu BIOS, un fichero de licencia o un juego original; ver su cabecera).
Para una PS1 real con un emulador de lector deja en sd/ una carpeta por region y sistema, lista para copiar a la raiz
de la tarjeta (SD_NOMBRE, por defecto 'World Rally 2 PS1'): '<nombre> (USA)' con .bin y .cue (XStation; tambien CD
grabado, MiSTer) y '<nombre> (USA) PSIO' con .bin y .cu2 (la hoja de pistas de PSIO).
Ficheros del disco en orden: SYSTEM.CNF y el EXE en el primer sector del directorio (lo unico que lee la BIOS); los
demas, del manifiesto FICHEROS de mas abajo (los que existan en cd/)."""
import subprocess, os, struct, shutil, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'wrally_ps1', 'ps1'))
import licencia
EXE = 'WR2PS1.EXE'
FICHEROS = ['SYSTEM.CNF', EXE]
IMG = 'wr2ps1_%s'

def build(reg):
    img = IMG % reg
    open('cd_lic_%s.dat' % reg, 'wb').write(lic[reg])        # sectores 0-11: licencia y logo (licencia.py)
    x = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<iso_project image_name="%s.bin" cue_sheet="%s.cue">' % (img, img),
         '  <track type="data">',
         '    <license file="cd_lic_%s.dat"/>' % reg,
         '    <identifiers system="PLAYSTATION" application="PLAYSTATION" volume="WR2PS1" volume_set="WR2PS1" publisher="ARKRETRO" data_preparer="ARKRETRO" copyright="ARKRETRO"/>',
         '    <directory_tree>']
    x += ['      <file name="%s" type="data" source="cd/%s"/>' % (f, f) for f in FICHEROS if os.path.exists('cd/' + f)]
    x += ['      <dummy sectors="1024"/>', '    </directory_tree>', '  </track>', '</iso_project>']
    open('iso_%s.xml' % reg, 'w').write('\n'.join(x) + '\n')
    for e in ('.bin', '.cue'):              # mkpsxiso reescribe el fichero sin crear otro: la copia de sd/ es un enlace
        if os.path.exists(img + e): os.remove(img + e)
    subprocess.check_call([os.environ.get('MKPSXISO', 'mkpsxiso'), '-y', 'iso_%s.xml' % reg], stdout=subprocess.DEVNULL)

def le(b, o): return struct.unpack_from('<I', b, o)[0]
def check_dir(reg):
    """SYSTEM.CNF y el ejecutable en el primer sector del directorio raiz (lo unico que lee la BIOS)"""
    f = open('%s.bin' % (IMG % reg), 'rb')
    def sec(n): f.seek(n * 2352 + 24); return f.read(2048)
    d = sec(le(sec(16), 158)); p = 0; nm = []
    while p < 2048 and d[p]: nm.append(d[p + 33:p + 33 + d[p + 32]].split(b';')[0].decode('latin1')); p += d[p]
    for n in ('SYSTEM.CNF', EXE): assert n in nm, '%s: %s fuera del primer sector del directorio' % (reg, n)

def msf(n):
    return '%02d:%02d:%02d' % (n // 4500, n // 75 % 60, n % 75)
def cu2(img):
    """hoja .cu2 de PSIO (revision 2, como Cue2cu2) de una imagen de una sola pista de datos"""
    n = os.path.getsize('%s.bin' % img) // 2352
    return '\r\n'.join(['ntracks 1', 'size      ' + msf(n), 'data1     00:02:00', '', 'trk end   ' + msf(n + 150)]).encode('ascii')

SD = os.environ.get('SD_NOMBRE', 'World Rally 2 PS1')
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
def sd(reg):
    img = IMG % reg
    n = '%s (%s)' % (SD, reg); d = sd_dir(n)
    put(img + '.bin', os.path.join(d, n + '.bin'))
    cue = open(img + '.cue').read()
    assert cue.count('"%s.bin"' % img) == 1, 'el .cue debe tener una sola linea FILE'
    cue = cue.replace('"%s.bin"' % img, '"%s.bin"' % n).replace('\r\n', '\n').replace('\n', '\r\n')
    open(os.path.join(d, n + '.cue'), 'wb').write(cue.encode('ascii'))
    n += ' PSIO'; d = sd_dir(n)
    put(img + '.bin', os.path.join(d, n + '.bin'))
    open(os.path.join(d, n + '.cu2'), 'wb').write(cu2(img))

REG = [r for r in licencia.TXT if r in os.environ.get('REGIONES', ' '.join(licencia.TXT)).split()]
lic = {reg: licencia.datos(reg) for reg in REG}
for reg in REG:
    build(reg)
    check_dir(reg)
    sd(reg)
    print(reg, 'ok', '%s.bin %d KB' % (IMG % reg, os.path.getsize('%s.bin' % (IMG % reg)) >> 10))

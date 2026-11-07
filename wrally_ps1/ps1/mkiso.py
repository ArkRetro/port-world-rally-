"""Genera las imagenes de CD (USA para MiSTer, EUR para la PSone) con datos y pistas de audio.
Lo que se lee al arrancar va seguido al principio para leerlo de una vez (tabla de pistas, efectos, paquete de
menus) y detras lo que se lee de fondo (paquete de carrera y resto de menus).
TRACKS.BIN lleva el principio y el final exactos de cada pista de audio (GetTD solo da minutos y segundos): se
construye la imagen, se lee la disposicion del .cue, se escribe la tabla y se vuelve a construir (TRACKS.BIN no
cambia de tamano, asi que la disposicion es la misma).
Para una PS1 real con un emulador de lector (ODE) deja en sd/ una carpeta por region y sistema, lista para copiar a la
raiz de la tarjeta (SD_NOMBRE: el nombre, por defecto 'World Rally PS1'):
 - '<nombre> (USA)': .bin y .cue (XStation; tambien CD grabado, MiSTer);
 - '<nombre> (USA) PSIO': .bin y .cu2 (la hoja de pistas de PSIO, que no lee el .cue). El .bin es otra imagen: el EXE
   lleva ode_cfg = '1' (snd.c: el CD-DA entra por la entrada externa de la SPU, que es por donde lo da PSIO)."""
import subprocess, sys, os, struct, shutil, wave
import licencia
LIC = licencia.TXT
tracks = [l.strip() for l in open('../snd/tracks.txt') if l.strip()]
# pista final de silencio (2 s): la ultima pista de musica acaba como las demas, en el cambio de pista del subcodigo
# (INT4 de la pausa automatica), y no en el final de la imagen (lead-out), que cada ODE resuelve a su manera
FIN = '../snd/wav/fin.wav'
w = wave.open(FIN, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(bytes(150 * 2352)); w.close()
tracks.append(FIN[len('../snd/'):])
# al arrancar: tabla de pistas y menus (seguidos); de fondo: efectos, carrera y menus 2 (seguidos). Detras de cada
# recorrido, una copia de RACE.BIN: lo que falte de el en la carga se lee sin volver a buscar. Las copias se llaman
# ZRACEnn para que vayan al final del directorio: la BIOS solo lee el primer sector del directorio raiz y ahi tienen
# que estar SYSTEM.CNF y el ejecutable (si no, se queda en el logo)
files = ['SYSTEM.CNF', 'LOGO.BIN', 'TRACKS.BIN', 'MENU.BIN', 'SNDFX.BIN', 'RACE.BIN', 'MENU2.BIN', 'POST.BIN', 'POST2.BIN', 'WRPS1.EXE', 'TILES.BIN']
for k in range(12): files += ['LEVEL%02d.BIN' % k, 'ZRACE%02d.BIN=RACE.BIN' % k]
def build(reg, img=None, exe='WRPS1.EXE'):
    """imagen wrps1_<reg>.bin/.cue (img: otro nombre; exe: el ejecutable que va como WRPS1.EXE)"""
    img = img or 'wrps1_%s' % reg
    open('cd_lic_%s.dat' % reg, 'wb').write(lic[reg])    # sectores 0-11: licencia y logo (licencia.py)
    x = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<iso_project image_name="%s.bin" cue_sheet="%s.cue">' % (img, img),
         '  <track type="data">',
         '    <license file="cd_lic_%s.dat"/>' % reg,
         '    <identifiers system="PLAYSTATION" application="PLAYSTATION" volume="WRPS1" volume_set="WRPS1" publisher="ARKRETRO" data_preparer="ARKRETRO" copyright="ARKRETRO"/>',
         '    <directory_tree>']
    x += ['      <file name="%s" type="data" source="cd/%s"/>' % (f.split('=')[0], exe if f == 'WRPS1.EXE' else f.split('=')[-1])
          for f in files]
    x += ['      <dummy sectors="1024"/>', '    </directory_tree>', '  </track>']
    x += ['  <track type="audio" source="../snd/%s"/>' % t for t in tracks]
    x += ['</iso_project>']
    open('iso_%s.xml' % reg, 'w').write('\n'.join(x) + '\n')
    for e in ('.bin', '.cue'):              # mkpsxiso reescribe el fichero sin crear otro: la copia de sd/ es un enlace
        if os.path.exists(img + e): os.remove(img + e)
    subprocess.check_call([os.environ.get('MKPSXISO', 'mkpsxiso'), '-y', 'iso_%s.xml' % reg], stdout=subprocess.DEVNULL)
def check_dir(reg, img=None):
    """SYSTEM.CNF y el ejecutable en el primer sector del directorio raiz (lo unico que lee la BIOS)"""
    f = open('%s.bin' % (img or 'wrps1_%s' % reg), 'rb')
    def sec(n): f.seek(n * 2352 + 24); return f.read(2048)
    d = sec(le(sec(16), 158)); p = 0; nm = []
    while p < 2048 and d[p]: nm.append(d[p + 33:p + 33 + d[p + 32]].split(b';')[0].decode('latin1')); p += d[p]
    for n in ('SYSTEM.CNF', 'WRPS1.EXE'): assert n in nm, '%s: %s fuera del primer sector del directorio' % (reg, n)
def le(b, o): return struct.unpack_from('<I', b, o)[0]
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
def layout(reg, img=None):
    """pista -> (principio = indice 01, final = indice 00 de la siguiente), en sectores desde el principio del disco"""
    img = img or 'wrps1_%s' % reg
    idx = cue_idx(img)
    end = os.path.getsize('%s.bin' % img) // 2352
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
def msf(n, alt=False):
    """MM:SS:FF en decimal; alt: notacion de Systems Console (PSIO): un cuadro 0 se escribe SS-1:75"""
    m, s, f = n // 4500, n // 75 % 60, n % 75
    if alt and f == 0:
        f = 75
        if s: s -= 1
        else: m, s = m - 1, 59
    return '%02d:%02d:%02d' % (m, s, f)
assert (msf(7377, 1), msf(7500, 1), msf(4500, 1), msf(4500)) == ('01:38:27', '01:39:75', '00:59:75', '01:00:00')
CU2_REV = int(os.environ.get('CU2_REV', '2'))
def cu2(img):
    """hoja .cu2 de PSIO, igual byte a byte que la de Cue2cu2 / Systems Console. Revision 2 (firmware 2.6.11 o
    posterior): posiciones absolutas (indice + 150 sectores), pregapNN = indice 00 y trackNN = indice 01; size en
    sectores del .bin; CRLF y sin salto al final. Revision 1 (CU2_REV=1, firmware antiguo): sin pregap, size + 2 s y
    trk end = size + 4 s. Con pausas de 2 s exactas (las de mkpsxiso) las dos describen la misma disposicion"""
    idx = cue_idx(img); n = os.path.getsize('%s.bin' % img) // 2352
    assert sorted(idx) == list(range(1, len(idx) + 1)) and idx[1] == {1: 0}, 'pista 1 de datos al principio del .bin'
    r1 = CU2_REV == 1
    L = ['ntracks %d' % len(idx), 'size      ' + msf(n + 150 * r1), 'data1     00:02:00']
    for t in range(2, len(idx) + 1):
        if not r1: L.append('pregap%02d  %s' % (t, msf(idx[t].get(0, idx[t][1]) + 150, True)))
        L.append('track%02d   %s' % (t, msf(idx[t][1] + 150, True)))
    L += ['', 'trk end   ' + msf(n + (450 if r1 else 150), True)]
    return '\r\n'.join(L).encode('ascii')
SD = os.environ.get('SD_NOMBRE', 'World Rally PS1')
def sd_dir(n):
    assert len(n) <= 45 and n.isascii() and n.strip() == n and not set(n) & set('.\\/:*?"<>|'), \
        'SD_NOMBRE: hasta 45 caracteres ASCII en total, sin puntos ni \\ / : * ? " < > |: %r' % n
    d = os.path.join('sd', n)
    if os.path.isdir(d): shutil.rmtree(d)
    os.makedirs(d)
    return d
def put(src, dst):
    """la imagen a la carpeta de la tarjeta (enlace si se puede: son 48 MB)"""
    if os.path.exists(dst): os.remove(dst)
    try: os.link(src, dst)
    except OSError: shutil.copyfile(src, dst)
def sd(reg):
    """carpetas para la tarjeta: '<nombre> (<region>)' (XStation: .bin y .cue con la linea FILE a ese nombre, CRLF) y
    '<nombre> (<region>) PSIO' (.bin de la imagen para PSIO y su .cu2). Un solo .bin por carpeta (PSIO monta uno)"""
    n = '%s (%s)' % (SD, reg); d = sd_dir(n)
    put('wrps1_%s.bin' % reg, os.path.join(d, n + '.bin'))
    cue = open('wrps1_%s.cue' % reg).read()
    assert cue.count('"wrps1_%s.bin"' % reg) == 1, 'el .cue debe tener una sola linea FILE'
    cue = cue.replace('"wrps1_%s.bin"' % reg, '"%s.bin"' % n).replace('\r\n', '\n').replace('\n', '\r\n')
    open(os.path.join(d, n + '.cue'), 'wb').write(cue.encode('ascii'))
    n += ' PSIO'; d = sd_dir(n); img = 'wrps1_%s_psio' % reg
    put(img + '.bin', os.path.join(d, n + '.bin'))
    open(os.path.join(d, n + '.cu2'), 'wb').write(cu2(img))
def exe_psio():
    """el EXE con ode_cfg = '1' (snd.c)"""
    e = open('cd/WRPS1.EXE', 'rb').read()
    assert e.count(b'WRPS1-ODE:0') == 1, 'ode_cfg no esta (o esta dos veces) en WRPS1.EXE'
    open('cd/WRPS1_PSIO.EXE', 'wb').write(e.replace(b'WRPS1-ODE:0', b'WRPS1-ODE:1'))
LIC = [r for r in LIC if r in os.environ.get('REGIONES', ' '.join(LIC)).split()]
lic = {reg: licencia.datos(reg) for reg in LIC}
write_tracks({})
build(LIC[0])
tab = layout(LIC[0])
write_tracks(tab)
exe_psio()
for reg in LIC:
    build(reg)
    assert layout(reg) == tab, 'disposicion distinta en %s' % reg
    check_dir(reg)
    img = 'wrps1_%s_psio' % reg
    build(reg, img, 'WRPS1_PSIO.EXE')
    assert layout(reg, img) == tab, 'disposicion distinta en %s (PSIO)' % reg
    check_dir(reg, img)
    sd(reg)
    print(reg, 'ok')
print('pistas', ' '.join('%d:%d-%d' % (t, a, b) for t, (a, b) in sorted(tab.items()) if t >= 2))

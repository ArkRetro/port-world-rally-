"""sndwr2.py: sonido del World Rally 2 para la SPU (diseno 5; uso personal, sale de tus ROM): cada trozo de la tabla
de muestras del programa (0x1ACD0: 256 listas de trozos [tipo/banco.w][final.w][longitud.w]) se lee del banco de
sonido de gfx1 (banco 0 = 0x100000-0x1FFFFF, 1 = 0x300000-0x3FFFFF; los bytes estan al reves), se pasa a PS-ADPCM
(snd/psxadpcm: filtro 0 en el primer bloque, banderas 3 en el ultimo, relleno a 28 con la ultima muestra) y va a uno de
los ficheros de la SPU de work/cd/:
  SPUFIJO.BIN  bloque de silencio (0x1000) + los 33 efectos, el motor comun y la musica corta: se sube a 0x1000
  SPUTIT.BIN, SPUDEMO0-3.BIN, SPUMENU.BIN, SPUS7E.BIN, SPUCAR0-2.BIN: la zona variable, se suben a 0x5D5E0
Y work/snd_tab.c: por trozo, clave (banco << 16 | final) -> direccion en la SPU | fichero << 24, y bloques; ordenado por
clave (busqueda binaria). Las demos: 0 Portugal (0xA2), 1 Safari (0xA3), 2 Corcega (0xA4), 3 RAC (0xA5)."""
import os, sys, struct, subprocess, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work

TABLA = 0x1ACD0
SIL = 0x1000; FIJO0 = 0x1010; VAR0 = 0x5D5E0; VARN = 0x80000 - VAR0
FIJO = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x16, 0x17, 0x19, 0x1A, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x2B, 0x2E, 0x30, 0x39, 0x52, 0x53,
        0x54, 0x55, 0x5B, 0x5E, 0x60, 0x6C, 0x77, 0x78, 0x79, 0x7A, 0x80]
VARIABLES = [('SPUTIT', [0x70, 0x71, 0x68, 0x69, 0x6A, 0x6B, 0x6D]), ('SPUDEMO0', [0x72, 0x7D]), ('SPUDEMO1', [0x7B]),
             ('SPUDEMO2', [0x73, 0x74]), ('SPUDEMO3', [0x76, 0x75]), ('SPUMENU', [0x7C]), ('SPUS7E', [0x7E]),
             ('SPUCAR0', [0x27, 0x28, 0x29, 0x65, 0x66, 0x67]), ('SPUCAR1', [0x56, 0x57, 0x58, 0x61, 0x62, 0x6B, 0x6D]),
             ('SPUCAR2', [0x31, 0x32, 0x33, 0x5C, 0x5D, 0x68, 0x69, 0x6A])]
ENC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'snd', 'psxadpcm')

def be16(b, o): return struct.unpack_from('>H', b, o)[0]
def tabla(prog):
    """id -> [(banco, final, longitud)]"""
    r = {}
    for i in range(256):
        off = be16(prog, TABLA + 2 * i)
        if not off: continue
        p = TABLA + off; ch = []
        while be16(prog, p):
            t, fin, ln = be16(prog, p), be16(prog, p + 2), be16(prog, p + 4)
            ch.append((t & 3, fin, ln)); p += 6
        r[i] = ch
    return r

def codifica(gfx, banco, fin, ln):
    a = 0x100000 + banco * 0x200000 + (fin << 8) - 0x100000
    raw = gfx[a:a + ln][::-1]
    x = [(b - 128) * 256 for b in raw]
    x += [x[-1]] * (-len(x) % 28)
    with tempfile.TemporaryDirectory() as d:
        open(d + '/in', 'wb').write(struct.pack('<%dh' % len(x), *x))
        subprocess.check_call([ENC, d + '/in', d + '/out', '3'])
        return open(d + '/out', 'rb').read()

def main():
    prog = open(work('prog.bin'), 'rb').read(); gfx = open(work('gfx1.bin'), 'rb').read()
    T = tabla(prog); os.makedirs(work('cd'), exist_ok=True)
    if not os.path.exists(ENC): subprocess.check_call(['gcc', '-O2', '-o', ENC, ENC + '.c'])
    adpcm = {}
    def tr(i, k):
        if (i, k) not in adpcm: adpcm[(i, k)] = codifica(gfx, *T[i][k])
        return adpcm[(i, k)]
    ent = []                                   # (clave, direccion | fichero << 24, bloques)
    def fichero(nom, ids, base, nf):
        d = bytearray(); fallos = []
        for i in ids:
            for k in range(len(T[i])):
                a = tr(i, k); ent.append(((T[i][k][0] << 16) | T[i][k][1], base + len(d) | nf << 24, len(a) // 16)); d += a
        open(work('cd/%s.BIN' % nom), 'wb').write(d)
        return len(d)
    sil = bytes([0, 7]) + bytes(14)            # bloque de ceros en bucle sobre si mismo
    n = fichero('SPUFIJO', FIJO, FIJO0, 0)
    d = sil + open(work('cd/SPUFIJO.BIN'), 'rb').read(); open(work('cd/SPUFIJO.BIN'), 'wb').write(d)
    print('SPUFIJO.BIN %d B (fijo %d, diseno 378 320; acaba en 0x%05X)' % (len(d), n, FIJO0 + n))
    assert FIJO0 + n <= VAR0, 'el banco fijo no cabe'
    for nf, (nom, ids) in enumerate(VARIABLES, 1):
        n = fichero(nom, ids, VAR0, nf)
        print('%s.BIN %d B%s' % (nom, n, '' if n <= VARN else ' NO CABE (%d)' % VARN))
        assert n <= VARN
    ent.sort()
    # trozos que no van en ningun fichero (S09, S6F sin referencia): fuera
    todos = {(i, k) for i in T for k in range(len(T[i]))}
    fuera = sorted(set(i for i, k in todos) - set(FIJO) - set(x for _, l in VARIABLES for x in l))
    print('muestras fuera de los ficheros:', ' '.join('S%02X' % i for i in fuera))
    with open(work('snd_tab.c'), 'w') as f:
        f.write('/* generado por tools/sndwr2.py: trozos del GAE1 en la SPU (uso personal) */\n')
        f.write('#define SND_N %d\n' % len(ent))
        f.write('const unsigned snd_key[SND_N] = {%s};\n' % ', '.join('0x%x' % k for k, _, _ in ent))
        f.write('const unsigned snd_val[SND_N] = {%s};\n' % ', '.join('0x%x' % v for _, v, _ in ent))
        f.write('const unsigned short snd_nblk[SND_N] = {%s};\n' % ', '.join(str(b) for _, _, b in ent))
    print('snd_tab.c: %d trozos' % len(ent))

if __name__ == '__main__': main()

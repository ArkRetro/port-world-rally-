"""Sectores de licencia (0-11) de la imagen de CD. La BIOS comprueba el texto del sector 4 y, al arrancar, compara los
sectores 5-11 con el logo de PlayStation que lleva dentro (un modelo TMD de 0x3278 bytes): solo lo dibuja si son
iguales byte a byte (visto en la SCPH-101, v4.4; con otra cosa sale la pantalla del logo sin el logo). Esos datos son
de Sony y no van en el paquete: se toman de un fichero tuyo indicado en LICENCIA_USA / LICENCIA_EUR (o LICENCIA para
las dos):
 - tu BIOS (512 KB, p. ej. scph101.bin): se saca el logo de su shell (en las BIOS 4.x va comprimido: se descomprime
   como lo hace la propia BIOS) y va en los sectores 5-11 con el texto de la region en el 4;
 - un fichero de licencia de 28032 bytes (12 sectores de 2336: subcabecera + datos + EDC/ECC), p. ej. LICENSEA.DAT
   del SDK de Sony (LICENSEE.DAT para Europa), o
 - la imagen .bin (sectores de 2352 bytes) de un juego original de esa region: se copian sus sectores 0-11.
Sin fichero se genera solo el texto del sector 4 (arranca igual, pero sin el logo). Uso personal."""
import os, sys, struct

SEC = 2336
TXT = {'USA': 'Sony Computer Entertainment Amer  ica ', 'EUR': 'Sony Computer Entertainment Euro pe   '}


def _texto(reg):
    t = b'          Licensed  by          ' + TXT[reg].encode()
    sec = []
    for i in range(12):
        d = bytearray(2048)
        if i == 4: d[:len(t)] = t
        sec.append(bytes([0, 0, 8, 0, 0, 0, 8, 0]) + bytes(d) + bytes(SEC - 8 - 2048))
    return b''.join(sec)


def _unlz(src, p):
    """descompresor del shell de las BIOS 4.x (lo que hace la BIOS tras copiarlo a 0x80030000): tamano en 3 bytes y
    luego palabras de control big endian con 30 banderas (del bit 31 abajo): 0 un byte tal cual, 1 copia de lo ya
    descomprimido (2 bytes: los 2 bits bajos de la palabra de control reparten distancia y longitud)"""
    size = src[p] | src[p + 1] << 8 | src[p + 2] << 16; p += 4
    out = bytearray()
    while True:
        v = int.from_bytes(src[p:p + 4], 'big'); p += 4
        sh = v & 3
        for _ in range(30):
            a = src[p]; p += 1
            if v & 0x80000000:
                a = a << 8 | src[p]; p += 1
                o = len(out) - ((a & (0x3FFF >> sh)) + 1)
                for k in range((a >> (14 - sh)) + 3): out.append(out[o + k])
            else:
                out.append(a)
            if len(out) >= size: return bytes(out[:size])
            v = (v << 1) & 0xFFFFFFFF


def _tmd(b):
    """el logo en b: TMD de un objeto (0x41, banderas 0, 1 objeto) con vertices, normales y primitivas seguidos"""
    k = b.find(b'\x41\0\0\0\0\0\0\0\x01\0\0\0')
    while k >= 0:
        vt, nv, nt, nn, pt, npr = struct.unpack_from('<6I', b, k + 12)
        n = 12 + nt + nn * 8
        if pt == 28 and nv and nn and npr and vt + nv * 8 == nt and n <= 7 * 2048 and k + n <= len(b):
            return b[k:k + n]
        k = b.find(b'\x41\0\0\0\0\0\0\0\x01\0\0\0', k + 4)
    return None


def _logo_bios(rom):
    t = _tmd(rom)                            # shell sin comprimir (BIOS anteriores)
    if t: return t
    w0, w1 = struct.unpack_from('<II', rom, 0x18000)
    if w0 == 0x3C048003 and w1 >> 16 == 0x2484:   # lui a0, 0x8003; addiu a0, a0, n: datos comprimidos en 0x18000 + n
        return _tmd(_unlz(rom, 0x18000 + (w1 & 0xFFFF)))
    return None


def _de_fichero(fn, reg):
    d = open(fn, 'rb').read()
    if len(d) == 512 * 1024:
        t = _logo_bios(d)
        if not t: raise ValueError('%s: no se encuentra el logo en la BIOS' % fn)
        print('  %s: logo de la BIOS %s (%d bytes)' % (reg, fn, len(t)))
        s = bytearray(_texto(reg)); t += bytes(7 * 2048 - len(t))
        for k in range(7): s[(5 + k) * SEC + 8:(5 + k) * SEC + 8 + 2048] = t[k * 2048:(k + 1) * 2048]
        return bytes(s)
    if len(d) == 12 * SEC:
        return d
    if len(d) >= 16 * 2352 and d[:12] == b'\x00' + b'\xff' * 10 + b'\x00':
        return b''.join(d[k * 2352 + 16:(k + 1) * 2352] for k in range(12))
    raise ValueError('%s: ni BIOS de 512 KB, ni fichero de licencia de %d bytes, ni imagen .bin de 2352 bytes por sector'
                     % (fn, 12 * SEC))


def datos(reg):
    fn = os.environ.get('LICENCIA_' + reg) or os.environ.get('LICENCIA')
    if not fn:
        print('  %s: sin LICENCIA_%s -> solo el texto de licencia (la BIOS no mostrara el logo)' % (reg, reg))
        return _texto(reg)
    d = _de_fichero(fn, reg)
    s4 = d[4 * SEC + 8:4 * SEC + 8 + 2048]
    if b'Licensed' not in s4:
        raise ValueError('%s: el sector 4 no tiene el texto de licencia' % fn)
    if TXT[reg][:31].encode() not in s4:
        print('  AVISO %s: la licencia de %s no es de la region %s: %r' % (reg, fn, reg, s4.strip(b'\0 ')[:80]))
    if not any(any(d[k * SEC + 8:k * SEC + 8 + 2048]) for k in range(5, 12)):
        print('  AVISO %s: los sectores 5-11 de %s estan vacios: no habra logo' % (reg, fn))
    print('  %s: sectores de licencia de %s' % (reg, fn))
    return d


if __name__ == '__main__':
    for r in sys.argv[1:] or ['USA', 'EUR']:
        open('cd_lic_%s.dat' % r, 'wb').write(datos(r))

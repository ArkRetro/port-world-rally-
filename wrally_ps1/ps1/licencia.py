"""Sectores de licencia (0-11) de la imagen de CD. La BIOS dibuja el logo de PlayStation al arrancar con los datos
de los sectores 5-11 (y comprueba el texto del sector 4); esos datos son de Sony y no van en el paquete: se toman
de un fichero tuyo indicado en LICENCIA_USA / LICENCIA_EUR (o LICENCIA para las dos):
 - un fichero de licencia de 28032 bytes (12 sectores de 2336: subcabecera + datos + EDC/ECC), p. ej. LICENSEA.DAT
   del SDK de Sony (LICENSEE.DAT para Europa), o
 - la imagen .bin (sectores de 2352 bytes) de un juego original de esa region: se copian sus sectores 0-11.
Sin fichero se genera solo el texto del sector 4 (arranca igual, pero sin el logo). Uso personal."""
import os, sys

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


def _de_fichero(fn):
    d = open(fn, 'rb').read()
    if len(d) == 12 * SEC:
        return d
    if len(d) >= 16 * 2352 and d[:12] == b'\x00' + b'\xff' * 10 + b'\x00':
        return b''.join(d[k * 2352 + 16:(k + 1) * 2352] for k in range(12))
    raise ValueError('%s: ni fichero de licencia de %d bytes ni imagen .bin de 2352 bytes por sector' % (fn, 12 * SEC))


def datos(reg):
    fn = os.environ.get('LICENCIA_' + reg) or os.environ.get('LICENCIA')
    if not fn:
        print('  %s: sin LICENCIA_%s -> solo el texto de licencia (la BIOS no mostrara el logo)' % (reg, reg))
        return _texto(reg)
    d = _de_fichero(fn)
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

"""Busca las ROM de MAME de World Rally 1 y 2 por su CRC32 (como MAME: da igual el nombre del fichero, la carpeta y
si el romset es merged, split o non-merged) en los zip y carpetas que se le den, y las deja con el nombre que usan
los ports en dos carpetas. Lee zip (tambien zips dentro de zips) y carpetas; los 7z hay que descomprimirlos antes.
Uso: roms.py destino_wr1 destino_wr2 fuente [fuente ...]. Uso personal: solo se copian tus ROM."""
import io, os, sys, zipfile, zlib

# nombre que usa el port -> (CRC32, bytes). Los CRC son los de la lista de MAME
WR1 = {
    'worldr14.c01': (0xe931c2ee, 0x80000), 'worldr15.c03': (0x11f0fe2c, 0x80000),   # sonido (OKI)
    'worldr16.c22': (0x9e0d126c, 0x80000), 'worldr17.c23': (0x050f5629, 0x80000),   # programa (version 1.0, 0E56)
    'worldr18.i07': (0xb37c807e, 0x80000), 'worldr19.i09': (0x018b35bb, 0x80000),   # graficos
    'worldr20.i11': (0x58b2809a, 0x80000), 'worldr21.i13': (0xb7fddb12, 0x80000),
    'wrdallas.bin': (0x547d1768, 0x8000),                                           # MCU DS5002FP
}
WR2 = {
    'wr2_64.ic64': (0x4cdf4e1e, 0x80000), 'wr2_63.ic63': (0x94887c9f, 0x80000),     # programa
    'wrally2_ds5002fp_sram.bin': (0x4c532e9e, 0x8000),                              # MCU DS5002FP
}
# graficos y sonido del WR2: los tres de wrally2 o las 14 EPROM de wrally2a (vale cualquiera de los dos juegos)
WR2_GFX_A = {'wr2_ic68.ic68': (0x4a75ffaa, 0x100000), 'wr2_ic69.ic69': (0xa174d196, 0x400000),
             'wr2_ic70.ic70': (0x8d1e43ba, 0x200000)}
WR2_GFX_B = {'wr2.%s' % n: (c, 0x80000) for n, c in (
    ('01d', 0x753a138d), ('02d', 0x9c2a723c), ('09d', 0x372d70c8), ('10d', 0x5db67eb3), ('11d', 0xae66b97c),
    ('12d', 0x6dbdaa95), ('13d', 0xa4cd32f8), ('14d', 0x6eb781d5), ('16d', 0xad26086b), ('17d', 0xc1ec0745),
    ('18d', 0xe3617814), ('19d', 0x2dae988c), ('20d', 0x4f7ade84), ('21d', 0x899b0583))}
DONDE = {'worldr16.c22': 'wrally.zip o wrallyd.zip (segun la version de MAME)',
         'worldr17.c23': 'wrally.zip o wrallyd.zip (segun la version de MAME)'}


def recorre(fuentes):
    """(crc, bytes, funcion que lee el contenido, nombre para mostrar) de cada fichero de las fuentes"""
    def de_zip(z, ruta):
        for i in z.infolist():
            if i.is_dir(): continue
            nom = '%s:%s' % (ruta, i.filename)
            if i.filename.lower().endswith('.zip'):
                try: yield from de_zip(zipfile.ZipFile(io.BytesIO(z.read(i))), nom)
                except zipfile.BadZipFile: pass
            else: yield i.CRC, i.file_size, (lambda z=z, i=i: z.read(i)), nom
    def de_fich(p):
        if zipfile.is_zipfile(p):
            try: yield from de_zip(zipfile.ZipFile(p), p); return
            except zipfile.BadZipFile: pass
        n = os.path.getsize(p)
        if n <= 0x400000:
            d = open(p, 'rb').read()
            yield zlib.crc32(d), n, (lambda d=d: d), p
    for f in fuentes:
        if os.path.isdir(f):
            for dp, _, fs in os.walk(f):
                for x in sorted(fs): yield from de_fich(os.path.join(dp, x))
        elif os.path.isfile(f): yield from de_fich(f)
        else: print('AVISO: no existe %s' % f)


def main():
    if len(sys.argv) < 4: raise SystemExit(__doc__)
    d1, d2, fuentes = sys.argv[1], sys.argv[2], sys.argv[3:]
    quiero = {}
    for dest, tabla in ((d1, WR1), (d2, WR2), (d2, WR2_GFX_A), (d2, WR2_GFX_B)):
        for n, (c, s) in tabla.items(): quiero[(c, s)] = (dest, n)
    hecho = {}
    for c, s, leer, nom in recorre(fuentes):
        k = (c & 0xffffffff, s)
        if k in quiero and quiero[k] not in hecho:
            d = leer()
            if zlib.crc32(d) != k[0]: continue
            dest, n = quiero[k]; os.makedirs(dest, exist_ok=True)
            open(os.path.join(dest, n), 'wb').write(d); hecho[quiero[k]] = nom
    falta1 = [n for n in WR1 if (d1, n) not in hecho]
    falta2 = [n for n in WR2 if (d2, n) not in hecho]
    gfx = [t for t in (WR2_GFX_A, WR2_GFX_B) if all((d2, n) in hecho for n in t)]
    for (_, n), nom in sorted(hecho.items()): print('  %-26s <- %s' % (n, nom))
    err = []
    if falta1: err.append('World Rally 1: faltan %s' % ', '.join('%s (CRC %08x%s)' % (
        n, WR1[n][0], ', en ' + DONDE[n] if n in DONDE else '') for n in falta1))
    if falta2: err.append('World Rally 2: faltan %s' % ', '.join('%s (CRC %08x)' % (n, WR2[n][0]) for n in falta2))
    if not gfx: err.append('World Rally 2: faltan los graficos (wr2_ic68/69/70 de wrally2 o las 14 EPROM wr2.*d de '
                           'wrally2a)')
    if err: raise SystemExit('\n'.join(['ROM que faltan:'] + ['  ' + e for e in err] + [
        'Pon los zip de MAME de wrally (y wrallyd si tu romset es split) y de wrally2 (o wrally2a).']))
    print('ROM completas: World Rally 1 en %s, World Rally 2 en %s' % (d1, d2))


if __name__ == '__main__':
    main()

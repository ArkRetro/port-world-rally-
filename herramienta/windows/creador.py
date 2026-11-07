"""Creador de imagenes de World Rally 1 y 2 para PS1 en Windows (lo lanza 'Crear imagenes PS1.exe', creador.c; uso
personal) - arkretro. Uso: python creador.py carpeta_de_las_roms carpeta_de_salida [--regiones 'USA EUR']
1. Herramientas libres, la primera vez: el Python de este paquete con numpy, scipy y pillow, w64devkit (gcc y sh de
   busybox para Windows), el gcc de MIPS de PSn00bSDK, mkpsxiso y los .ld de nugget. Cada descarga con su SHA-256.
2. Todo se hace en C:\\WRPS1 (sin espacios ni acentos, que algunos programas no llevan bien; si no se puede crear, en
   C:\\Users\\Public\\WRPS1): las herramientas, una copia del port, una de las ROM y la construccion. Si se corta, al
   volver a lanzarlo sigue donde iba.
3. herramienta/crear_imagen.sh con el sh de w64devkit (las mismas cuentas que en Linux; las corridas del banco repiten
   las partidas grabadas, sin fork).
4. Las imagenes, a la carpeta de salida.
La salida (stdout) es el registro que muestra la ventana; las lineas '######## n/6' son los pasos de la barra."""
import hashlib, os, shutil, subprocess, sys, time, urllib.request, zipfile

APP = os.path.dirname(os.path.abspath(__file__))
# (nombre, url, sha256, MB): las versiones con que se probo
GH = 'https://github.com'
PY = 'https://files.pythonhosted.org/packages'
NUG = 'https://raw.githubusercontent.com/pcsx-redux/nugget/8a129f0498131ecefb27f5eb5eee30d016d0fe45'
DESCARGAS = [
    ('numpy', PY + '/12/33/fefe62073dc8acfd0f2b9ed7c003af2f50aa61555e113e6db02b8f79f145/numpy-2.5.4-cp312-cp312-win_amd64.whl',
     'a7b1b6353e36a7e50de2973a38d705c88ee93adcf120673cee7f45a4a3fa223a', 13),
    ('scipy', PY + '/39/e7/979fd14e75008623df31ba70d6bb144700f68feadcea042021c06a05bf82/scipy-1.18.1-cp312-cp312-win_amd64.whl',
     '5e4d44984abc0020154ea81b247adeddcc3ac5527b975ff798bd1ba0adc513c2', 37),
    ('pillow', PY + '/ff/cd/73a79ecdc822076002eb24b5e1e4a530b0bb5310fcb6d239abbec877110b/pillow-13.0.0-cp312-cp312-win_amd64.whl',
     'ff3fe9eb621fbb9db4c4be33ab946f6bac5f98ab50ca8d3260503653656c961b', 7),
    ('w64devkit', GH + '/skeeto/w64devkit/releases/download/v1.23.0/w64devkit-1.23.0.zip',
     '5c7dce6762be3e0dba648a9317790444c0e2f1ef3e677315c115727d7a549539', 77),
    ('mipsel', GH + '/Lameguy64/PSn00bSDK/releases/download/v0.24/gcc-mipsel-none-elf-12.3.0-windows.zip',
     '51df48145d5ef4b396d3932b72ccba538cc605c7c7745b32c58c277aa00523c5', 63),
    ('mkpsxiso', GH + '/Lameguy64/mkpsxiso/releases/download/v2.04/mkpsxiso-2.04-win64.zip',
     '65b910e61a276c1b3e30ab13e232f416199b5ffc31eaad3c647d0226bc132df5', 1),
    ('ps-exe.ld', NUG + '/ps-exe.ld', 'df608a9f10a1a4f37d2755e05f1d9fe65206ba8064b802b7f7c89c05e4178593', 0),
    ('nooverlay.ld', NUG + '/nooverlay.ld', '4192744a729e42063942cf6130b77dd0ff7eac4e71ebe1d867f9e8bd328897d4', 0),
]


def di(*a):
    print(*a, flush=True)


def base():
    """C:\\WRPS1 o C:\\Users\\Public\\WRPS1 (WRPS1_BASE para probar fuera de Windows)"""
    cands = [os.environ['WRPS1_BASE']] if os.environ.get('WRPS1_BASE') else []
    cands += [r'C:\WRPS1', os.path.join(os.environ.get('PUBLIC', r'C:\Users\Public'), 'WRPS1')]
    for c in cands:
        try:
            os.makedirs(c, exist_ok=True)
            p = os.path.join(c, '.prueba'); open(p, 'w').close(); os.remove(p)
            if c.isascii() and ' ' not in c: return c
        except OSError: pass
    raise SystemExit('ERROR: no se puede crear la carpeta de trabajo (C:\\WRPS1)')


def sha(fn):
    h = hashlib.sha256()
    with open(fn, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()


def descarga(nombre, url, suma, mb, dest):
    fn = os.path.join(dest, url.rsplit('/', 1)[1])
    if os.path.exists(fn) and sha(fn) == suma: return fn
    for intento in range(3):
        di('Descargando %s%s...' % (nombre, ' (%d MB)' % mb if mb else ''))
        try:
            with urllib.request.urlopen(url, timeout=60) as r, open(fn + '.tmp', 'wb') as f:
                shutil.copyfileobj(r, f, 1 << 20)
            if sha(fn + '.tmp') == suma:
                os.replace(fn + '.tmp', fn); return fn
            di('  la descarga de %s no es la esperada (suma distinta): otra vez' % nombre)
        except Exception as e:
            di('  fallo al descargar %s: %s' % (nombre, e))
        time.sleep(3)
    raise SystemExit('ERROR: no se pudo descargar %s (%s). Comprueba la conexion a internet y vuelve a probar.' % (nombre, url))


def herramientas(H):
    """instala lo que falte en H y devuelve el entorno para la construccion"""
    desc = os.path.join(H, 'descargas'); os.makedirs(desc, exist_ok=True)
    py = os.path.join(H, 'python')
    hecho = lambda n: os.path.exists(os.path.join(H, n + '.ok'))
    marca = lambda n: open(os.path.join(H, n + '.ok'), 'w').close()
    if not hecho('python'):
        di('Preparando Python...')
        shutil.rmtree(py, ignore_errors=True)
        shutil.copytree(os.path.dirname(sys.executable), py)
        for x in os.listdir(py):
            if x.endswith('._pth'): os.remove(os.path.join(py, x))     # asi sys.path lleva la carpeta del script
        shutil.copy(os.path.join(py, 'python.exe'), os.path.join(py, 'python3.exe'))
        marca('python')
    sp = os.path.join(py, 'Lib', 'site-packages')
    for n, url, s, mb in DESCARGAS:
        if hecho(n): continue
        fn = descarga(n, url, s, mb, desc)
        di('Instalando %s...' % n)
        if n in ('numpy', 'scipy', 'pillow'):
            os.makedirs(sp, exist_ok=True); zipfile.ZipFile(fn).extractall(sp)
        elif n == 'w64devkit':                       # la 1.23 en zip (las 2.x son autoextraibles de 7-Zip con ventana)
            shutil.rmtree(os.path.join(H, 'w64devkit'), ignore_errors=True); zipfile.ZipFile(fn).extractall(H)
        elif n == 'mipsel':
            shutil.rmtree(os.path.join(H, 'mipsel'), ignore_errors=True); zipfile.ZipFile(fn).extractall(os.path.join(H, 'mipsel'))
        elif n == 'mkpsxiso':
            zipfile.ZipFile(fn).extractall(H)
        else:
            os.makedirs(os.path.join(H, 'nugget'), exist_ok=True); shutil.copy(fn, os.path.join(H, 'nugget', n))
        marca(n)
    barra = lambda p: p.replace('\\', '/')
    env = dict(os.environ)
    env['PATH'] = os.pathsep.join([py, os.path.join(H, 'w64devkit', 'bin'), os.path.join(H, 'mipsel', 'bin'),
                                   os.path.join(H, 'mkpsxiso-2.04-win64', 'bin'), env.get('PATH', '')])
    env.update(MIPS='mipsel-none-elf', MIPS_ELF='elf32-littlemips', NUGGET=barra(os.path.join(H, 'nugget')),
               MKPSXISO=barra(os.path.join(H, 'mkpsxiso-2.04-win64', 'bin', 'mkpsxiso.exe')),
               PYTHONUTF8='1', PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1')
    return env


def copia_roms(org, dest):
    """los zip y las BIOS (ficheros de 512 KB) de la carpeta de las ROM (y sus subcarpetas) a una sin acentos"""
    shutil.rmtree(dest, ignore_errors=True); os.makedirs(dest)
    n = 0; siete = []
    for dp, _, fs in os.walk(org):
        for x in fs:
            p = os.path.join(dp, x); lx = x.lower()
            if lx.endswith('.7z'): siete.append(x); continue
            if lx.endswith('.zip') or (os.path.getsize(p) == 0x80000 and lx.endswith(('.bin', '.rom'))):
                n += 1; shutil.copy(p, os.path.join(dest, '%03d_%s' % (n, ''.join(c if c.isascii() and c.isalnum() or c in '._-' else '_' for c in x))))
    if siete: di('AVISO: los .7z no se leen (%s): descomprimelos y vuelve a probar si faltan ROM' % ', '.join(siete))
    if not n: raise SystemExit('ERROR: en %s no hay ningun .zip de MAME' % org)


def main():
    if len(sys.argv) < 3: raise SystemExit(__doc__)
    roms, salida = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    reg = sys.argv[sys.argv.index('--regiones') + 1] if '--regiones' in sys.argv else 'USA EUR'
    t0 = time.time()
    B = base(); H = os.path.join(B, 'herramientas')
    di('######## 0/6 herramientas (la primera vez se descargan unos 200 MB)')
    di('Carpeta de trabajo: %s' % B)
    env = herramientas(H)
    di('Copiando el port y tus ROM a %s...' % B)
    port = os.path.join(B, 'port')
    shutil.copytree(os.path.join(APP, 'port'), port, dirs_exist_ok=True)
    copia_roms(roms, os.path.join(B, 'roms'))
    sal = os.path.join(B, 'salida'); shutil.rmtree(sal, ignore_errors=True)
    barra = lambda p: p.replace('\\', '/')
    cmd = [os.path.join(H, 'w64devkit', 'bin', 'sh.exe'), 'herramienta/crear_imagen.sh', '--salida', barra(sal),
           '--trabajo', barra(port), '--regiones', reg, barra(os.path.join(B, 'roms'))]
    p = subprocess.Popen(cmd, cwd=port, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    with open(os.path.join(B, 'registro.txt'), 'wb') as reg_f:
        for l in iter(p.stdout.readline, b''):
            reg_f.write(l); reg_f.flush()
            sys.stdout.write(l.decode('utf-8', 'replace')); sys.stdout.flush()
    if p.wait():
        raise SystemExit('ERROR: la construccion fallo (codigo %d). El registro completo esta en %s'
                         % (p.returncode, os.path.join(B, 'registro.txt')))
    di('Copiando las imagenes a %s...' % salida)
    os.makedirs(salida, exist_ok=True)
    for x in sorted(os.listdir(sal)):
        shutil.rmtree(os.path.join(salida, x), ignore_errors=True)
        shutil.copytree(os.path.join(sal, x), os.path.join(salida, x))
    di('LISTO en %d min: las imagenes estan en %s' % ((time.time() - t0) / 60, salida))


if __name__ == '__main__':
    main()

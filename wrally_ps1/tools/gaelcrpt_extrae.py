"""Descifrado de la VRAM de Gaelco para el port, sacado del fuente de MAME en tu maquina (no va en el repositorio).

El repositorio no lleva el algoritmo ni sus tablas: este script LEE los ficheros de MAME (licencia BSD-3-Clause,
https://github.com/mamedev/mame, en el commit fijo de MAME_REV):
  src/mame/gaelco/gaelcrpt.cpp   la funcion decrypt() del dispositivo gaelco_vram_encryption_device
  src/mame/gaelco/gaelcrpt.h     set_params(): que miembro es cada parametro
  src/mame/gaelco/wrally.cpp     set_params(...) del World Rally (los dos parametros de la placa)
y traduce decrypt() de forma mecanica (sentencia a sentencia, sin saber nada del algoritmo) a:
  gaelcrpt_gen.h   C para el codigo recompilado: gc_dec(ep, dp, e) / gc_dec0(e) en linea, con cada bitswap<16> como dos
                   consultas a tabla (byte alto | byte bajo) y los switch seguidos sobre la misma variable fundidos en uno
  gaelcrpt_gen.c   las tablas de esos bitswap y gdecrypt(ep, dp, e), la traduccion literal (referencia del banco)
  gaelcrpt_gen.py  decrypt(ep, dp, e) en Python (tools/mapdec.py, ps1/gen2.py)
Despues comprueba el resultado: una huella (SHA-256) de la salida de las 65536 palabras como primera palabra y de
65536 pares (pseudoaleatorios fijos) ha de ser la del descifrado con que se probo el port. La huella no lleva nada del
algoritmo; si MAME cambia el fichero de forma que la traduccion no cuadra, se para con un mensaje.

Uso: python3 tools/gaelcrpt_extrae.py [--mame CARPETA] [--salida CARPETA] [--descargar] [--sin-comprobar]
  --mame     carpeta con gaelcrpt.cpp, gaelcrpt.h y wrally.cpp (por defecto $WR_MAME o work/mame)
  --salida   donde se escriben los generados (por defecto work/gaelcrpt; fuera de git)
  --descargar baja los tres ficheros de github.com/mamedev/mame (commit MAME_REV) a --mame si faltan, con su SHA-256
             (el creador de Windows y el Dockerfile de herramienta/ ya los bajan ellos)"""
import hashlib, os, re, sys, urllib.request

MAME_REV = 'f34f02505e32c1993c6a782b6814232cbfc74e36'      # mame0289
MAME_URL = 'https://raw.githubusercontent.com/mamedev/mame/%s/src/mame/gaelco/' % MAME_REV
FUENTES = {'gaelcrpt.cpp': 'bc1970463732e3e565c0f5c0c53638c6d2a014d495956eae43816b5272fff11a',
           'gaelcrpt.h': 'b8ac115c7c667390c5cde43ee3d5aa3c3722310e25691c80d9e2f6c285d2f931',
           'wrally.cpp': 'ac7ccd985c226c706e4a63e3319fd2e1cdf93ca2f2cc18ac09bae60147ebd883'}
# huella de la salida del descifrado con que se probo el port (ver huella())
HUELLA = '2356f371ed3287f63a7cf615a6cd687f67b7f1ae3a31e3a2ddcb18ede07c4037'
AQUI = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.normpath(os.path.join(AQUI, '..', 'work'))


def falla(msg):
    raise SystemExit('ERROR (gaelcrpt_extrae): ' + msg)


def sha(fn):
    return hashlib.sha256(open(fn, 'rb').read()).hexdigest()


def descargar(d):
    os.makedirs(d, exist_ok=True)
    for n, s in FUENTES.items():
        fn = os.path.join(d, n)
        if os.path.exists(fn) and sha(fn) == s: continue
        print('Descargando %s de MAME (%s)...' % (n, MAME_REV[:10]), flush=True)
        with urllib.request.urlopen(MAME_URL + n, timeout=60) as r: datos = r.read()
        if hashlib.sha256(datos).hexdigest() != s: falla('la descarga de %s no es la esperada (SHA-256 distinto)' % n)
        open(fn, 'wb').write(datos)


# ---------------------------------------------------------------- lectura del C++
def sin_comentarios(t):
    return re.sub(r'//[^\n]*|/\*.*?\*/', ' ', t, flags=re.S)


def cierre(t, i, a='(', b=')'):
    """indice del cierre del parentesis (o llave) que abre en t[i]"""
    n = 0
    for j in range(i, len(t)):
        if t[j] == a: n += 1
        elif t[j] == b:
            n -= 1
            if not n: return j
    falla('parentesis sin cerrar')


def trozos(t, sep=','):
    """t partido por sep a nivel 0 de parentesis"""
    r, n, ini = [], 0, 0
    for j, c in enumerate(t):
        if c in '([{': n += 1
        elif c in ')]}': n -= 1
        elif c == sep and not n: r.append(t[ini:j]); ini = j + 1
    r.append(t[ini:])
    return [x.strip() for x in r]


def lee_funcion(cpp):
    m = re.search(r'::\s*decrypt\s*\(', cpp)
    if not m: falla('no se encuentra ::decrypt( en gaelcrpt.cpp')
    p = cierre(cpp, m.end() - 1)
    params = [re.findall(r'\w+', x)[-1] for x in trozos(cpp[m.end():p])]
    if len(params) != 3: falla('decrypt() no tiene 3 parametros: %r' % params)
    a = cpp.index('{', p)
    return params, cpp[a + 1:cierre(cpp, a, '{', '}')]


def sentencias(t):
    """lista de ('asg', var, op, expr) | ('sw', var, [(valor, [sentencias])]) | ('ret', expr)"""
    r, i = [], 0
    while True:
        while i < len(t) and t[i].isspace(): i += 1
        if i >= len(t): return r
        m = re.match(r'switch\s*\(', t[i:])
        if m:
            p = cierre(t, i + m.end() - 1)
            var = t[i + m.end():p].strip()
            if not re.fullmatch(r'\w+', var): falla('switch sobre algo que no es una variable: %r' % var)
            a = t.index('{', p); b = cierre(t, a, '{', '}')
            casos = []
            for c in re.split(r'\bcase\b', t[a + 1:b])[1:]:
                v, cuerpo = c.split(':', 1)
                cuerpo = cuerpo.strip()
                if not cuerpo.endswith('break;'): falla('case sin break al final (switch %s)' % var)
                s = sentencias(cuerpo[:-len('break;')])
                if any(x[0] != 'asg' for x in s): falla('case con algo que no es una asignacion')
                casos.append((int(v, 0), s))
            if re.search(r'\bdefault\b', t[a + 1:b]): falla('switch con default (no previsto)')
            r.append(('sw', var, casos)); i = b + 1; continue
        j = i
        n = 0
        while j < len(t) and not (t[j] == ';' and n == 0):
            n += t[j] in '(' ; n -= t[j] in ')'; j += 1
        s = ' '.join(t[i:j].split()); i = j + 1
        m = re.fullmatch(r'return\s+(.+)', s)
        if m: r.append(('ret', m.group(1))); continue
        m = re.fullmatch(r'(?:int\s+(?:const\s+)?)?(\w+)\s*(=|\^=|\|=|&=|\+=)\s*(.+)', s)
        if not m: falla('sentencia no prevista en decrypt(): %r' % s)
        r.append(('asg',) + m.groups())


def lee_params(h, drv):
    m = re.search(r'set_params\s*\(([^)]*)\)\s*\{([^}]*)\}', h)
    if not m: falla('no se encuentra set_params() en gaelcrpt.h')
    nombres = [re.findall(r'\w+', x)[-1] for x in trozos(m.group(1))]
    miembro = {b: a for a, b in re.findall(r'(\w+)\s*=\s*(\w+)\s*;', m.group(2))}
    v = re.findall(r'set_params\s*\(([^)]*)\)', drv)
    if len(v) != 1: falla('wrally.cpp deberia tener un set_params(...) (hay %d)' % len(v))
    vals = [int(x, 0) for x in trozos(v[0])]
    if len(vals) != len(nombres): falla('set_params con %d valores' % len(vals))
    return {miembro[n]: x for n, x in zip(nombres, vals)}


# ---------------------------------------------------------------- expresiones
class Trad:
    """expresiones de decrypt(): identificadores conocidos, enteros, BIT(), bitswap<16>(), + << >> & | ^ y parentesis"""
    def __init__(self, params, miembros, locales):
        self.ren = dict(zip(params, ('ep', 'dp', 'e')))
        self.miembros = miembros; self.locales = set(locales); self.bits = []

    def bitswaps(self, x):
        """[(texto, argumento, bits)] de cada bitswap<16>(...) de x"""
        r = []
        for m in re.finditer(r'bitswap\s*<\s*16\s*>\s*\(', x):
            p = cierre(x, m.end() - 1)
            a = trozos(x[m.end():p])
            if len(a) != 17: falla('bitswap<16> con %d bits' % (len(a) - 1))
            bits = tuple(int(b, 0) for b in a[1:])
            if sorted(bits) != list(range(16)): falla('bitswap<16> que no es una permutacion')
            r.append((x[m.start():p + 1], a[0], bits))
        return r

    def conv(self, x, modo, tablas=None):
        """modo 'c' (bitswap en linea), 'ct' (bitswap con tabla: tablas[bits] = indice) o 'py'"""
        for txt, arg, bits in self.bitswaps(x):
            a = self.conv(arg, modo, tablas)
            if modo == 'ct': y = 'GC_BSW(%d, %s)' % (tablas[bits], a)
            else:
                f = 'GB' if modo == 'c' else '_B'
                y = '(' + ' | '.join('(%s(%s, %d) << %d)' % (f, a, b, 15 - i) for i, b in enumerate(bits)) + ')'
            x = x.replace(txt, '@%d@' % len(self.bits)); self.bits.append(y)
        def ident(m):
            w = m.group(0)
            if w in self.ren: return self.ren[w]
            if w in self.miembros: return 'GC_P%d' % (sorted(self.miembros).index(w) + 1)
            if w == 'BIT': return 'GB' if modo != 'py' else '_B'
            if w in self.locales: return w
            if re.fullmatch(r'0[xX][0-9a-fA-F]+|0|[1-9][0-9]*', w): return w
            falla('identificador o numero no previsto en decrypt(): %r' % w)
        x = re.sub(r'\w+', ident, x)
        if re.search(r'[^\w\s()@,+&|^]', x.replace('<<', '').replace('>>', '')):
            falla('operador no previsto en decrypt(): %r' % x)
        while '@' in x: x = re.sub(r'@(\d+)@', lambda m: self.bits[int(m.group(1))], x)
        x = ' '.join(x.split())
        if modo == 'ct':     # (GB(v, n + 1) << 1) | GB(v, n) -> ((v >> n) & 3): dos bits seguidos de una vez
            x = re.sub(r'\(GB\((\w+), ?(\d+)\) << 1\) \| GB\(\1, ?(\d+)\)',
                       lambda m: '((%s >> %s) & 3)' % (m.group(1), m.group(3)) if int(m.group(2)) == int(m.group(3)) + 1
                       else m.group(0), x)
        return x


def vars_de(st):
    s = []
    for x in st:
        if x[0] == 'asg' and x[1] not in s: s.append(x[1])
        if x[0] == 'sw':
            for _, c in x[2]:
                for v in vars_de(c):
                    if v not in s: s.append(v)
    return s


# ---------------------------------------------------------------- Python (tambien para las comprobaciones)
def a_python(st, tr, n=1):
    r = []
    for x in st:
        if x[0] == 'asg': r.append('    ' * n + '%s %s %s' % (x[1], x[2], tr.conv(x[3], 'py')))
        elif x[0] == 'ret': r.append('    ' * n + 'return (%s) & 0xFFFF' % tr.conv(x[1], 'py'))
        else:
            for k, (v, c) in enumerate(x[2]):
                r.append('    ' * n + '%s %s == %d:' % ('if' if not k else 'elif', x[1], v))
                r += a_python(c, tr, n + 1) or ['    ' * (n + 1) + 'pass']
    return r


def valores(expr, tr):
    """valores posibles de una expresion de un solo parametro (None si usa otra cosa)"""
    py = tr.conv(expr, 'py')
    usa = set(re.findall(r'\b(ep|dp|e)\b', py))
    if len(usa) != 1 or set(re.findall(r'\b[a-z_]\w*\b', py)) - usa - {'_B'}: return None
    f = eval('lambda %s: %s' % (usa.pop(), py), {'_B': lambda x, n: (x >> n) & 1})
    return {f(i) for i in range(65536)}


# ---------------------------------------------------------------- C optimizado
def optimiza(st, tr):
    """1) switch sobre v cuyos casos 0..n-1 son 'r = bitswap<16>(a, ...)' con el mismo r y a (y v en 0..n-1): una
    consulta a tabla con indice base + v; 2) switch seguidos sobre la misma variable (asignada una sola vez antes) con
    solo asignaciones a otras variables entre medias: uno, con esas asignaciones dentro de cada caso"""
    defs = {}
    for x in st:
        if x[0] == 'asg': defs[x[1]] = defs.get(x[1], []) + [x]
    r = []
    for x in st:
        if x[0] == 'sw':
            v, casos = x[1], x[2]
            uno = [c[0] if len(c) == 1 else None for _, c in casos]
            if all(u and u[2] == '=' for u in uno):
                bs = [tr.bitswaps(u[3]) for u in uno]
                if all(len(b) == 1 and b[0][0] == u[3].strip() for b, u in zip(bs, uno)) \
                        and len({b[0][1] for b in bs}) == 1 and len({u[1] for u in uno}) == 1 \
                        and [c for c, _ in casos] == list(range(len(casos))) and len(defs.get(v, [])) == 1 \
                        and (valores(defs[v][0][3], tr) or {-1}) <= set(range(len(casos))):
                    r.append(('tab', uno[0][1], v, bs[0][0][1], [b[0][2] for b in bs])); continue
        r.append(x)
    st, r = r, []
    i = 0
    while i < len(st):
        x = st[i]
        if x[0] == 'sw' and len(defs.get(x[1], [])) == 1:
            j = i + 1
            while j < len(st) and st[j][0] == 'asg' and st[j][1] != x[1]: j += 1
            if j < len(st) and st[j][0] == 'sw' and st[j][1] == x[1]:
                a, b = dict(x[2]), dict(st[j][2])
                if set(a) == set(b):
                    medio = st[i + 1:j]
                    st[j] = ('sw', x[1], [(v, a[v] + medio + b[v]) for v in sorted(a)])
                    i = j; continue
        r.append(x); i += 1
    return r, defs


def a_c(st, tr, modo, tablas, defs, n=1):
    r = []
    for x in st:
        if x[0] == 'asg': r.append('    ' * n + '%s %s %s;' % (x[1], x[2], tr.conv(x[3], modo, tablas)))
        elif x[0] == 'ret':
            y = tr.conv(x[1], modo, tablas)          # una consulta a las tablas ya es de 16 bits
            r.append('    ' * n + ('return %s;' % y if re.fullmatch(r'GC_BSW\(\d+, \w+\)', y) else 'return (%s) & 0xFFFF;' % y))
        elif x[0] == 'tab':
            _, dst, v, arg, _, base = x
            r.append('    ' * n + '%s = GC_BSW(%d + %s, %s);' % (dst, base, v, tr.conv(arg, modo, tablas)))
        else:
            vals = valores(defs[x[1]][0][3], tr) if modo == 'ct' and len(defs.get(x[1], [])) == 1 else None
            cubre = vals is not None and vals <= {v for v, _ in x[2]}
            r.append('    ' * n + 'switch (%s) {' % x[1])
            for k, (v, c) in enumerate(x[2]):
                r.append('    ' * n + ('default:' if cubre and k == len(x[2]) - 1 else 'case %d:' % v))
                r += a_c(c, tr, modo, tablas, defs, n + 1)
                r.append('    ' * (n + 1) + 'break;')
            r.append('    ' * n + '}')
    return r


# ---------------------------------------------------------------- comprobacion
def huella(dec):
    """SHA-256 de dec(0, 0, e) para las 65536 e y de dec(ep, dp, e) para 65536 ternas de un generador fijo, en u16 LE"""
    h = hashlib.sha256()
    h.update(b''.join(dec(0, 0, e).to_bytes(2, 'little') for e in range(65536)))
    x = 12345
    out = []
    for _ in range(65536):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF; ep = x >> 16
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF; dp = x >> 16
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF; e = x >> 16
        out.append(dec(ep, dp, e).to_bytes(2, 'little'))
    h.update(b''.join(out))
    return h.hexdigest()


def main():
    a = sys.argv[1:]
    def opc(n, d):
        return a[a.index(n) + 1] if n in a else d
    mame = opc('--mame', os.environ.get('WR_MAME') or os.path.join(WORK, 'mame'))
    sal = opc('--salida', os.path.join(WORK, 'gaelcrpt'))
    if '--descargar' in a: descargar(mame)
    falta = [n for n in FUENTES if not os.path.isfile(os.path.join(mame, n))]
    if falta:
        falla('faltan %s de MAME en %s. El descifrado de la VRAM de Gaelco no va en el repositorio: se saca del fuente '
              'de MAME (src/mame/gaelco/, commit %s). Bajalos con: python3 tools/gaelcrpt_extrae.py --descargar '
              '(o pon WR_MAME con la carpeta donde esten)' % (', '.join(falta), mame, MAME_REV))
    for n, s in FUENTES.items():
        if sha(os.path.join(mame, n)) != s:
            print('AVISO: %s no es el de MAME %s con que se probo (SHA-256 distinto): se traduce igual y se comprueba '
                  'el resultado' % (n, MAME_REV[:10]), flush=True)
    lee = lambda n: sin_comentarios(open(os.path.join(mame, n), encoding='utf-8', errors='replace').read())
    params, cuerpo = lee_funcion(lee('gaelcrpt.cpp'))
    miembros = lee_params(lee('gaelcrpt.h'), lee('wrally.cpp'))
    if len(miembros) != 2: falla('se esperaban 2 parametros de set_params')
    st = sentencias(cuerpo)
    if not st or st[-1][0] != 'ret' or any(x[0] == 'ret' for x in st[:-1]): falla('decrypt() sin un return al final')
    locales = vars_de(st)
    tr = Trad(params, miembros, locales)
    pv = [miembros[m] for m in sorted(miembros)]
    if not all(re.fullmatch(r'[A-Za-z_]\w*', v) for v in locales) or set(locales) & {'ep', 'dp', 'e', 'GB', 'GC_P1', 'GC_P2'}: falla('variables locales no previstas')
    cab = ('generado por wrally_ps1/tools/gaelcrpt_extrae.py desde src/mame/gaelco/gaelcrpt.cpp y wrally.cpp de MAME '
           '(github.com/mamedev/mame, commit %s; licencia BSD-3-Clause, copyright-holders: Manuel Abadia). '
           'No se distribuye ni se edita a mano' % MAME_REV)

    # Python
    py = ['"""%s"""' % cab, 'GC_P1, GC_P2 = 0x%x, 0x%x' % tuple(pv),
          'def _B(x, n): return (x >> n) & 1', 'def decrypt(ep, dp, e):']
    py += a_python(st, tr)
    py = '\n'.join(py) + '\n'
    espacio = {}
    exec(compile(py, 'gaelcrpt_gen.py', 'exec'), espacio)
    hh = huella(espacio['decrypt'])
    if hh != HUELLA and '--sin-comprobar' not in a:
        falla('el descifrado traducido de este gaelcrpt.cpp no da lo mismo que el probado con el port (huella %s). '
              'Usa los ficheros de MAME del commit %s' % (hh, MAME_REV))

    # C: tablas de los bitswap (byte alto, byte bajo) en el orden en que salen
    opt, defs = optimiza(st, tr)
    lista = []     # las de un switch convertido en consulta con indice, seguidas (base + v)
    def recoge(s):
        for k, x in enumerate(s):
            if x[0] == 'tab': s[k] = x + (len(lista),); lista.extend(x[4])
            elif x[0] == 'sw':
                for _, c in x[2]: recoge(c)
            else:
                for _, _, b in tr.bitswaps(x[-1]):
                    if b not in lista: lista.append(b)
    recoge(opt)
    tablas = {}
    for i, b in enumerate(lista): tablas.setdefault(b, i)
    def bsw(v, b): return sum(((v >> x) & 1) << (15 - i) for i, x in enumerate(b))
    h = ['/* %s */' % cab,
         '/* descifrado de la VRAM de Gaelco: gc_dec(ep, dp, e) = decrypt() de MAME (ep, dp: la palabra anterior del par,',
         '   cifrada y descifrada; e: la cifrada, de 16 bits); gc_dec0(e) = la primera de un par (ep = dp = 0) */',
         '#ifndef GAELCRPT_GEN_H', '#define GAELCRPT_GEN_H',
         '#define GC_P1 0x%x' % pv[0], '#define GC_P2 0x%x' % pv[1],
         '#define GB(x, n) (((x) >> (n)) & 1)',
         'extern const unsigned short gc_tab[%d][2][256];' % len(lista),
         '/* bitswap<16> de v (16 bits) con la tabla t: la permutacion de 16 bits es la OR de la de cada byte */',
         'static inline __attribute__((always_inline)) unsigned GC_BSW(unsigned t, unsigned v)',
         '{ const unsigned short *p = gc_tab[t][0]; return p[v >> 8] | p[256 + (v & 0xFF)]; }',
         'unsigned short gdecrypt(unsigned short ep, unsigned short dp, unsigned short e);',
         'static inline __attribute__((always_inline)) unsigned gc_dec(unsigned ep, unsigned dp, unsigned e)', '{',
         '    unsigned %s;' % ', '.join(locales)]
    h += a_c(opt, tr, 'ct', tablas, defs)
    h += ['}', 'static inline __attribute__((always_inline)) unsigned gc_dec0(unsigned e) { return gc_dec(0, 0, e); }',
          '#endif', '']
    c = ['/* %s */' % cab, '#include "gaelcrpt_gen.h"', 'const unsigned short gc_tab[%d][2][256] = {' % len(lista)]
    for b in lista:
        c.append('{{' + ','.join('0x%x' % bsw(v << 8, b) for v in range(256)) + '},')
        c.append(' {' + ','.join('0x%x' % bsw(v, b) for v in range(256)) + '}},')
    c += ['};', '/* traduccion literal de decrypt() (sin tablas): la referencia del banco de pruebas */',
          'unsigned short gdecrypt(unsigned short ep_, unsigned short dp_, unsigned short e_)', '{',
          '    unsigned ep = ep_, dp = dp_, e = e_, %s;' % ', '.join(locales)]
    c += a_c(st, tr, 'c', None, {})
    c += ['}', '']
    os.makedirs(sal, exist_ok=True)
    for n, t in (('gaelcrpt_gen.h', '\n'.join(h)), ('gaelcrpt_gen.c', '\n'.join(c)), ('gaelcrpt_gen.py', py)):
        open(os.path.join(sal, n + '.tmp'), 'w', newline='\n').write(t)
        os.replace(os.path.join(sal, n + '.tmp'), os.path.join(sal, n))
    print('gaelcrpt_extrae: descifrado de la VRAM sacado de MAME %s -> %s (huella %s)'
          % (MAME_REV[:10], sal, 'comprobada' if hh == HUELLA else 'SIN comprobar'), flush=True)


if __name__ == '__main__':
    main()

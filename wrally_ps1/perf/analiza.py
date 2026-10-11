"""Reparte el perfil de psxprof (ciclos por PC) entre funciones C y, dentro de rc_run, entre instrucciones y
rutinas del 68000 (via la tabla de lineas DWARF y los comentarios de wr_rc.c).
uso: analiza.py dir_perfil wr2g.nm lines.txt wr_rc.c [main.c]"""
import sys, re, struct, collections, bisect
d, nmf, linesf, wrc = sys.argv[1:5]
prof = struct.unpack('<%dQ' % 0x80000, open(d + '/prof.bin', 'rb').read())
profn = struct.unpack('<%dQ' % 0x80000, open(d + '/profn.bin', 'rb').read())
total = sum(prof)
syms = []
for l in open(nmf):
    p = l.split()
    if len(p) == 3 and p[1] in 'tTwW': syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [a for a, _ in syms]
def sym(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return syms[i][1] if i >= 0 else '?'
by = collections.Counter(); byn = collections.Counter()
for i, c in enumerate(prof):
    if c:
        s = sym(0x80000000 + i * 4); by[s] += c; byn[s] += profn[i]
print('ciclos totales %d (%.2f s, %.1f cuadros de video)' % (total, total / 33868800, total / 566000))
print('%-28s %8s %6s %10s %5s' % ('funcion', 'Mciclos', '%', 'instr', 'CPI'))
for s, c in by.most_common(30):
    print('%-28s %8.1f %6.2f %10d %5.2f' % (s, c / 1e6, 100 * c / total, byn[s], c / max(byn[s], 1)))
# lineas
line_of = {}
cur = None
for l in open(linesf):
    m = re.match(r'^(\S+)\s+(\d+)\s+(0x[0-9a-f]+)', l)
    if m:
        f, ln, a = m.group(1), int(m.group(2)), int(m.group(3), 16)
        line_of[a] = (f, ln)
la = sorted(line_of)
def line(pc):
    i = bisect.bisect_right(la, pc) - 1
    return line_of[la[i]] if i >= 0 else ('?', 0)
# mapa linea de wr_rc.c -> direccion 68k
wl = open(wrc).read().split('\n')
l68 = [0] * (len(wl) + 2)
cur68 = 0
for k, t in enumerate(wl, 1):
    m = re.search(r'CK\(0x([0-9a-f]+),', t)
    if m: cur68 = int(m.group(1), 16)
    l68[k] = cur68
by68 = collections.Counter(); bym = collections.Counter(); byline = collections.Counter()
for i, c in enumerate(prof):
    if not c: continue
    pc = 0x80000000 + i * 4
    f, ln = line(pc)
    if f.startswith('wr_rc'):
        by68[l68[ln] if ln < len(l68) else 0] += c
    elif f.startswith('main'):
        bym[ln] += c
    byline[(f, ln)] += c
# rutinas 68k: agrupar por el destino de llamada mas cercano por debajo
calls = set()
for t in wl:
    m = re.search(r"/\* [0-9a-f]+ (bsr|jsr)\S* \$([0-9a-f]+) \*/", t)
    if m: calls.add(int(m.group(2), 16))
calls = sorted(calls)
def rout(a):
    i = bisect.bisect_right(calls, a) - 1
    return calls[i] if i >= 0 else 0
byr = collections.Counter()
for a, c in by68.items(): byr[rout(a)] += c
rc_total = sum(by68.values())
print('\ncodigo 68k recompilado: %.1f Mciclos (%.1f%%)' % (rc_total / 1e6, 100 * rc_total / total))
print('rutinas 68k (por entrada de bsr/jsr mas cercana):')
for r, c in byr.most_common(25): print('  %06x %8.2f Mc %5.1f%%' % (r, c / 1e6, 100 * c / total))
print('\ninstrucciones 68k mas caras:')
ins = {}
for t in wl:
    m = re.search(r'CK\(0x([0-9a-f]+),.*/\* ([0-9a-f]+ .*) \*/', t)
    if m: ins[int(m.group(1), 16)] = m.group(2)
for a, c in by68.most_common(40): print('  %06x %7.2f Mc %5.2f%%  %s' % (a, c / 1e6, 100 * c / total, ins.get(a, '')))
print('\nlineas de main.c mas caras:')
ml = open(sys.argv[5]).read().split('\n') if len(sys.argv) > 5 else []
for ln, c in bym.most_common(25): print('  %5d %7.2f Mc %5.2f%%  %s' % (ln, c / 1e6, 100 * c / total, ml[ln - 1].strip()[:90] if ln - 1 < len(ml) else ''))

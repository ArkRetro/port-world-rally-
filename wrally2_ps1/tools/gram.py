"""gram.py [--r1 | --codigo] w2ps1.map: puerta G-RAM (diseno 2.1, 2.5 y 11.2) con el mapa del enlazado.
R0 (el EXE de H2, ROM contigua): pool = 0x80200000 - pila (8 KB) - fin de la imagen - lo que falta del codigo hasta
sus presupuestos (rc_run 393 216 B y C del port 81 920 B); puerta: pool >= 160 KB.
--codigo: imprime RES_CODIGO (lo que falta hasta los presupuestos de R1: rc_run 417 792 B y C 81 920 B) para el
enlazado del EXE de tamanos R1 (reservas.c con -DR1).
--r1 (ese EXE, sin la ROM contigua y con la ROM residente, rc_romtab y RES_CODIGO): area de tramo = 0x80200000 - pila -
fin de la imagen; puerta: area >= 960 KB. Sale con error si la puerta no pasa (GRAM=0: solo avisa)."""
import sys, os, re

FIN, PILA = 0x80200000, 8192          # PILA_RAM de ps1/carga.h (el juego va con la pila en el scratchpad)
PRESU = {'rc_run R0': 393216, 'rc_run R1': 417792, 'C': 81920}
GENERADO = {'wr2_rc.o': 'rc_run', 'mcu_rc.o': 'MCU traducida', 'data.o': 'datos', 'data_r1.o': 'datos',
            'reservas.o': 'reservas', 'reservas_r1.o': 'reservas'}

def secciones(mapa):
    """(seccion de salida, entrada, direccion, tamano, fichero) de las secciones cargadas en la RAM"""
    sal = None; pend = None; r = []
    for l in open(mapa):
        l = l.rstrip('\n')
        m = re.match(r'^(\.\S+)(\s+0x([0-9a-f]+)\s+0x([0-9a-f]+))?\s*$', l)
        if m: sal = m.group(1); pend = None; continue
        m = re.match(r'^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$', l)
        if m: nom, a, n, f = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        else:
            m = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$', l)
            if m and pend: nom, a, n, f = pend, int(m.group(1), 16), int(m.group(2), 16), m.group(3)
            else:
                m = re.match(r'^ (\S+)$', l); pend = m.group(1) if m and not l.strip().startswith('*') else None
                continue
        pend = None
        if a >= 0x80000000 and n and sal in ('.text', '.rodata', '.data', '.sbss', '.bss'):
            if nom == '*fill*': f = 'relleno'
            r.append((sal, nom, a, n, os.path.basename(f.split('(')[0]) if '.a(' not in f else 'libgcc'))
    return r

def cuentas(mapa):
    s = secciones(mapa)
    fin = max(a + n for _, _, a, n, _ in s)
    g = {}
    for sal, nom, a, n, f in s:
        clase = GENERADO.get(f, 'C' if f != 'relleno' else 'relleno')
        if clase == 'rc_run' and sal == '.rodata': clase = 'ht'
        if clase == 'rc_run' and sal in ('.sbss', '.bss'): clase = 'ROM residente (rc_romres)'   # real desde H3; en R0 esta repetida
        if clase == 'C' and sal in ('.sbss', '.bss'): clase = 'BSS ' + f
        if clase == 'reservas' and sal in ('.sbss', '.bss'): clase = 'reservas (buferes por escribir)'
        g[clase] = g.get(clase, 0) + n
    return fin, g

def tabla(g, fin):
    for k in sorted(g, key=lambda k: -g[k]): print('  %-34s %9d' % (k, g[k]))
    print('  %-34s %9d  (0x%08X)' % ('fin de la imagen', fin - 0x80010000, fin))

def falta(g, r):
    return max(0, PRESU['rc_run ' + r] - g.get('rc_run', 0)) + max(0, PRESU['C'] - g.get('C', 0))

def puerta(ok, txt):
    print('PUERTA G-RAM %s: %s' % (txt, 'pasa' if ok else 'NO PASA'))
    if not ok and os.environ.get('GRAM', '1') != '0': sys.exit(1)

if __name__ == '__main__':
    modo = sys.argv[1] if sys.argv[1].startswith('--') else ''
    fin, g = cuentas(sys.argv[-1])
    if modo == '--codigo': print(falta(g, 'R1')); sys.exit(0)
    if modo == '--r1':
        print('EXE de tamanos R1 (%s):' % sys.argv[-1]); tabla(g, fin)
        area = FIN - PILA - fin
        print('  area de tramo: %d B (%.1f KB)' % (area, area / 1024))
        puerta(area >= 960 * 1024, 'R1 area >= 960 KB')
    else:
        print('EXE R0 (%s):' % sys.argv[-1]); tabla(g, fin)
        f = falta(g, 'R0'); pool = FIN - PILA - fin - f + g.get('ROM residente (rc_romres)', 0)   # en R0 la ROM entera ya esta en el EXE
        print('  codigo que falta hasta los presupuestos: %d B (rc_run %d, C %d)' % (f, PRESU['rc_run R0'], PRESU['C']))
        print('  pool: %d B (%.1f KB)' % (pool, pool / 1024))
        puerta(pool >= 160 * 1024, 'R0 pool >= 160 KB')

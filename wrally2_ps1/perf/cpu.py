"""cpu.py carpeta [desde]: puerta G-CPU con el cpu.csv de psxprof2 (por llamada a rc_vblank: ciclos de la CPU fuera de
rc_vblank desde la llamada anterior = 68000 recompilado + MCU por acceso; ciclos de la llamada anterior entera y de
mcu_cuadro dentro; modo FEC298). Fraccion del cuadro de video (263 lineas x 2 152 ciclos = 566 000): 68000 + MCU =
cyc_68k + cyc_mcu_ant; cuadro entero = cyc_68k + cyc_vblank_ant (con el dibujo del mosaico y la espera del vsync: 100 % = va a 60 Hz; mas = cuadros saltados). Mediana y p99 por modo
(1 carrera, 5 demo, los demas menus) desde la llamada 'desde' (por defecto 200: fuera del arranque).
Puerta (H2): carrera, 68000 + MCU mediana <= 30 % y p99 <= 45 %."""
import csv, sys, os

VB = 566000.0
d = sys.argv[1]; desde = int(sys.argv[2]) if len(sys.argv) > 2 else 200
rows = [r for r in csv.DictReader(open(os.path.join(d, 'cpu.csv'))) if int(r['llamada']) >= desde]
def p(v, q): v = sorted(v); return v[min(len(v) - 1, int(q * len(v)))] if v else 0
modos = {}
for r in rows: modos.setdefault(int(r['modo']), []).append(r)
nombre = {1: 'carrera', 5: 'demo', 0: 'negro/transicion'}
print('%-22s %7s %22s %22s' % ('modo', 'cuadros', '68000+MCU med/p99/max', 'cuadro entero med/p99'))
res = None
for m in sorted(modos):
    rs = modos[m]
    a = [(int(r['cyc_68k']) + int(r['cyc_mcu_ant'])) / VB * 100 for r in rs]
    b = [(int(r['cyc_68k']) + int(r['cyc_vblank_ant'])) / VB * 100 for r in rs]
    print('%-22s %7d %6.1f %6.1f %6.1f  %7.1f %6.1f %%' % ('%X %s' % (m, nombre.get(m, 'menu')), len(rs), p(a, 0.5), p(a, 0.99), max(a), p(b, 0.5), p(b, 0.99)))
    if m == 1: res = (p(a, 0.5), p(a, 0.99))
if res:
    ok = res[0] <= 30 and res[1] <= 45
    print('PUERTA G-CPU (carrera: mediana <= 30 %%, p99 <= 45 %%): %.1f %% / %.1f %% -> %s' % (res[0], res[1], 'pasa' if ok else 'NO PASA'))
    sys.exit(0 if ok else 1)
print('sin cuadros de carrera')

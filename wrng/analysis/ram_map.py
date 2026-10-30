#!/usr/bin/env python3
"""Mapa de uso de la RAM compartida FEC000-FEFFFF por el 68k (lecturas/escrituras dinámicas por fase + referencias estáticas)."""
import csv
ramr = {}; ramw = {}
# solo fases 2 (atracción) y 4 (partida): la fase 1 incluye el test de RAM que toca los 16 KB
for l in open('cov_ramr.txt'):
    p = l.split()
    if int(p[1]) & 6: ramr[int(p[0], 16)] = int(p[1])
for l in open('cov_ramw.txt'):
    p = l.split()
    if int(p[1]) & 6: ramw[int(p[0], 16)] = int(p[1])
BASE, END = 0xFEC000, 0xFF0000
# clasificar por palabra: R, W, RW, -
def cls(a):
    r = a in ramr or (a + 1) in ramr; w = a in ramw or (a + 1) in ramw
    return ('R' if r else '') + ('W' if w else '') or '-'
ranges = []; a = BASE
while a < END:
    c = cls(a); b = a
    while b < END and cls(b) == c: b += 2
    ranges.append((a, b, c)); a = b
# fusionar huecos pequeños (<16 bytes) entre rangos tocados
m = []
for s, e, c in ranges:
    if m and c == '-' and e - s < 16 and m[-1][2] != '-':
        m[-1] = (m[-1][0], e, m[-1][2]); continue
    if m and m[-1][2] == c: m[-1] = (m[-1][0], e, c); continue
    if m and c != '-' and m[-1][2] != '-':  # unir tocados contiguos aunque difiera R/W
        m[-1] = (m[-1][0], e, 'RW'); continue
    m.append((s, e, c))
# referencias estáticas por dirección (absrefs.csv)
refs = {}
for r in csv.DictReader(open('absrefs.csv')):
    if r['region'] == 'RAM':
        refs.setdefault(int(r['address'], 16), []).append(r)
with open('ram_ranges.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['start', 'end', 'len', 'dyn', 'n_static_refs', 'first_static_pcs'])
    for s, e, c in m:
        rs = [x for a in range(s, e) for x in refs.get(a, [])]
        pcs = sorted({x['pc'] for x in rs})
        w.writerow(['%06X' % s, '%06X' % e, e - s, c, len(rs), ' '.join(pcs[:6])])
tr = sum(e - s for s, e, c in m if c != '-')
print('RAM tocada dinámicamente: %d bytes de 16384; rangos=%d' % (tr, len(m)))
for s, e, c in m: print('%06X-%06X %5d %s' % (s, e, e - s, c))
# pila: menor dirección escrita por encima de FEFF00
lo = min(a for a in ramw if a >= 0xFEFE80) if any(a >= 0xFEFE80 for a in ramw) else None
print('pila: dirección mínima escrita >= FEFE80: %06X (SSP inicial FEFF80 -> %d bytes usados)' % (lo, 0xFEFF80 - lo))
# direcciones de RAM referenciadas estáticamente pero no tocadas dinámicamente
st = set(refs); dyn = set(ramr) | set(ramw)
print('direcciones RAM con ref. estática: %d; de ellas no tocadas en la ejecución: %d' % (len(st), len([a for a in st if a not in dyn and (a ^ 1) not in dyn])))

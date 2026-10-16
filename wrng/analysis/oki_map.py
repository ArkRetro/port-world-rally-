#!/usr/bin/env python3
"""Mapa de comandos del driver OKI (0x14000-0x14444) -> oki_map.csv
comando 00-7F (y FE/FF stop): entrada directa de 4 bytes en 14C02/14C9A/14D32 (según fec152&3 = 2 / 0 / otro): [máscara canales, prioridad, volumen, frase]
comando 80-B2: puntero en 14524/145F0/146BC -> script: [máscara, prioridad, volumen] + bytes:
   <F0 frase (índice en tabla 144AC -> (banco 70000D, frase OKI)); si modo temporizado (F6) le siguen 2 bytes de duración (frames)
   F0 xx: ignora byte; F1 pp: prioridad; F2 vv: volumen; F3 nn dd dd: repetir nn veces saltando dd (rel.); F4 dd dd: salto;
   F5 xx tt tt: espera tt frames; F6: modo temporizado ON; F7: OFF; F8-FE: nop; FF: fin (libera canal)"""
import struct, csv
LAB = '/tmp/claude-0/-home-user-port-world-rally-/0e54570e-879c-581d-b19c-42bb81ddf7e3/scratchpad/lab'
rom = open(LAB + '/wr_cpu.bin', 'rb').read()
def u32(a): return struct.unpack('>I', rom[a:a + 4])[0]
PHR = 0x144AC; NPHR = (0x14524 - PHR) // 2
phrases = [(rom[PHR + 2 * i], rom[PHR + 2 * i + 1]) for i in range(NPHR)]
SETS = {'set2(fec152&3==2)': (0x14524, 0x14C02), 'set0(fec152&3==0)': (0x145F0, 0x14C9A), 'set1/3(otro)': (0x146BC, 0x14D32)}
def ph(b): return 'ph%02X(b%d,f%02X)' % (b, phrases[b][0], phrases[b][1]) if b < NPHR else 'ph%02X?' % b
def parse_script(p):
    out = []; timed = False; n = 0
    while n < 60:
        b = rom[p]; p += 1; n += 1
        if b < 0xF0:
            if timed: out.append(ph(b) + '/t%d' % ((rom[p] << 8) | rom[p + 1])); p += 2
            else: out.append(ph(b))
        elif b == 0xF0: out.append('F0:skip%02X' % rom[p]); p += 1
        elif b == 0xF1: out.append('F1:prio=%02X' % rom[p]); p += 1
        elif b == 0xF2: out.append('F2:vol=%X' % rom[p]); p += 1
        elif b == 0xF3:
            cnt = rom[p]; d = struct.unpack('>h', rom[p + 1:p + 3])[0]; out.append('F3:rep%d->%+d' % (cnt, d)); p += 3
            if cnt == 0: out.append('(bucle infinito)'); break
        elif b == 0xF4:
            d = struct.unpack('>h', rom[p:p + 2])[0]; out.append('F4:jmp%+d' % d); p += 2
            if d < 0: out.append('(bucle)'); break
            p += d
        elif b == 0xF5: out.append('F5:wait%d' % ((rom[p + 1] << 8) | rom[p + 2])); p += 3
        elif b == 0xF6: out.append('F6:timed'); timed = True
        elif b == 0xF7: out.append('F7:untimed'); timed = False
        elif b == 0xFF: out.append('FF:fin'); break
        else: out.append('%02X:nop' % b)
    return ' '.join(out)
rows = []
for name, (hi_tab, lo_tab) in SETS.items():
    for c in range(0, 38):
        p = lo_tab + 4 * c
        mask, prio, vol, f = rom[p], rom[p + 1], rom[p + 2], rom[p + 3]
        rows.append((name, '%02X' % c, '%06X' % p, '%02X' % mask, '%02X' % prio, '%X' % (vol & 0xF), ph(f)))
    for c in range(0, 51):
        p = u32(hi_tab + 4 * c)
        mask, prio, vol = rom[p], rom[p + 1], rom[p + 2]
        if mask == 0: rows.append((name, '%02X' % (0x80 + c), '%06X' % p, '00', '', '', '(vacío)')); continue
        rows.append((name, '%02X' % (0x80 + c), '%06X' % p, '%02X' % mask, '%02X' % prio, '%X' % (vol & 0xF), parse_script(p + 3)))
with open('oki_map.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['conjunto', 'comando', 'entrada', 'mascara_canales', 'prioridad', 'volumen', 'frases: phNN(bBANCO,fFRASE_OKI)[/tFRAMES] y opcodes'])
    for r in rows: w.writerow(r)
print('frases=%d' % NPHR)
print('tabla frases 144AC:', ' '.join('%02X:(b%d,f%02X)' % (i, b, p) for i, (b, p) in enumerate(phrases)))
for r in rows:
    if r[0].startswith('set2'): print(' '.join(r[1:]))

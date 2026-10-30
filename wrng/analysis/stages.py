#!/usr/bin/env python3
"""Decodifica la tabla de etapas (0xDFBBC, 16 bytes/etapa; campos cifrados que el juego descifra escribiéndolos en VRAM)
y lista todos los sitios 'move.l $ROM.l,$VRAM.l' (oráculo de descifrado) con su valor descifrado."""
import struct, csv, sys
sys.path.insert(0, '.')
from gaelcrypt import decrypt_long
LAB = '/tmp/claude-0/-home-user-port-world-rally-/0e54570e-879c-581d-b19c-42bb81ddf7e3/scratchpad/lab'
rom = open(LAB + '/wr_cpu.bin', 'rb').read()
def u32(a): return struct.unpack('>I', rom[a:a + 4])[0]
def u16(a): return struct.unpack('>H', rom[a:a + 2])[0]

print('== Tabla de etapas 0xDFBBC (16 bytes/etapa) ==')
print('etapa  w0   w1   ptrBloque(desc)  -> [fec188 fec18c fec190 fec194] (desc)   +8(desc)=feea8c/90   +C(desc)->fec1a0/feea94')
blocks = []
for n in range(0, 40):
    a = 0xDFBBC + n * 16
    w0, w1 = u16(a), u16(a + 2)
    p = decrypt_long(u32(a + 4))
    v8 = decrypt_long(u32(a + 8)); vc = decrypt_long(u32(a + 12))
    if p < 0x100000 and (p & 1) == 0 and p >= 0x15000:
        q = [decrypt_long(u32(p + i * 4)) for i in range(4)]
        blocks.append((n + 1, p, q, v8, vc))
        print('%2d  %04X %04X  %06X -> [%06X %06X %06X %06X]  %08X  %08X' % (n + 1, w0, w1, p, q[0], q[1], q[2], q[3], v8, vc))
    else:
        print('%2d  %04X %04X  (%08X no es puntero ROM: fin de tabla?)' % (n + 1, w0, w1, p))
        break

# tamaños por diferencia de punteros ordenados
ptrs = sorted(set([p for _, p, q, _, _ in blocks] + [x for _, _, q, _, _ in blocks for x in q if 0 < x < 0x100000]))
print('\n== Punteros de datos de etapa ordenados (tamaño = hasta el siguiente puntero) ==')
for i, p in enumerate(ptrs):
    nxt = ptrs[i + 1] if i + 1 < len(ptrs) else None
    print('%06X  %s' % (p, ('%d bytes' % (nxt - p)) if nxt else '?'))

with open('stages.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['etapa', 'bloque_ptr', 'fec188_metatiles_L0', 'fec18c_metatiles_L1', 'fec190_mapa_L0', 'fec194_mapa_L1', 'campo8_desc', 'campoC_desc'])
    for n, p, q, v8, vc in blocks:
        w.writerow([n, '%06X' % p] + ['%06X' % x for x in q] + ['%08X' % v8, '%08X' % vc])

# oráculo: todas las instrucciones move.l abs ROM -> abs VRAM del inventario
print('\n== move.l $ROM.l,$VRAM.l (oráculo de descifrado) ==')
rows = list(csv.DictReader(open('absrefs.csv')))
bypc = {}
for r in rows: bypc.setdefault(r['pc'], []).append(r)
seen = set()
with open('oracle_sites.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['pc', 'insn', 'rom_src', 'enc', 'dec'])
    for pc, rs in sorted(bypc.items()):
        regs = {r['region'] for r in rs}
        if 'VRAM' in regs and 'ROM' in regs and rs[0]['mnemonic'].startswith('move.l'):
            src = [r for r in rs if r['region'] == 'ROM'][0]
            a = int(src['address'], 16); e = u32(a); d = decrypt_long(e)
            print('%s  %s %s   enc=%08X dec=%08X' % (pc, rs[0]['mnemonic'], rs[0]['operands'], e, d))
            w.writerow([pc, rs[0]['mnemonic'] + ' ' + rs[0]['operands'], '%06X' % a, '%08X' % e, '%08X' % d])

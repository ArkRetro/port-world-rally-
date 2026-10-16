#!/usr/bin/env python3
"""Construye ranges.csv: [inicio,fin) CODE / DATA / UNKNOWN combinando desensamblado estático (code_insns.txt),
PCs ejecutados (cov_execpc.txt) y lecturas de datos de ROM (cov_dread.txt)."""
ROMSZ = 0x100000
code = bytearray(ROMSZ)     # 1 = código estático, 2 = además ejecutado
ins = {}
for l in open('code_insns.txt'):
    p = l.split(); ins[int(p[0], 16)] = int(p[1])
ex = {}
for l in open('cov_execpc.txt'):
    p = l.split(); ex[int(p[0], 16)] = int(p[1])
for a, sz in ins.items():
    v = 2 if a in ex else 1
    for i in range(a, a + sz): code[i] = v
dread = bytearray(ROMSZ)
for l in open('cov_dread.txt'):
    p = l.split(); a = int(p[0], 16)
    if a < ROMSZ and code[a] == 0:
        dread[a] = 1; dread[min(a + 1, ROMSZ - 1)] = 1
# clasificación por byte: C (código), D (dato leído), U (no tocado)
cls = bytearray(ROMSZ)
for i in range(ROMSZ):
    cls[i] = 1 if code[i] else (2 if dread[i] else 0)
# fusionar: huecos U de < 64 bytes entre D se consideran D (tablas con palabras no leídas)
GAP = 64
ranges = []
i = 0
while i < ROMSZ:
    j = i
    while j < ROMSZ and cls[j] == cls[i]: j += 1
    ranges.append([i, j, cls[i]])
    i = j
merged = []
for s, e, c in ranges:
    if merged and c == 0 and (e - s) < GAP and merged[-1][2] == 2:
        merged[-1][1] = e; continue
    if merged and merged[-1][2] == c:
        merged[-1][1] = e; continue
    if merged and c == 2 and merged[-1][2] == 0 and (merged[-1][1] - merged[-1][0]) < GAP and len(merged) > 1 and merged[-2][2] == 2:
        merged[-2][1] = e; merged.pop(); continue
    merged.append([s, e, c])
names = {0: 'UNKNOWN', 1: 'CODE', 2: 'DATA'}
with open('ranges.csv', 'w') as f:
    f.write('start,end,len,class,exec_bytes,exec_pct,note\n')
    for s, e, c in merged:
        exb = sum(1 for i in range(s, e) if code[i] == 2) if c == 1 else 0
        note = ''
        if c == 1: note = 'ejecutado %.0f%%' % (100.0 * exb / (e - s))
        f.write('%06X,%06X,%d,%s,%d,%.1f,%s\n' % (s, e, e - s, names[c], exb, (100.0 * exb / (e - s)) if c == 1 else 0, note))
tc = sum(1 for i in range(ROMSZ) if cls[i] == 1); td = sum(1 for i in range(ROMSZ) if cls[i] == 2); tu = ROMSZ - tc - td
tx = sum(1 for i in range(ROMSZ) if code[i] == 2)
print('CODE=%d (%.1f%%) ejecutado=%d (%.1f%% del código) DATA leído=%d (%.1f%%) UNKNOWN=%d (%.1f%%) rangos=%d' % (tc, 100.0 * tc / ROMSZ, tx, 100.0 * tx / tc, td, 100.0 * td / ROMSZ, tu, 100.0 * tu / ROMSZ, len(merged)))

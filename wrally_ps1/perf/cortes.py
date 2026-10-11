"""Cortes de las frases de CD-DA en una medida de psxprof (eventos.txt): cada pista que vuelve a sonar desde la mitad
(snd_resume) con donde se habia parado, cuanto tiempo estuvo cortada y cuanto se salta o se repite.
uso: cortes.py dir_medida imagen.cue"""
import sys, os
d, cue = sys.argv[1], sys.argv[2]
starts = []
for l in open(cue):
    p = l.split()
    if p[:2] == ['INDEX', '01']:
        m, s, f = map(int, p[2].split(':')); starts.append((m * 60 + s) * 75 + f)
CLK = 33868800.0
last_stop = None; n = 0
for l in open(os.path.join(d, 'eventos.txt')):
    p = l.split()
    if len(p) < 6: continue
    cyc, fr, ev = int(p[0]), int(p[1]), p[3]
    if ev == 'CDDA_STOP': last_stop = (cyc, fr, int(p[6]))
    elif ev == 'CDDA_PLAY':
        lba = int(p[5])
        if lba not in starts and last_stop:
            n += 1
            print('cuadro %d: sigue en %d (parada en %d en el cuadro %d): %.2f s cortada, %+d sectores' % (
                fr, lba, last_stop[2], last_stop[1], (cyc - last_stop[0]) / CLK, lba - last_stop[2]))
print('cortes: %d' % n)

"""Sonido de World Rally para PS1 (desde las ROM del usuario; uso personal):
 - frases cortas del OKI y efectos largos -> PS-ADPCM en cd/SNDFX.BIN (se sube entero a la RAM de la SPU al arrancar)
 - el resto de frases largas (> LONG s, musica) -> pistas de audio CD-DA (snd/tNN.wav, 44,1 kHz estereo)
 - snd_tab.c: frase -> direccion SPU/8 o pista"""
import numpy as np, struct, subprocess, wave, os, sys
from scipy.signal import resample_poly
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from paths import rom, work
from oki import oki, phr, oki_decode
RATE = 1000000 / 132
LONG = 4.0
SPU0 = 0x1010
# frases largas que van a la SPU como las cortas, mientras quepan: los efectos del coche, que el juego corta y vuelve
# a empezar (derrapes: en los registros SNDLOG de rc_host con la direccion oscilante suenan de media un 20 % de lo
# que duran). Desde el CD empezarian tarde (busqueda) y a trozos. Primero las que salen en los registros, por usos;
# luego las de los otros bancos de efectos (3 a 8), que suenan igual
EFECTOS = [95, 93, 89, 108, 92, 109, 81]
os.makedirs('wav', exist_ok=True)
fx = bytearray(); addr = [0] * 128; track = [0] * 128; ntr = 2; tracks = []; hecho = {}
pcm = {}
for p in sorted(phr):
    s, en, b = phr[p]
    if s >= 0x30000:
        if b is None: continue
        base = b * 0x10000 - 0x30000
    else: base = 0
    data = oki[base + s:base + en + 1]
    pcm[p] = (data, len(data) * 2 / RATE)
def senal(p):
    x = oki_decode(pcm[p][0]).astype(np.float64)
    return 32767 * np.tanh(x * 1.5 / 32767)
def cabe(n): return SPU0 + (len(fx) + n + 2047) // 2048 * 2048 <= 0x80000
def a_spu(p):
    data = pcm[p][0]
    if data in hecho: addr[p] = hecho[data]; return True        # misma muestra que otra frase
    np.concatenate([np.zeros(28), senal(p)]).astype('<i2').tofile(work('fx.raw'))
    subprocess.check_call(['./psxadpcm', work('fx.raw'), work('fx.adp')])
    blk = open(work('fx.adp'), 'rb').read()
    if not cabe(len(blk)): return False
    addr[p] = hecho[data] = (SPU0 + len(fx)) >> 3
    fx.extend(blk)
    return True
def a_cd(p):
    global ntr
    up = resample_poly(senal(p), 44100 * 132, 1000000)
    up = np.clip(up, -32768, 32767).astype('<i2')
    fn = 'wav/t%02d.wav' % ntr
    w = wave.open(fn, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(np.repeat(up, 2).tobytes()); w.close()
    track[p] = ntr; tracks.append(fn); ntr += 1
largas = [p for p in sorted(pcm) if pcm[p][1] > LONG]
for p in sorted(pcm):
    if p not in largas: assert a_spu(p), 'las frases cortas no caben en la RAM de la SPU'
for p in [q for q in EFECTOS if q in largas] + [q for q in largas if q not in EFECTOS]:
    if p in EFECTOS and a_spu(p): print('  efecto %d (%.1f s) a la SPU' % (p, pcm[p][1])); continue
    if p in EFECTOS: print('  efecto %d (%.1f s): no cabe en la SPU, va al CD' % (p, pcm[p][1]))
    a_cd(p)
os.makedirs('../ps1/cd', exist_ok=True)
fx += b'\0' * (-len(fx) % 2048)
assert SPU0 + len(fx) <= 0x80000, len(fx)
open('../ps1/cd/SNDFX.BIN', 'wb').write(fx)
with open('snd_tab.c', 'w') as f:
    f.write('/* generado por sndps1.py */\nconst unsigned short snd_addr[128] = {%s};\nconst unsigned char snd_track[128] = {%s};\nconst unsigned snd_fx_size = %d;\n' % (','.join(map(str, addr)), ','.join(map(str, track)), len(fx)))
open('tracks.txt', 'w').write('\n'.join(tracks) + '\n')
print('efectos', len(fx), 'bytes; pistas', len(tracks))

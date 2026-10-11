"""Sonido de World Rally para PS1 (desde las ROM del usuario; uso personal):
 - frases cortas del OKI -> PS-ADPCM en cd/SNDFX.BIN (se sube entero a la RAM de la SPU al arrancar)
 - frases largas (> LONG s) -> pistas de audio CD-DA (snd/tNN.wav, 44,1 kHz estereo)
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
os.makedirs('wav', exist_ok=True)
fx = bytearray(); addr = [0] * 128; track = [0] * 128; ntr = 2; tracks = []
for p in sorted(phr):
    s, en, b = phr[p]
    if s >= 0x30000:
        if b is None: continue
        base = b * 0x10000 - 0x30000
    else: base = 0
    data = oki[base + s:base + en + 1]
    pcm = oki_decode(data).astype(np.float64)
    pcm = 32767 * np.tanh(pcm * 1.5 / 32767)
    if len(data) * 2 / RATE > LONG:
        up = resample_poly(pcm, 44100 * 132, 1000000)
        up = np.clip(up, -32768, 32767).astype('<i2')
        st = np.repeat(up, 2)
        fn = 'wav/t%02d.wav' % ntr
        w = wave.open(fn, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(st.tobytes()); w.close()
        track[p] = ntr; tracks.append(fn); ntr += 1
    else:
        a = np.concatenate([np.zeros(28), pcm]).astype('<i2')
        a.tofile(work('fx.raw'))
        subprocess.check_call(['./psxadpcm', work('fx.raw'), work('fx.adp')])
        blk = open(work('fx.adp'), 'rb').read()
        addr[p] = (SPU0 + len(fx)) >> 3
        fx += blk
assert SPU0 + len(fx) <= 0x80000, len(fx)
os.makedirs('../ps1/cd', exist_ok=True)
fx += b'\0' * (-len(fx) % 2048)
open('../ps1/cd/SNDFX.BIN', 'wb').write(fx)
with open('snd_tab.c', 'w') as f:
    f.write('/* generado por sndps1.py */\nconst unsigned short snd_addr[128] = {%s};\nconst unsigned char snd_track[128] = {%s};\nconst unsigned snd_fx_size = %d;\n' % (','.join(map(str, addr)), ','.join(map(str, track)), len(fx)))
open('tracks.txt', 'w').write('\n'.join(tracks) + '\n')
print('efectos', len(fx), 'bytes; pistas', len(tracks))

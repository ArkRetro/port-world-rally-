"""Melodia de la intro Neo Geo: se graba de un juego del usuario (MAME, orden de sonido 0x02 de la BIOS)
y se codifica en ADPCM-A. Uso: jingle.py grabacion.wav t_inicio t_fin -> build/jingle.bin"""
import sys, wave, subprocess, numpy as np
from scipy.signal import resample_poly
wav, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
w = wave.open(wav); r = w.getframerate()
d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels()).astype(np.float64)
x = d.mean(1)[int(t0 * r):int(t1 * r)]
n = len(x); fade = int(0.05 * r)
x[-fade:] *= np.linspace(1, 0, fade)
y = resample_poly(x, 18518, r)
y *= 0.5 * 32767 / np.abs(y).max()
y.astype('<i2').tofile('/tmp/claude-0/j.raw')
subprocess.check_call(['/home/claude/wrally/neo/snd/adpcma_enc', '/tmp/claude-0/j.raw', '/home/claude/wrally/neo/build/jingle.bin'])
print('jingle', len(open('/home/claude/wrally/neo/build/jingle.bin', 'rb').read()), 'bytes')

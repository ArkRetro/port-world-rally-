"""Musica del menu (work/musica.bin, va dentro de INICIO.EXE): la de cada juego en PS-ADPCM mono para la SPU.
 - WR1: la del attract tal como la toca el port (pistas de CD-DA de wrally_ps1/snd: la primera cadena, la frase 118,
   hasta que entra la segunda, la musica de los menus, a los CORTE1 s, medido en el attract del port) y en bucle la
   segunda entera. A la frecuencia del OKI. El silencio que separa las frases encadenadas en las pistas (SEP sectores
   de sndps1.py, para que el CD-DA pare entre una y otra) se deja en un cuadro, como en la placa.
 - WR2: la del titulo, grabada del port (musica.sh: work/mus/w2.wav) desde que empieza a sonar despues del arranque
   hasta el silencio de detras, o work/musica2.wav entera si esta. A la frecuencia del GAE1 (tono 0x2D6), en bucle.
Cabecera: por pieza, bytes, desplazamiento del bucle (desde su principio) y tono (u32 cada uno); luego los datos de
las dos, cada una en multiplos de 64 bytes (la DMA de la SPU va de 16 en 16 palabras). Uso personal."""
import os, sys, struct, subprocess, wave
import numpy as np
from scipy.signal import resample_poly
AQUI = os.path.dirname(os.path.abspath(__file__))
SND1 = os.path.join(AQUI, '..', 'wrally_ps1', 'snd')
os.chdir(AQUI)
CORTE1 = 10.78
F1, F2 = 1000000 / 132, 7812.5                # OKI (1 MHz / 132) y GAE1 (tono 0x2D6 en la SPU)
SPU_MAX = 0x80000 - 0x1010                     # la RAM de la SPU desde 0x1010 (antes, los bufer de captura)


def lee(fn):
    w = wave.open(fn)
    a = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(np.float64)
    a = a.reshape(-1, w.getnchannels()).mean(axis=1)
    return a, w.getframerate()


def sin_huecos(a, f):
    """tramos de ceros exactos (el relleno entre frases) de mas de 2 000 muestras -> un cuadro"""
    z = np.concatenate([[0], (a == 0).astype(np.int8), [0]])
    d = np.flatnonzero(np.diff(z)); fuera = np.zeros(len(a), bool)
    for i, j in zip(d[::2], d[1::2]):
        if j - i > 2000: fuera[i + int(f / 60):j] = True
    return a[~fuera]


def a_frec(a, de, a_f):
    from fractions import Fraction
    q = Fraction(a_f).limit_denominator(1000) / de
    return resample_poly(a, q.numerator, q.denominator)


def adpcm(x, bucle):
    """x: muestras a la frecuencia final; bucle: muestra en la que empieza el bucle (multiplo de 28)"""
    x = np.concatenate([x, np.zeros(-len(x) % 28)])
    x = np.clip(x / np.max(np.abs(x)) * 0.9 * 32767, -32768, 32767).astype('<i2')
    x.tofile('work/mus/m.raw')
    subprocess.check_call([os.path.join(SND1, 'psxadpcm'), 'work/mus/m.raw', 'work/mus/m.adp'])
    b = bytearray(open('work/mus/m.adp', 'rb').read())
    b[bucle // 28 * 16 + 1] |= 4                    # principio del bucle
    b[-15] = 3                                      # ultimo bloque: fin y repetir desde el principio del bucle
    b += b'\0' * (-len(b) % 64)
    return bytes(b), bucle // 28 * 16


os.makedirs('work/mus', exist_ok=True)
pistas = [l.strip() for l in open(os.path.join(SND1, 'tracks.txt')) if l.strip()]
a, f = lee(os.path.join(SND1, pistas[0]))
b, _ = lee(os.path.join(SND1, pistas[1]))
intro = a_frec(sin_huecos(a[:int(CORTE1 * f)], f), f, F1)
intro = intro[:len(intro) // 28 * 28]
m1 = adpcm(np.concatenate([intro, a_frec(sin_huecos(b, f), f, F1)]), len(intro))

if os.path.exists('work/musica2.wav'):
    c, f = lee('work/musica2.wav'); c = a_frec(c, f, F2)
else:
    c, f = lee('work/mus/w2.wav')
    v = int(f * 0.05)
    rms = np.sqrt(np.convolve(c * c, np.ones(v) / v, 'same'))
    t = int(f * 20)                                 # despues del sonido del arranque de la BIOS
    i0 = t + int(np.argmax(rms[t:] > 300))
    j = i0 + int(f * 8)
    calla = rms[j:] < 30
    i1 = j + int(np.argmax(calla)) if calla.any() else len(c)
    print('WR2: musica del titulo de %.2f a %.2f s' % (i0 / f, i1 / f))
    assert 10 < (i1 - i0) / f < 40, 'no se encuentra la musica del titulo en la grabacion'
    c = a_frec(c[i0:i1], f, F2)
m2 = adpcm(c, 0)

tono = [round(4096 * F1 / 44100), round(4096 * F2 / 44100)]
assert len(m1[0]) + len(m2[0]) <= SPU_MAX, 'la musica no cabe en la RAM de la SPU'
cab = struct.pack('<6I', len(m1[0]), m1[1], tono[0], len(m2[0]), m2[1], tono[1])
open(sys.argv[1] if len(sys.argv) > 1 else 'work/musica.bin', 'wb').write(cab + m1[0] + m2[0])
print('musica: WR1 %d B (%.1f s), WR2 %d B (%.1f s)' % (len(m1[0]), len(m1[0]) / 16 * 28 / F1, len(m2[0]),
                                                      len(m2[0]) / 16 * 28 / F2))

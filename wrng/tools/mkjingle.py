#!/usr/bin/env python3
"""Optional jingle for the start-up screen (src/eye.s): an audio file of your own, remixed with a 16-bit
console flavour and encoded to YM2610 ADPCM-A in a free part of the V ROM, played from phrase slot 0x6F
(Z80 command 0xEF).  Without an audio file the V ROM and the sample table are copied unchanged and the
start-up screen stays silent (that is the build the kit reproduces: the jingle is never distributed).

usage: mkjingle.py <vrom_in> <samples_in.inc> <vrom_out> <samples_out.inc> <jingle.inc> [audio] [--wav preview.wav]

The remix (numpy + scipy, ffmpeg to decode the audio; only needed with an audio file): mono, band-limited
like a console DSP (100 Hz - 7 kHz), soft saturation, the echo of a 16-bit console (one tap, 112 ms,
feedback 0.35), a short fade out, at most MAX_S seconds, resampled to the ADPCM-A rate (18518 Hz).
"""
import shutil
import subprocess
import sys

SLOT = 0x6F                     # last entry of the sample table (unused by the arcade)
MAX_S = 8.0
RATE = 8000000 / 432            # YM2610 ADPCM-A sample rate


def remix(path):
    import numpy as np
    from scipy import signal
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-ac', '1', '-ar', '44100', '-f', 'f32le', '-'],
                         check=True, stdout=subprocess.PIPE).stdout
    x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    x = x[:int(MAX_S * 44100)]
    nz = np.nonzero(np.abs(x) > 0.01)[0]                        # trim the silence around it
    if len(nz):
        x = x[max(0, nz[0] - 441):nz[-1] + 4410]
    x /= max(1e-9, np.max(np.abs(x)))
    b, a = signal.butter(2, [100, 7000], btype='band', fs=44100)
    x = signal.lfilter(b, a, x)
    x = np.tanh(1.8 * x) / np.tanh(1.8)                         # warm, a little compressed
    d = int(0.112 * 44100)                                      # console echo
    y = x.copy()
    for i in range(d, len(y)):
        y[i] += 0.35 * y[i - d]
    y = 0.75 * x + 0.25 * y
    fade = int(0.35 * 44100)
    y[-fade:] *= np.linspace(1, 0, fade)
    y = signal.resample_poly(y, 18518, 44100)
    y /= max(1e-9, np.max(np.abs(y)))
    return [float(v) * 0.92 * 2047 for v in y]                  # 12-bit target for the encoder


def main(a):
    vin, sin, vout, sout, inc = a[:5]
    audio = a[5] if len(a) > 5 and not a[5].startswith('--') else ''
    if not audio:
        shutil.copyfile(vin, vout)
        shutil.copyfile(sin, sout)
        open(inc, 'w').write('        .equ    JINGLE_FRAMES, 0\n')
        return
    sys.path.insert(0, __file__.rsplit('/', 1)[0])
    import oki2adpcma
    s12 = remix(audio)
    n = len(s12)
    s12 += [0.0] * ((-n) % 512)
    data, dec12 = oki2adpcma.adpcma_encode(s12)
    v = bytearray(open(vin, 'rb').read())
    used = len(v)
    while used > 0 and v[used - 1] in (0x00, 0x80, 0x08):
        used -= 1
    start = (used + 0xFFFF) & ~0xFFFF                           # next 64 KB, inside one 1 MB bank
    if (start >> 20) != ((start + len(data) - 1) >> 20):
        start = ((start >> 20) + 1) << 20
    assert start + len(data) <= len(v), 'no room in the V ROM'
    v[start:start + len(data)] = data
    end = start + len(data) - 1
    open(vout, 'wb').write(v)
    lines = open(sin).read().split('\n')
    rows = [i for i, l in enumerate(lines) if l.strip().startswith('.db')]
    lines[rows[SLOT]] = '        .db 0x%02X,0x%02X,0x%02X,0x%02X   ; %d jingle (mkjingle.py)' % (
        (start >> 8) & 255, start >> 16, (end >> 8) & 255, end >> 16, SLOT)
    open(sout, 'w').write('\n'.join(lines))
    frames = int(n / RATE * 60)
    open(inc, 'w').write('        .equ    JINGLE_FRAMES, %d\n' % frames)
    print('jingle: %.2f s, V ROM 0x%06X-0x%06X, slot 0x%02X' % (n / RATE, start, end, SLOT))
    if '--wav' in a:
        oki2adpcma.write_wav(a[a.index('--wav') + 1], [s * 16 for s in dec12[:n]], int(RATE))


if __name__ == '__main__':
    main(sys.argv[1:])

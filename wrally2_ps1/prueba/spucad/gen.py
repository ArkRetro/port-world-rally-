"""Trozos sinteticos para la sonda spucad: NT tonos (nada de la ROM) de duraciones todas distintas (0,2 a 3 s), cada
uno un sample PS-ADPCM con las reglas de diseno.md 5.2: filtro 0 en el primer bloque, banderas 3 (fin + repetir) en
el ultimo, sin inicio de bucle. La sonda reconoce que trozo acaba de sonar por su duracion (se diferencian en
>= 50 ms) y el oido por el tono (una escala: la cadena suena como una melodia; un trozo repetido o saltado se nota).
Salida: trozos.bin (bloque de silencio + trozos, se sube a 0x1000 de la RAM de la SPU) y trozos.h (tabla).
Codificador: copia en Python de wrally_ps1/snd/psxadpcm.c (busqueda exhaustiva de filtro y desplazamiento)."""
import math, time
FS = 7812.5                                      # frecuencia original de la placa; la PS1 lo toca a tono 0x2DF
F0 = [0, 60, 115, 98, 122]; F1 = [0, 0, -52, -55, -60]
# (ms, Hz): cortos agudos y largos graves; la sonda da los cortos a la voz 0, los medios a la 4, los largos a la 5.
# Escala pentatonica de do: notas a >= 48 Hz unas de otras (un analisis por espectrograma de la grabacion las separa)
TROZOS = [(200, 1760), (250, 1568), (320, 1319), (400, 1047), (500, 880), (650, 784),
          (800, 659), (1000, 523), (1300, 440), (1700, 392), (2200, 330), (3000, 262)]

def tono(ms, hz):
    """seno con 5 ms de entrada y salida suaves (cada trozo se articula como una nota)"""
    n = round(ms * FS / 1000 / 28) * 28
    f = int(FS * 0.005); x = []
    for i in range(n):
        a = 0.5
        if i < f: a *= i / f
        if i >= n - f: a *= (n - 1 - i) / f
        x.append(int(round(a * 32767 * math.sin(2 * math.pi * hz * i / FS))))
    return x

def c16(v): return -32768 if v < -32768 else 32767 if v > 32767 else v
def div_c(a, b): return -((-a) // b) if a < 0 else a // b   # division truncada hacia cero, como en C

def codifica(x):
    """bloques de 16 bytes; el estado del decodificador sigue de un bloque al siguiente. Devuelve (bytes, error)"""
    s1 = s2 = 0; out = bytearray(); et = 0
    nb = len(x) // 28
    for b in range(nb):
        blk = x[b * 28:(b + 1) * 28]
        be = None
        for f in ([0] if b == 0 else range(5)):             # primer bloque: filtro 0 (no depende de lo anterior)
            for sh in range(13):
                o1, o2, e, nib = s1, s2, 0, []
                for i in range(28):
                    pred = (o1 * F0[f] + o2 * F1[f] + 32) >> 6
                    r = blk[i] - pred
                    q = div_c((r << sh) + (2048 if r >= 0 else -2048), 4096)
                    if q > 7: q = 7
                    if q < -8: q = -8
                    d = c16(((q << 12) >> sh) + pred)
                    e += (d - blk[i]) * (d - blk[i])
                    if be is not None and e >= be[0]: break
                    nib.append(q & 15); o2 = o1; o1 = d
                else:
                    be = (e, f, sh, nib, o1, o2)
        e, f, sh, nib, s1, s2 = be
        et += e
        blq = bytearray(16); blq[0] = sh | (f << 4); blq[1] = 3 if b == nb - 1 else 0
        for i in range(28): blq[2 + i // 2] |= nib[i] << ((i & 1) * 4)
        out += blq
    return bytes(out), et

def decodifica(d):
    """comprobacion: decodificador de la SPU (sin interpolar)"""
    o1 = o2 = 0; y = []
    for b in range(len(d) // 16):
        sh, f = d[b * 16] & 15, d[b * 16] >> 4
        for i in range(28):
            q = (d[b * 16 + 2 + i // 2] >> ((i & 1) * 4)) & 15
            if q >= 8: q -= 16
            v = c16(((q << 12) >> sh) + ((o1 * F0[f] + o2 * F1[f] + 32) >> 6))
            y.append(v); o2 = o1; o1 = v
    return y

t0 = time.time()
datos = bytearray(bytes([0, 3]) + bytes(14))      # 0x1000: silencio, un bloque que se repite a si mismo
addr, blk, ms, hz = [], [], [], []
for m, h in TROZOS:
    x = tono(m, h); d, e = codifica(x); y = decodifica(d)
    sn = sum(v * v for v in x); sr = sum((a - b) ** 2 for a, b in zip(x, y))
    assert len(d) % 16 == 0 and d[-15] == 3 and d[0] >> 4 == 0 and all(d[i] & 4 == 0 for i in range(1, len(d), 16))
    addr.append((0x1000 + len(datos)) >> 3); blk.append(len(d) // 16); ms.append(len(x) * 1000 / FS); hz.append(h)
    datos += d
    print('%5d ms %5d Hz %4d bloques  SNR %.1f dB' % (m, h, len(d) // 16, 10 * math.log10(sn / max(sr, 1))))
assert len(set(blk)) == len(blk), 'duraciones repetidas'
assert all(abs(a - b) * 28 / FS >= 0.05 for i, a in enumerate(blk) for b in blk[i + 1:]), 'trozos a menos de 50 ms'
while len(datos) % 64: datos.append(0)             # el DMA a la SPU va en bloques de 64 bytes
open('trozos.bin', 'wb').write(datos)
with open('trozos.h', 'w') as f:
    f.write('/* generado por gen.py: tabla de los trozos de trozos.bin (subido a 0x1000 de la RAM de la SPU) */\n')
    f.write('#define NT %d\n#define TZ_BYTES %d\n' % (len(TROZOS), len(datos)))
    f.write('static const u16 tz_addr[NT] = {%s};   /* direccion / 8, como el registro */\n' % ', '.join('0x%04X' % a for a in addr))
    f.write('static const u16 tz_blk[NT] = {%s};    /* bloques de 16 bytes (28 muestras) */\n' % ', '.join(str(b) for b in blk))
    f.write('static const u16 tz_ms[NT] = {%s};     /* duracion nominal a 7812,5 Hz */\n' % ', '.join(str(int(round(m))) for m in ms))
    f.write('static const u16 tz_hz[NT] = {%s};\n' % ', '.join(str(h) for h in hz))
print('trozos.bin %d bytes, %.1f s de audio, %.1f s' % (len(datos), sum(ms) / 1000, time.time() - t0))

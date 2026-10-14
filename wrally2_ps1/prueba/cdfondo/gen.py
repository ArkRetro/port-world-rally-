"""Datos de prueba de la sonda cdfondo (nada de la ROM): cd/DATOS.BIN, SECTORES sectores de 2048 bytes (6000 por
defecto: 12 MB, 40 s a doble velocidad; la imagen queda en ~17 MB). Cada sector lleva su indice en la primera palabra
y 511 palabras de un xorshift32 sembrado con el indice, lo mismo que comprueba main.c (semilla y comprueba): un sector
que llegue con otro indice es un hueco o una repeticion; uno con el indice bien y el flujo mal, contenido malo."""
import os, struct
N = int(os.environ.get('SECTORES', '6000'))
M = 0xFFFFFFFF
os.makedirs('cd', exist_ok=True)
with open('cd/DATOS.BIN', 'wb') as f:
    for i in range(N):
        s = ((i * 0x9E3779B1) ^ 0xA5A5A5A5) & M or 1
        w = [i]
        for k in range(511):
            s ^= (s << 13) & M; s ^= s >> 17; s ^= (s << 5) & M
            w.append(s)
        f.write(struct.pack('<512I', *w))
print('cd/DATOS.BIN: %d sectores, %d bytes, %.1f s a 2x' % (N, N * 2048, N / 150.0))

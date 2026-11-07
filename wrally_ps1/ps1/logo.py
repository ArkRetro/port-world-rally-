"""Logo de arranque (en lugar del de PlayStation): ps1/logo_pina.png (RGBA, sin fondo) a cd/LOGO.BIN, sobre negro,
150 filas de alto, centrado en 128 pixeles de ancho, 15 bits por pixel (lo que pide la GPU), 8 filas por sector"""
import numpy as np, os
from PIL import Image
ALTO, ANCHO = 150, 128
im = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logo_pina.png')).convert('RGBA')
w = round(im.width * ALTO / im.height)
a = np.array(im.resize((w, ALTO), Image.LANCZOS)).astype(np.float64) / 255
rgb = a[..., :3] * a[..., 3:4]                      # sobre negro
# a 5 bits por componente con un tramado ordenado (sin bandas en las gafas)
bay = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16 - 0.5
y, x = np.mgrid[0:ALTO, 0:w]
q = np.clip(np.floor(rgb * 31 + 0.5 + bay[y % 4, x % 4][..., None] * 0.9), 0, 31).astype(np.uint16)
px = q[..., 0] | (q[..., 1] << 5) | (q[..., 2] << 10)
out = np.zeros(((ALTO + 7) // 8 * 8, ANCHO), np.uint16)
x0 = (ANCHO - w) // 2
out[:ALTO, x0:x0 + w] = px
os.makedirs('cd', exist_ok=True)
out.astype('<u2').tofile('cd/LOGO.BIN')
print('LOGO.BIN %dx%d (%d sectores)' % (ANCHO, out.shape[0], out.nbytes // 2048))

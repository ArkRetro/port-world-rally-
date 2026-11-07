"""Rutas comunes: ROM del usuario (WR_ROM, por defecto ./rom) y ficheros generados (./work)"""
import os
ROOT = os.path.dirname(os.path.abspath(__file__))
ROM = os.environ.get('WR_ROM', os.path.join(ROOT, 'rom'))
WORK = os.path.join(ROOT, 'work')
os.makedirs(WORK, exist_ok=True)
def rom(n): return os.path.join(ROM, n)
def work(n): return os.path.join(WORK, n)

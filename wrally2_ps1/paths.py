"""Rutas comunes: ROM del usuario (WR2_ROM, por defecto ./rom) y ficheros generados (./work).
WR1 = carpeta del port del World Rally 1, de donde se reutiliza lo generico (dec68, cd.c, psxprof...)"""
import os
ROOT = os.path.dirname(os.path.abspath(__file__))
WR1 = os.path.join(ROOT, '..', 'wrally_ps1')
ROM = os.environ.get('WR2_ROM', os.path.join(ROOT, 'rom'))
WORK = os.path.join(ROOT, 'work')
os.makedirs(WORK, exist_ok=True)
def rom(n): return os.path.join(ROM, n)
def work(n): return os.path.join(WORK, n)

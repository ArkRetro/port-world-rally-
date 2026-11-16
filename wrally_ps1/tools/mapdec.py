"""Descifrado de pares de palabras de VRAM de Gaelco (para los mapas de los recorridos). El descifrado no va en el
repositorio: work/gaelcrpt/gaelcrpt_gen.py lo genera tools/gaelcrpt_extrae.py desde el gaelcrpt.cpp de MAME"""
import numpy as np, functools, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work
sys.path.insert(0, work('gaelcrpt'))
try:
    import gaelcrpt_gen
except ImportError:
    raise SystemExit('ERROR: falta work/gaelcrpt/gaelcrpt_gen.py (el descifrado de la VRAM de Gaelco, que se saca del '
                     'fuente de MAME en tu maquina): ejecuta en wrally_ps1/ python3 tools/gaelcrpt_extrae.py --descargar')
@functools.lru_cache(maxsize=None)
def _d0(e0): return gaelcrpt_gen.decrypt(0, 0, e0)
@functools.lru_cache(maxsize=None)
def _pair(e0, e1):
    d0 = _d0(e0); return d0, gaelcrpt_gen.decrypt(e0, d0, e1)
def dec_pairs(e0, e1):
    r = [_pair(int(a), int(b)) for a, b in zip(e0, e1)]
    return np.array([x[0] for x in r], np.int64), np.array([x[1] for x in r], np.int64)

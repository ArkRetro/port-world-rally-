"""Descifrado de pares de palabras de VRAM de Gaelco (para los mapas de los recorridos)"""
import numpy as np, gcrypt, functools
@functools.lru_cache(maxsize=None)
def _d0(e0): return gcrypt.decrypt(0, 0, e0)
@functools.lru_cache(maxsize=None)
def _pair(e0, e1):
    d0 = _d0(e0); return d0, gcrypt.decrypt(e0, d0, e1)
def dec_pairs(e0, e1):
    r = [_pair(int(a), int(b)) for a, b in zip(e0, e1)]
    return np.array([x[0] for x in r], np.int64), np.array([x[1] for x in r], np.int64)

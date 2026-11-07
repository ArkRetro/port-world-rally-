"""Lectura de las ROM de MAME del usuario: carpeta o zip (WR2_ROM). Busca cada fichero en la raiz y en wrally2a/"""
import os, zipfile
from paths import ROM

class RomSet:
    def __init__(self, path=ROM):
        self.path = path; self.z = None; self.names = {}
        if os.path.isfile(path) and zipfile.is_zipfile(path):
            self.z = zipfile.ZipFile(path)
            for n in self.z.namelist(): self.names.setdefault(os.path.basename(n).lower(), n)
        else:
            for dp, _, fs in os.walk(path):
                for f in fs: self.names.setdefault(f.lower(), os.path.join(dp, f))
    def has(self, n): return n.lower() in self.names
    def read(self, n):
        p = self.names.get(n.lower())
        if p is None: raise SystemExit('falta %s en %s' % (n, self.path))
        if self.z: return self.z.read(p)
        with open(p, 'rb') as f: return f.read()

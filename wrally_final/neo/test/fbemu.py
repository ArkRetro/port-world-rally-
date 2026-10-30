"""Frontend libretro minimo para probar FBNeo (sin pantalla)."""
import ctypes as C, os, sys
from PIL import Image
CORE = '/tmp/claude-0/lrfbneo/src/burner/libretro/fbneo_neogeo_libretro.so'
ENV_CB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO_CB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIOB_CB = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
POLL_CB = C.CFUNCTYPE(None)
INPUT_CB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
class GameInfo(C.Structure):
    _fields_ = [('path', C.c_char_p), ('data', C.c_void_p), ('size', C.c_size_t), ('meta', C.c_char_p)]
class Var(C.Structure):
    _fields_ = [('key', C.c_char_p), ('value', C.c_char_p)]
B, Y, SELECT, START, UP, DOWN, LEFT, RIGHT, A, X, L, R = range(12)
LOG_CB = C.CFUNCTYPE(None, C.c_int, C.c_char_p)
class LogIf(C.Structure):
    _fields_ = [('log', LOG_CB)]
VERBOSE = os.environ.get('FBLOG')
def _log(level, fmt):
    if VERBOSE: sys.stdout.write('[%d] %s' % (level, fmt.decode(errors='replace')))
_logcb = LOG_CB(_log)
_logif = LogIf(_logcb)
class Emu:
    def __init__(self, rom, sysdir, opts=None):
        self.opts = {k.encode(): C.create_string_buffer(v.encode()) for k, v in (opts or {}).items()}
        self._sys = C.create_string_buffer(sysdir.encode())
        self.lib = C.CDLL(CORE)
        self.frame = None; self.fmt = 0
        self.pressed = set()
        self._cbs = [ENV_CB(self._env), VIDEO_CB(self._video), AUDIO_CB(lambda l, r: None),
                     AUDIOB_CB(lambda d, n: n), POLL_CB(lambda: None), INPUT_CB(self._input)]
        L = self.lib
        L.retro_set_environment(self._cbs[0]); L.retro_set_video_refresh(self._cbs[1])
        L.retro_set_audio_sample(self._cbs[2]); L.retro_set_audio_sample_batch(self._cbs[3])
        L.retro_set_input_poll(self._cbs[4]); L.retro_set_input_state(self._cbs[5])
        L.retro_init()
        gi = GameInfo(os.path.abspath(rom).encode(), None, 0, None)
        self.ok = L.retro_load_game(C.byref(gi))
    def _env(self, cmd, data):
        cmd &= 0xFFFF
        if cmd == 10:
            self.fmt = C.cast(data, C.POINTER(C.c_int)).contents.value
            return True
        if cmd in (9, 31):
            C.cast(data, C.POINTER(C.c_char_p))[0] = C.cast(self._sys, C.c_char_p).value
            return True
        if cmd == 3:
            C.cast(data, C.POINTER(C.c_bool))[0] = True; return True
        if cmd == 15:
            v = C.cast(data, C.POINTER(Var)).contents
            b = self.opts.get(v.key)
            if b is None: return False
            v.value = C.cast(b, C.c_char_p).value; return True
        if cmd == 70: return True
        if cmd == 27:
            C.cast(data, C.POINTER(LogIf))[0] = _logif; return True
        return False
    def _video(self, data, w, h, pitch):
        if data: self.frame = (C.string_at(data, pitch * h), w, h, pitch)
    def _input(self, port, device, index, idn):
        return 1 if (port == 0 and device == 1 and idn in self.pressed) else 0
    def run(self, n=1, keys=None):
        if keys is not None: self.pressed = set(keys)
        for _ in range(n): self.lib.retro_run()
    def shot(self, path):
        raw, w, h, pitch = self.frame
        if self.fmt == 1: im = Image.frombuffer('RGB', (w, h), raw, 'raw', 'BGRX', pitch, 1)
        else: im = Image.frombuffer('RGB', (w, h), raw, 'raw', 'BGR;16', pitch, 1)
        im.save(path); return im
if __name__ == '__main__':
    e = Emu('/tmp/claude-0/fbt/roms/wrallyng.zip', '/tmp/claude-0/fbt/sys')
    print('carga', e.ok)

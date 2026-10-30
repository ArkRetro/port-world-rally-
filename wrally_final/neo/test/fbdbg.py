from fbemu import *
import ctypes as C
e = Emu('/tmp/claude-0/fbt/roms/wrallyng.zip', '/tmp/claude-0/fbt/sys')
L = e.lib
L.retro_get_memory_data.restype = C.c_void_p; L.retro_get_memory_size.restype = C.c_size_t
for i in range(6):
    e.run(100)
    p = L.retro_get_memory_data(2); n = L.retro_get_memory_size(2)
    ram = C.string_at(p, n)
    # RAM con bytes intercambiados (little endian host)
    w = lambda a: ram[(a-0x100000)+1] << 8 | ram[(a-0x100000)]
    print(i, 'fmt', e.fmt, 'flag', hex(w(0x10BFFE)), 'C1A6', hex(w(0x10C1A6)), 'frame px', max(e.frame[0]) if e.frame else None)

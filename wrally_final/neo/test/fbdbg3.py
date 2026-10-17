from fbemu import *
import ctypes as C, pickle, struct
S=pickle.load(open('/home/claude/wrally/neo/build/syms.pkl','rb'))
e = Emu('/tmp/claude-0/fbt/roms/wrallyng.zip', '/tmp/claude-0/fbt/sys')
L = e.lib
L.retro_get_memory_data.restype = C.c_void_p; L.retro_get_memory_size.restype = C.c_size_t
def rd():
    ram = C.string_at(L.retro_get_memory_data(2), L.retro_get_memory_size(2))
    return bytes(ram[i ^ 1] for i in range(len(ram)))
for i in range(12):
    e.run(5)
    r = rd()
    w = lambda a: struct.unpack('>H', r[a-0x100000:a-0x100000+2])[0]
    print((i+1)*5, 'flag', hex(w(0x10BFFE)), 'ngfr', w(S['ng_frames']), 'exc', w(0x10BF10), r[0xBF12:0xBF22].hex())

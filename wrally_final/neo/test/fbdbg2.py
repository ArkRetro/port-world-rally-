from fbemu import *
import ctypes as C, pickle
S=pickle.load(open('/home/claude/wrally/neo/build/syms.pkl','rb'))
e = Emu('/tmp/claude-0/fbt/roms/wrallyng.zip', '/tmp/claude-0/fbt/sys')
L = e.lib
L.retro_get_memory_data.restype = C.c_void_p; L.retro_get_memory_size.restype = C.c_size_t
for i in range(40):
    e.run(5)
    ram = C.string_at(L.retro_get_memory_data(2), L.retro_get_memory_size(2))
    w = lambda a: ram[(a-0x100000)+1] << 8 | ram[(a-0x100000)]
    print(i*5+5, 'flag', hex(w(0x10BFFE)), 'ngfr', w(S['ng_frames']), 'vfix', w(S['dbg_vfix']), 'req', hex(ram[0xFDAE^1]), 'sysmode', hex(ram[0xFD80^1]))

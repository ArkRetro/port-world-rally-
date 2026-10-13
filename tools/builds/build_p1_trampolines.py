import struct,sys,os,shutil
LAB='/tmp/claude-0/-home-user-port-world-rally-/307720d2-25b6-5a9f-8356-703187f15fbf/scratchpad/lab'
W=LAB+'/work/H1P2meas'
SP='/tmp/claude-0/-home-user-port-world-rally-/307720d2-25b6-5a9f-8356-703187f15fbf/scratchpad'
src=open(f'{LAB}/roms/wrv6/wrv6-p1.p1','rb').read()
b=bytearray(len(src)); b[0::2]=src[1::2]; b[1::2]=src[0::2]   # now big-endian CPU order
def w16(a,v): b[a]=v>>8; b[a+1]=v&0xff
def w32(a,v): w16(a,v>>16); w16(a+2,v&0xffff)
def r32(a): return struct.unpack('>I',b[a:a+4])[0]
assert all(x==0xff for x in b[0x1000:0x1018])
tr={10:(0x1000,0x2000E0),25:(0x1006,0x200C76),26:(0x100C,0x200098),27:(0x1012,0x20008E)}
for vec,(ta,tgt) in tr.items():
    assert r32(vec*4)==tgt,(vec,hex(r32(vec*4)))
    w16(ta,0x4EF9); w32(ta+2,tgt); w32(vec*4,ta)
out=bytearray(len(b)); out[0::2]=b[1::2]; out[1::2]=b[0::2]
open(f'{W}/roms/wrv6t/wrv6t-p1.p1','wb').write(out)
for ext in ['s1','m1','v1','c1','c2']:
    d=f'{W}/roms/wrv6t/wrv6t-{ext}.{ext}'
    if not os.path.exists(d): os.symlink(f'{LAB}/roms/wrv6/wrv6-{ext}.{ext}',d)
diff=[i for i in range(len(src)) if src[i]!=out[i]]
print('bytes changed',len(diff),'ranges',hex(min(diff)),hex(max(diff)))
# .neo builds for hardware
neo=bytearray(open(f'{SP}/rar/WRally_neogeo_final_v6.neo','rb').read())
assert neo[0x1000:0x1000+0x200000]==src
neo[0x1000:0x1000+0x200000]=out
os.makedirs(f'{W}/neo',exist_ok=True)
open(f'{W}/neo/WRally_v6T_p1tramp.neo','wb').write(neo)
neo2=bytearray(neo); struct.pack_into('<I',neo2,0x28,0x216)
open(f'{W}/neo/WRally_v6T_p1tramp_ngh0216.neo','wb').write(neo2)
print('neo ngh', hex(struct.unpack('<I',neo[0x28:0x2c])[0]), hex(struct.unpack('<I',neo2[0x28:0x2c])[0]))

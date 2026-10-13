# build checksum-display variant of WR v6 ("ck"): computes 16-bit word sums once per boot at 0x270A
# (where v6 forces #$0E56) and prints  S1 S2 S3  on the self-test "ROM Checksum" line.
#  S1 = sum of words 0x0EE000-0x0EEFFF  (shine pointer table + data, H4 region)
#  S2 = sum of words 0x000000-0x0FFFFF  (P1 as seen by CPU, includes this patch)
#  S3 = sum of words 0x200000-0x2FFFEF  (P2 bank 0, excludes bank-register area)
import sys,struct,zlib
src_neo, out_neo, out_p1 = sys.argv[1], sys.argv[2], sys.argv[3]
n=bytearray(open(src_neo,'rb').read())
P=struct.unpack('<I',n[4:8])[0]
p=bytearray(n[0x1000:0x1000+P])
A=bytearray(p[i^1] for i in range(len(p)))   # CPU (big-endian) view
def off(a): return a if a<0x100000 else a-0x100000
def put(addr,b): A[off(addr):off(addr)+len(b)]=b
def chk(addr,hexs):
    b=bytes.fromhex(hexs.replace(' ','')); assert A[off(addr):off(addr)+len(b)]==b,(hex(addr),A[off(addr):off(addr)+len(b)].hex())
chk(0x270a,'323C0E56 303CFFFF 343CFFFF 4E714E714E714E71')
chk(0x27ee,'4BF90010F366 4EB900012172')
assert all(x==0xff for x in A[0x1000:0x1200])
H=lambda s: bytes.fromhex(s.replace(' ',''))
L=lambda a: struct.pack('>I',a)
CACHE=0x109F00   # RAM never read/written by v6 between game start and frame 2900 (measured)
# --- mini assembler with labels
prog=[]   # (label or None, kind, data)
def I(h): prog.append((None,'b',H(h)))
def lab(x): prog.append((x,'l',None))
def bsr(x): prog.append((None,'bsr',x))
def lea_a5(x): prog.append((None,'lea5',x))
lab('calc')
I('48E7BFFE')                       # movem.l d0/d2-d7/a0-a6,-(a7)
I('41F9000EE000 363C07FF 7A00')     # lea $EE000,a0 ; move.w #$7ff,d3 ; moveq #0,d5
I('DA58 51CBFFFC')                  # add.w (a0)+,d5 ; dbra d3,*-2
I('91C8 7C00 780F')                 # suba.l a0,a0 ; moveq #0,d6 ; moveq #15,d4
I('13C000300001 363C7FFF DC58 51CBFFFC 51CCFFEE')  # kick wd; 0x8000 words; dbra d4
I('41F900200000 7E00 780E')         # lea $200000,a0 ; moveq #0,d7 ; moveq #14,d4
I('13C000300001 363C7FFF DE58 51CBFFFC 51CCFFEE')
I('13C000300001 363C7FF7 DE58 51CBFFFC')            # last 0x7FF8 words up to 0x2FFFEF
I('33C5'+L(CACHE).hex()+'33C6'+L(CACHE+2).hex()+'33C7'+L(CACHE+4).hex())
I('3205 4CDF7FFD 4E75')             # move.w d5,d1 ; movem.l (a7)+,... ; rts
lab('show')
I('48E7FFFE')                       # movem.l d0-d7/a0-a6,-(a7)
I('4BF90010F366 4EB900012172')      # print buffer (S1) as original code did
I('3039'+L(CACHE+2).hex()); bsr('pr')
I('3039'+L(CACHE+4).hex()); bsr('pr')
I('3039'+L(CACHE).hex()+'41F90010F366 4EB90000FD88')  # restore S1 text in buffer
I('4CDF7FFF 4E75')
lab('pr')
I('41F90010F366 4EB90000FD88')      # hex(D0) -> buffer (before D0 is clobbered)
lea_a5('sp'); I('4EB900012172')     # print " "
I('4BF90010F366 4EB900012172 4E75') # print buffer
lab('sp'); I('2003')
lab('lbl'); I('01515A0200 524F4D20 0201 0300')   # "ROM " label at same position/colours as the original line
# assemble at 0x1000
def assemble(base):
    labels={}; pc=base
    for l,k,d in prog:
        if k=='l': labels[l]=pc
        elif k=='b': pc+=len(d)
        elif k=='bsr': pc+=4
        elif k=='lea5': pc+=6
    out=bytearray(); pc=base
    for l,k,d in prog:
        if k=='b': out+=d; pc+=len(d)
        elif k=='bsr': disp=labels[d]-(pc+2); assert -32768<=disp<32768; out+=H('6100')+struct.pack('>h',disp); pc+=4
        elif k=='lea5': out+=H('4BF9')+L(labels[d]); pc+=6
    return out,labels
code,labels=assemble(0x1000)
put(0x1000,code)
put(0x270a,H('4EB9')+L(labels['calc'])+H('303CFFFF 343CFFFF 4E714E714E71'))
put(0x27ee,H('4EB9')+L(labels['show'])+H('4E714E714E71'))
chk(0x27e2,'4BF9000ED896'); put(0x27e2,H('4BF9')+L(labels['lbl']))   # shorter label so 3 values fit on screen
def wsum(lo,hi):
    s=0
    for a in range(lo,hi,2): s=(s+((A[off(a)]<<8)|A[off(a)+1]))&0xffff
    return s
S1=wsum(0x0ee000,0x0ef000); S2=wsum(0x000000,0x100000); S3=wsum(0x200000,0x2ffff0)
p2=bytes(A[i^1] for i in range(len(A)))
n[0x1000:0x1000+P]=p2
open(out_neo,'wb').write(n); open(out_p1,'wb').write(p2)
print('labels',{k:hex(v) for k,v in labels.items()},'code bytes',len(code))
print('expected on screen: ROM %04X %04X %04X'%(S1,S2,S3))
print('p1 crc %08x  neo crc %08x size %d'%(zlib.crc32(p2),zlib.crc32(n),len(n)))

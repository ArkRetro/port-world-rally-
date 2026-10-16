import sys
d=open('/tmp/claude-0/-home-user-port-world-rally-/0e54570e-879c-581d-b19c-42bb81ddf7e3/scratchpad/lab/src/wrally/wrdallas.bin','rb').read()
bases=[(d[0x4044+2*i]<<8)|d[0x4044+2*i+1] for i in range(12)]
AX={0x00:'EA98+30h&FFF',0x80:'EA9C+18h&3FFF',0x40:'EAA0+18h',0xC0:'EAA4'}
SZ={1:2,2:2,3:6,4:4,5:4,6:2,7:2,9:2,0xA:2,0xB:2,0xC:2}
NM={0:'SIGN',1:'C271=1',2:'SETDIR',3:'GOTO_PT',4:'RACE_START',5:'RACE_START2',6:'NOP6',7:'FINISH',8:'CHECKPOINT',9:'C105=1',0xA:'MSG(C14B)',0xB:'LIGHT(C1A1)',0xC:'NOPC'}
def decode(stage, out):
    base=bases[stage]; end = bases[stage+1] if stage<11 else 0x3800
    off=0
    out.write("=== STAGE %d  base=%04X (len %d)\n"%(stage,base,end-base))
    while base+off < end:
        p=base+off
        b0,b1,cmd=d[p],d[p+1],d[p+2]
        ax=b0&0xC0; thr=((b0&0x3F)<<8)|b1
        if ax==0xC0 and (b0&0x20): thr|=0xC000
        direc='>=' if cmd&0x80 else '< '
        line="  off=%04X @%04X  trig %-14s %s %04X : "%(off,p,AX[ax],direc,thr)
        if (cmd&0x60)==0x60:
            out.write(line+"END (C16C=0)\n"); off+=4; break
        if (cmd&0x60)==0x40:
            out.write(line+"SKIP(%02X)\n"%cmd); off+=4; continue
        q=2; parts=[]
        while True:
            c=d[p+q]; k=c&0x1F; sz=SZ.get(k,4) if k!=8 else 6
            params=d[p+q+1:p+q+sz]
            if k==8:
                parts.append("CHECKPOINT x=%04X y=%04X (EB1C=x-18h) nextoff=%04X"%((params[1]<<8)|params[2],(params[3]<<8)|params[4], off+(q-2)+8))
                off=off+(q-2)+8; q=None; break
            parts.append("%s(%s)"%(NM.get(k,'SIGN'),' '.join('%02X'%x for x in params)))
            if c&0x20: q+=sz; continue
            off = off + q + sz; break
        out.write(line+" ; ".join(parts)+"\n")
    out.write("  (end offset %04X, next base %04X)\n"%(off, base+off))
with open('scripts.txt','w') as out:
    out.write("bases: %s\n"%' '.join('%04X'%b for b in bases))
    for s in range(12): decode(s,out)
print(open('scripts.txt').read()[:200])

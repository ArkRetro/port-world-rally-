"""Python port of Lightening Force's unpacker (ROM 0x000EAE, Kosinski-style).

entry(rom, base, idx): address of entry idx in an archive (byte 1 = count, then
32-bit offsets from base+2). dec(rom, addr) -> (data, compressed size).
Example: the player ship is entry 3 of the archive at 0x90000, bytes 0x100-0x5FF.
"""
def entry(rom,base,idx):
    n=rom[base+1]; assert idx<n
    off=int.from_bytes(rom[base+2+idx*4:base+6+idx*4],'big'); return base+off
def dec(rom,p):
    out=bytearray(); start=p
    def word():
        nonlocal p; w=rom[p]|(rom[p+1]<<8); p+=2; return w
    bits=word(); cnt=16
    def bit():
        nonlocal bits,cnt
        b=bits&1; bits>>=1; cnt-=1
        if cnt==0: bits=word(); cnt=16
        return b
    while True:
        if bit():
            out.append(rom[p]); p+=1; continue
        if bit()==0:
            c=(bit()<<1); c|=bit(); c+=2
            d=rom[p]-0x100; p+=1
        else:
            lo=rom[p]; hi=rom[p+1]; p+=2
            d=(((hi>>3)|0xe0)<<8|lo)-0x10000
            c=hi&7
            if c: c+=2
            else:
                c=rom[p]; p+=1
                if c==0: break
                if c==1: continue
                c+=1
        for _ in range(c): out.append(out[d])
    return bytes(out),p-start

# verificacion: decodifica ADPCM-A de la V-ROM y guarda WAV de una frase
import numpy as np, struct, wave, sys
STEPS=[16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552]
INC=[-1,-1,-1,-1,2,5,7,9]
v=open('/home/claude/wrally/neo/build/v1.bin','rb').read(); t=open('/home/claude/wrally/neo/build/snd_ztab.bin','rb').read()
p=int(sys.argv[1]); sb,eb=struct.unpack('<HH',t[p*4:p*4+4])
acc=0; si=0; out=[]
for b in v[sb*256:(eb+1)*256]:
    for n in (b>>4,b&15):
        d=(2*(n&7)+1)*STEPS[si]//8
        acc=(acc+(-d if n&8 else d))&0xfff; si=max(0,min(48,si+INC[n&7]))
        out.append((acc-4096 if acc&0x800 else acc)*16)
w=wave.open('/tmp/claude-0/p%d.wav'%p,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(18519); w.writeframes(np.array(out,'<i2').tobytes()); w.close()
a=np.array(out); print(p, len(out)/18519.0, 's', 'rms', np.sqrt((a.astype(float)**2).mean()))

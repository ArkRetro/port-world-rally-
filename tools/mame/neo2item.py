import os, struct, sys
src, root, name, desc = sys.argv[1:5]
d = open(src, "rb").read()
p, s, m, v1, v2, c = struct.unpack("<6I", d[4:28])
o = 4096
def take(n):
    global o
    r = d[o:o + n]; o += n
    return r
P, S, M, V, C = take(p), take(s), take(m), take(v1 + v2), take(c)
out=os.path.join(root,'roms',name); os.makedirs(out, exist_ok=True)
if len(M)<0x20000: M=M+M*((0x20000//len(M))-1)
for fn, blob in [(name+"-p1.p1", P), (name+"-s1.s1", S), (name+"-m1.m1", M), (name+"-v1.v1", V),
                   (name+"-c1.c1", C[0::2]), (name+"-c2.c2", C[1::2])]:
    open(os.path.join(out, fn), "wb").write(blob)
pl = max(p,0x100000)
item=f'''	<software name="{name}">
		<description>{desc}</description><year>2026</year><publisher>lab</publisher>
		<sharedfeat name="release" value="MVS,AES" /><sharedfeat name="compatibility" value="MVS,AES" />
		<part name="cart" interface="neo_cart">
			<dataarea name="maincpu" width="16" endianness="big" size="{hex(pl)}"><rom loadflag="load16_word_swap" name="{name}-p1.p1" offset="0" size="{hex(p)}" /></dataarea>
			<dataarea name="fixed" size="{hex(s)}"><rom name="{name}-s1.s1" offset="0" size="{hex(s)}" /></dataarea>
			<dataarea name="audiocpu" size="{hex(len(M))}"><rom name="{name}-m1.m1" offset="0" size="{hex(len(M))}" /></dataarea>
			<dataarea name="ymsnd:adpcma" size="{hex(v1+v2)}"><rom name="{name}-v1.v1" offset="0" size="{hex(v1+v2)}" /></dataarea>
			<dataarea name="sprites" size="{hex(c)}">
				<rom loadflag="load16_byte" name="{name}-c1.c1" offset="0" size="{hex(c//2)}" />
				<rom loadflag="load16_byte" name="{name}-c2.c2" offset="1" size="{hex(c//2)}" />
			</dataarea>
		</part>
	</software>
'''
x=open(os.path.join(root,'hash','neogeo.xml')).read()
if f'name="{name}"' not in x:
    x=x.replace('</softwarelist>', item+'</softwarelist>')
    open(os.path.join(root,'hash','neogeo.xml'),'w').write(x)
print('ok',name)

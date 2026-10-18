"""PIXELS logo drawn in the style of the SEGA logo of Lightening Force (96x32, palette line 1).

Index 1 is the outline and the inner groove; 2..10 is the diagonal gradient the
game animates with its palette (same indices the SEGA logo uses).
"""
from PIL import Image, ImageDraw
W,H=96,32
def glyphs():
    g={}
    def new(w): im=Image.new('1',(w,30),0); return im,ImageDraw.Draw(im)
    im,d=new(15)                       # P
    d.rectangle((0,0,5,29),1); d.rounded_rectangle((0,0,14,17),radius=5,fill=1); d.rectangle((6,6,8,11),0); g['P']=im
    im,d=new(7); d.rectangle((0,0,6,29),1); g['I']=im
    im,d=new(17)                       # X
    d.polygon([(0,0),(6,0),(16,29),(10,29)],1); d.polygon([(10,0),(16,0),(6,29),(0,29)],1); g['X']=im
    im,d=new(14)                       # E
    d.rectangle((0,0,5,29),1); d.rectangle((0,0,13,5),1); d.rectangle((0,12,11,17),1); d.rectangle((0,24,13,29),1); g['E']=im
    im,d=new(13); d.rectangle((0,0,5,29),1); d.rectangle((0,24,12,29),1); g['L']=im
    im,d=new(15)                       # S
    d.rounded_rectangle((0,0,14,17),radius=6,fill=1); d.rectangle((6,6,14,11),0)
    d.rounded_rectangle((0,12,14,29),radius=6,fill=1); d.rectangle((0,18,8,23),0); d.rectangle((6,12,14,17),1); d.rectangle((0,12,8,17),1)
    d.rectangle((9,6,14,11),0); g['S']=im
    return g
def dist(mask,x,y):
    for r in range(1,8):
        for dy in range(-r,r+1):
            for dx in range(-r,r+1):
                if max(abs(dx),abs(dy))!=r: continue
                yy,xx=y+dy,x+dx
                if not (0<=yy<H and 0<=xx<W) or not mask[yy][xx]: return r
    return 8
def logo(text='PIXELS', gap=2, groove=True):
    g=glyphs(); total=sum(g[c].width for c in text)+gap*(len(text)-1)
    mask=[[0]*W for _ in range(H)]; letter=[[None]*W for _ in range(H)]
    x0=(W-total)//2
    for c in text:
        im=g[c]
        for y in range(30):
            for x in range(im.width):
                if im.getpixel((x,y)): mask[y+1][x0+x]=1; letter[y+1][x0+x]=x0
        x0+=im.width+gap
    px=[[0]*W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if not mask[y][x]: continue
            edge=any(not (0<=y+dy<H and 0<=x+dx<W and mask[y+dy][x+dx]) for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)))
            if edge: px[y][x]=1; continue
            if groove and dist(mask,x,y)==3:
                px[y][x]=1; continue
            xl=x-letter[y][x]+2
            idx=7+int((xl-7)//4 - (y-2)/6.25)
            px[y][x]=max(2,min(10,idx))
    return px
def tiles(px):
    """48 tiles, 12x4, row by row (the order the game maps them: 0x100 + y*12 + x)."""
    out=bytearray()
    for ty in range(4):
        for tx in range(12):
            for y in range(8):
                r=px[ty*8+y][tx*8:tx*8+8]
                out+=bytes((r[i]<<4)|r[i+1] for i in range(0,8,2))
    return bytes(out)

def literal_stream(data):
    """Encode for the game's unpacker (ROM 0xEAE) using literals only, then the end marker.
    Like Kosinski, the next 16-bit descriptor is read as soon as the 16th bit is used."""
    out = bytearray(b'\0\0'); pos = 0; val = 0; n = 0
    def bit(b):
        nonlocal pos, val, n
        val |= b << n; n += 1
        if n == 16:
            out[pos:pos+2] = val.to_bytes(2, 'little'); pos = len(out); out.extend(b'\0\0'); val = 0; n = 0
    for byte in data:
        bit(1); out.append(byte)
    bit(0); bit(1); out.extend(b'\0\0\0')          # long copy with count 0 = end
    out[pos:pos+2] = val.to_bytes(2, 'little')
    return bytes(out)

if __name__=='__main__':
    px=logo()
    for r in px: print(''.join('%x'%p if p else '.' for p in r))

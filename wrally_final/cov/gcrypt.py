P1, P2 = 0x1f, 0x522a
def B(x, n): return (x >> n) & 1
def bitswap(v, *bits):
    r = 0
    for b in bits: r = (r << 1) | B(v, b)
    return r
SW = [(1,2,0,14,12,15,4,8,13,7,3,6,11,5,10,9),(14,10,4,15,1,6,12,11,8,0,9,13,7,3,5,2),
      (2,13,15,1,12,8,14,4,6,0,9,5,10,7,3,11),(3,8,1,13,14,4,15,0,10,2,7,12,6,11,9,5)]
def decrypt(ep, dp, e):
    swap = (B(dp,8)<<1)|B(dp,7); typ = (B(dp,12)<<1)|B(dp,2)
    res = bitswap(e, *SW[swap]) ^ P2
    if typ == 0: k = 0b111010
    elif typ == 1: k = B(dp,0)|(B(dp,1)<<1)|(B(dp,1)<<2)|(B(ep,3)<<3)|(B(ep,8)<<4)|(B(ep,15)<<5)
    elif typ == 2: k = B(ep,5)|(B(dp,5)<<1)|(B(ep,7)<<2)|(B(ep,3)<<3)|(B(ep,13)<<4)|(B(ep,14)<<5)
    else: k = B(ep,0)|(B(ep,9)<<1)|(B(ep,6)<<2)|(B(dp,4)<<3)|(B(ep,2)<<4)|(B(dp,11)<<5)
    k ^= P1
    res = (res & 0xffc0) | ((res + k) & 0x3f)
    res ^= P1
    if typ == 0: k = B(e,9)|(B(res,2)<<1)|(B(e,5)<<2)|(B(res,5)<<3)|(B(res,4)<<4)
    elif typ == 1: k = B(dp,2)|(B(ep,4)<<1)|(B(dp,14)<<2)|(B(res,1)<<3)|(B(dp,12)<<4)
    elif typ == 2: k = B(ep,6)|(B(dp,6)<<1)|(B(dp,15)<<2)|(B(res,0)<<3)|(B(dp,7)<<4)
    else: k = B(dp,2)|(B(dp,9)<<1)|(B(ep,5)<<2)|(B(dp,1)<<3)|(B(ep,10)<<4)
    k ^= P1
    res = (res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800)
    res ^= (P1 << 6) | (P1 << 11)
    return bitswap(res, 2,6,0,11,14,12,7,10,5,4,8,3,9,1,13,15)
def dec_long(v):
    e0, e1 = v >> 16, v & 0xffff
    d0 = decrypt(0, 0, e0)
    return (d0 << 16) | decrypt(e0, d0, e1)

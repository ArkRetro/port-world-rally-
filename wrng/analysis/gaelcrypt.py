#!/usr/bin/env python3
"""Cifrado de VRAM de Gaelco (World Rally: param1=0x1f, param2=0x522a). Port de src/mame/gaelco/gaelcrpt.cpp.
decrypt_pair(enc_hi, enc_lo) -> (dec_hi, dec_lo): descifra la pareja de palabras escrita por un mismo move.l
(1.ª palabra decrypt(0,0,w); 2.ª decrypt(enc_prev, dec_prev, w)). Una escritura move.w aislada = decrypt(0,0,w)."""
P1, P2 = 0x1F, 0x522A

def bit(v, n): return (v >> n) & 1

def bitswap16(v, *bits):
    r = 0
    for i, b in enumerate(bits):
        r |= bit(v, b) << (15 - i)
    return r

def decrypt(enc_prev, dec_prev, enc):
    swap = (bit(dec_prev, 8) << 1) | bit(dec_prev, 7)
    typ = (bit(dec_prev, 12) << 1) | bit(dec_prev, 2)
    if swap == 0: res = bitswap16(enc, 1, 2, 0, 14, 12, 15, 4, 8, 13, 7, 3, 6, 11, 5, 10, 9)
    elif swap == 1: res = bitswap16(enc, 14, 10, 4, 15, 1, 6, 12, 11, 8, 0, 9, 13, 7, 3, 5, 2)
    elif swap == 2: res = bitswap16(enc, 2, 13, 15, 1, 12, 8, 14, 4, 6, 0, 9, 5, 10, 7, 3, 11)
    else: res = bitswap16(enc, 3, 8, 1, 13, 14, 4, 15, 0, 10, 2, 7, 12, 6, 11, 9, 5)
    res ^= P2
    if typ == 0: k = 0b111010
    elif typ == 1: k = bit(dec_prev, 0) | bit(dec_prev, 1) << 1 | bit(dec_prev, 1) << 2 | bit(enc_prev, 3) << 3 | bit(enc_prev, 8) << 4 | bit(enc_prev, 15) << 5
    elif typ == 2: k = bit(enc_prev, 5) | bit(dec_prev, 5) << 1 | bit(enc_prev, 7) << 2 | bit(enc_prev, 3) << 3 | bit(enc_prev, 13) << 4 | bit(enc_prev, 14) << 5
    else: k = bit(enc_prev, 0) | bit(enc_prev, 9) << 1 | bit(enc_prev, 6) << 2 | bit(dec_prev, 4) << 3 | bit(enc_prev, 2) << 4 | bit(dec_prev, 11) << 5
    k ^= P1
    res = (res & 0xFFC0) | ((res + k) & 0x003F)
    res ^= P1
    if typ == 0: k = bit(enc, 9) | bit(res, 2) << 1 | bit(enc, 5) << 2 | bit(res, 5) << 3 | bit(res, 4) << 4
    elif typ == 1: k = bit(dec_prev, 2) | bit(enc_prev, 4) << 1 | bit(dec_prev, 14) << 2 | bit(res, 1) << 3 | bit(dec_prev, 12) << 4
    elif typ == 2: k = bit(enc_prev, 6) | bit(dec_prev, 6) << 1 | bit(dec_prev, 15) << 2 | bit(res, 0) << 3 | bit(dec_prev, 7) << 4
    else: k = bit(dec_prev, 2) | bit(dec_prev, 9) << 1 | bit(enc_prev, 5) << 2 | bit(dec_prev, 1) << 3 | bit(enc_prev, 10) << 4
    k ^= P1
    res = (res & 0x003F) | ((res + (k << 6)) & 0x07C0) | ((res + (k << 11)) & 0xF800)
    res ^= (P1 << 6) | (P1 << 11)
    return bitswap16(res, 2, 6, 0, 11, 14, 12, 7, 10, 5, 4, 8, 3, 9, 1, 13, 15)

def decrypt_pair(hi, lo):
    dh = decrypt(0, 0, hi)
    return dh, decrypt(hi, dh, lo)

def decrypt_long(v):
    dh, dl = decrypt_pair((v >> 16) & 0xFFFF, v & 0xFFFF)
    return (dh << 16) | dl

if __name__ == '__main__':
    import struct, sys
    rom = open('/tmp/claude-0/-home-user-port-world-rally-/0e54570e-879c-581d-b19c-42bb81ddf7e3/scratchpad/lab/wr_cpu.bin', 'rb').read()
    for a in (0xF09FC, 0xE4260, 0xEDD46, 0xDF5C8, 0xEEC7C, 0xEDDB6):
        v = struct.unpack('>I', rom[a:a + 4])[0]
        print('ROM %06X: %08X -> %08X' % (a, v, decrypt_long(v)))

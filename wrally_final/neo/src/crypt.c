/* Descifrado de escrituras a VRAM de Gaelco (parametros World Rally 0x1f, 0x522a) */
#include "ng.h"
extern const u16 sw_tab[4][2][256];   /* bitswap inicial: [swap][byte alto/bajo][valor] */
extern const u16 fin_tab[2][256];     /* bitswap final */
#define P1 0x1f
#define P2 0x522a
#define B(x, n) (((x) >> (n)) & 1)
u16 gdecrypt(u16 ep, u16 dp, u16 e)
{
    int swap = (B(dp, 8) << 1) | B(dp, 7);
    int type = (B(dp, 12) << 1) | B(dp, 2);
    u16 res = sw_tab[swap][0][e >> 8] | sw_tab[swap][1][e & 0xff];
    u16 k;
    res ^= P2;
    switch (type) {
    case 0: k = 0x3a; break;
    case 1: k = B(dp,0) | (B(dp,1)<<1) | (B(dp,1)<<2) | (B(ep,3)<<3) | (B(ep,8)<<4) | (B(ep,15)<<5); break;
    case 2: k = B(ep,5) | (B(dp,5)<<1) | (B(ep,7)<<2) | (B(ep,3)<<3) | (B(ep,13)<<4) | (B(ep,14)<<5); break;
    default: k = B(ep,0) | (B(ep,9)<<1) | (B(ep,6)<<2) | (B(dp,4)<<3) | (B(ep,2)<<4) | (B(dp,11)<<5); break;
    }
    k ^= P1;
    res = (res & 0xffc0) | ((res + k) & 0x3f);
    res ^= P1;
    switch (type) {
    case 0: k = B(e,9) | (B(res,2)<<1) | (B(e,5)<<2) | (B(res,5)<<3) | (B(res,4)<<4); break;
    case 1: k = B(dp,2) | (B(ep,4)<<1) | (B(dp,14)<<2) | (B(res,1)<<3) | (B(dp,12)<<4); break;
    case 2: k = B(ep,6) | (B(dp,6)<<1) | (B(dp,15)<<2) | (B(res,0)<<3) | (B(dp,7)<<4); break;
    default: k = B(dp,2) | (B(dp,9)<<1) | (B(ep,5)<<2) | (B(dp,1)<<3) | (B(ep,10)<<4); break;
    }
    k ^= P1;
    res = (res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800);
    res ^= (P1 << 6) | (P1 << 11);
    return fin_tab[0][res >> 8] | fin_tab[1][res & 0xff];
}

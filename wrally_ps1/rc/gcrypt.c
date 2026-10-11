/* Gaelco VRAM decrypt (World Rally: 0x1f, 0x522a), bitswap directo. gdecrypt es la referencia (la usa el
   banco de pruebas con Musashi); el codigo recompilado usa la version en linea de rc_rt.h con estas tablas */
typedef unsigned short u16;
#define P1 0x1f
#define P2 0x522a
#define B(x, n) (((x) >> (n)) & 1)
static const unsigned char SW[4][16] = {{1,2,0,14,12,15,4,8,13,7,3,6,11,5,10,9},{14,10,4,15,1,6,12,11,8,0,9,13,7,3,5,2},
 {2,13,15,1,12,8,14,4,6,0,9,5,10,7,3,11},{3,8,1,13,14,4,15,0,10,2,7,12,6,11,9,5}};
static const unsigned char FIN[16] = {2,6,0,11,14,12,7,10,5,4,8,3,9,1,13,15};
static u16 bs(u16 v, const unsigned char *b) { u16 r = 0; int i; for (i = 0; i < 16; i++) r = (r << 1) | B(v, b[i]); return r; }
/* tablas: la permutacion de 16 bits es la OR de la de cada byte */
u16 rc_gsw[4][2][256], rc_gfin[2][256];
static int tab_ok;
void rc_gcrypt_init(void)
{
    int s, h, v;
    if (tab_ok) return;
    for (s = 0; s < 4; s++) for (h = 0; h < 2; h++) for (v = 0; v < 256; v++) rc_gsw[s][h][v] = bs((u16)(h ? v : v << 8), SW[s]);
    for (h = 0; h < 2; h++) for (v = 0; v < 256; v++) rc_gfin[h][v] = bs((u16)(h ? v : v << 8), FIN);
    tab_ok = 1;
}
#define BS(v, s) (rc_gsw[s][0][(v) >> 8] | rc_gsw[s][1][(v) & 0xFF])
#define BF(v) (rc_gfin[0][(v) >> 8] | rc_gfin[1][(v) & 0xFF])
u16 gdecrypt(u16 ep, u16 dp, u16 e)
{
    int swap = (B(dp, 8) << 1) | B(dp, 7), type = (B(dp, 12) << 1) | B(dp, 2);
    u16 res, k;
    if (!tab_ok) rc_gcrypt_init();
    res = BS(e, swap) ^ P2;
    switch (type) {
    case 0: k = 0x3a; break;
    case 1: k = B(dp,0) | (B(dp,1)<<1) | (B(dp,1)<<2) | (B(ep,3)<<3) | (B(ep,8)<<4) | (B(ep,15)<<5); break;
    case 2: k = B(ep,5) | (B(dp,5)<<1) | (B(ep,7)<<2) | (B(ep,3)<<3) | (B(ep,13)<<4) | (B(ep,14)<<5); break;
    default: k = B(ep,0) | (B(ep,9)<<1) | (B(ep,6)<<2) | (B(dp,4)<<3) | (B(ep,2)<<4) | (B(dp,11)<<5); break;
    }
    k ^= P1; res = (res & 0xffc0) | ((res + k) & 0x3f); res ^= P1;
    switch (type) {
    case 0: k = B(e,9) | (B(res,2)<<1) | (B(e,5)<<2) | (B(res,5)<<3) | (B(res,4)<<4); break;
    case 1: k = B(dp,2) | (B(ep,4)<<1) | (B(dp,14)<<2) | (B(res,1)<<3) | (B(dp,12)<<4); break;
    case 2: k = B(ep,6) | (B(dp,6)<<1) | (B(dp,15)<<2) | (B(res,0)<<3) | (B(dp,7)<<4); break;
    default: k = B(dp,2) | (B(dp,9)<<1) | (B(ep,5)<<2) | (B(dp,1)<<3) | (B(ep,10)<<4); break;
    }
    k ^= P1;
    res = (res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800);
    res ^= (P1 << 6) | (P1 << 11);
    return BF(res);
}

/* descifrado rapido de la segunda palabra: todo lo que depende solo de la primera va en gd_info */
extern const u16 gd_info[65536], sw_tab[4][2][256], fin_tab[2][256], dec_first[65536];
#define P1 0x1f
#define P2 0x522a
static inline u16 gdec2(u16 e0, u16 e1)
{
    u16 inf = gd_info[e0];
    const u16 *t = sw_tab[inf & 3][0];
    u16 res = (t[e1 >> 8] | t[256 + (e1 & 0xff)]) ^ P2;
    u16 k;
    res = (res & 0xffc0) | ((res + ((inf >> 4) & 0x3f)) & 0x3f);
    res ^= P1;
    k = inf >> 10;
    switch ((inf >> 2) & 3) {
    case 0: k = ((e1 >> 9) & 1) | ((res >> 1) & 2) | ((e1 >> 3) & 4) | ((res >> 2) & 8) | (res & 0x10); break;
    case 1: k |= (res << 2) & 8; break;
    case 2: k |= (res << 3) & 8; break;
    default: break;
    }
    k ^= P1;
    res = (res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800);
    res ^= (P1 << 6) | (P1 << 11);
    return fin_tab[0][res >> 8] | fin_tab[1][res & 0xff];
}

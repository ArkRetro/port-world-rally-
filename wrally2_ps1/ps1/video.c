/* Imagen de verdad de los menus (H3, formato A de doc/formatos.md): cada tile es un hueco de 8 x 16 halfwords (texels
   de 8 bits = pen 0-31) en una de las 10 paginas de textura de 8 bpp (bloques b0-b19 por pares); la CLUT es la paleta
   del juego, subida a S0 cuando cambia (fundidos gratis). Plano 0: 24 x 16 posiciones agrupadas por paleta (la GPU
   recarga la CLUT de 256 entradas cada vez que cambia); sprites de la copia del vblank anterior con el banco actual,
   en su orden; celdas de efecto como RESTA (B-F) y SUMA (B+F) con las CLUT fijas de la fila 246. Lo que no esta en la
   VRAM se pide al cargador (carga_falta) y se dibuja mientras como en el mosaico. Tambien la lista de la GPU y las
   primitivas sin textura que usa main.c (cifras, mosaico). Sin libc; comentarios sin acentos. */
#include "video.h"
#include "carga.h"
#define REG(a) (*(volatile u32 *)(a))
#define GP0 REG(0x1F801810)
#define GP1 REG(0x1F801814)
#define D2_MADR REG(0x1F8010A0)
#define D2_BCR REG(0x1F8010A4)
#define D2_CHCR REG(0x1F8010A8)
#define T1 t1_get()
static inline u16 t1_get(void)
{
    volatile u16 *r = (volatile u16 *)0xBF801110; u16 a = *r, b;
    while ((b = *r) != a) a = b;
    return a;
}

/* ------------------------------------------------------------ lista de la GPU */
void gpu_wait(void) { while (!(GP1 & (1 << 26))); }
void dma_wait(void) { while (D2_CHCR & (1 << 24)); }
/* nodos de hasta 16 palabras (el FIFO de la GPU; con nodos mas largos Mednafen pierde palabras y la GPU se queda a
   medio comando) seguidos en un bufer (lst), enlazados en orden y con primitivas enteras */
#define LSTN 12288                                /* 48 KB (diseno 2.1; el peor cuadro de la placa, 42 KB) */
static u32 lst[LSTN];
u32 *lp; static u32 *node;
static void l_close(u32 *next)
{
    u32 n = (u32)(lp - node - 1);
    node[0] = (n << 24) | ((u32)next & 0xFFFFFF);
}
void l_begin(void) { node = lst; lp = lst + 1; }
void l_room(int n)
{
    if (lp + n - node - 1 > 16 || lp + n + 2 >= lst + LSTN) {
        if (lp + n + 2 >= lst + LSTN) return;             /* lleno: se pierde el resto */
        l_close(lp); node = lp; lp = node + 1;
    }
}
int l_full(int n) { return lp + n + 2 >= lst + LSTN; }
void l_send(void)
{
    l_close((u32 *)0xFFFFFF);
    dma_wait(); gpu_wait(); D2_MADR = (u32)lst; D2_BCR = 0; D2_CHCR = 0x01000401;
}
void rect16(int x, int y, u32 rgb)
{
    if (l_full(2)) return;
    l_room(2); lp[0] = 0x78000000 | rgb; lp[1] = ((u32)(y & 0xFFFF) << 16) | (x & 0xFFFF); lp += 2;
}
void rect(int x, int y, int w, int h, u32 rgb)
{
    if (l_full(3)) return;
    l_room(3); lp[0] = 0x60000000 | rgb; lp[1] = ((u32)(y & 0xFFFF) << 16) | (x & 0xFFFF); lp[2] = ((u32)h << 16) | w; lp += 3;
}
/* color de la paleta del juego (xRRRRRGGGGGBBBBB) en 24 bits para la GPU (0xBBGGRR) */
u32 rgb24(u32 i)
{
    u32 v = *(const u16 *)(rc_pal + ((i & 0xFFF) << 1));
    return ((v >> 10) & 31) << 3 | (((v >> 5) & 31) << 11) | ((v & 31) << 19);
}
static const u16 font[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
void digits(int x, int y, u32 v, int n)
{
    int i, k, d[8];
    for (i = 0; i < n; i++) { d[i] = v % 10; v /= 10; }
    rect(x - 2, y - 2, n * 8 + 2, 14, 0x000000);
    for (i = 0; i < n; i++)
        for (k = 0; k < 15; k++)
            if (font[d[n - 1 - i]] & (1 << (14 - k))) rect(x + i * 8 + (k % 3) * 2, y + (k / 3) * 2, 2, 2, 0xFFFFFF);
}
/* subida a la VRAM (GP0 A0h) por DMA desde la RAM principal (d alineado a 4; w * h par). Vacia la cache de texturas
   antes: lo subido puede pisar texels que la GPU tiene guardados */
static void upload(int x, int y, int w, int h, const void *d)
{
    int words = w * h / 2, bs = 16;
    dma_wait(); gpu_wait();
    GP0 = 0x01000000;
    GP0 = 0xA0000000; GP0 = ((u32)y << 16) | x; GP0 = ((u32)h << 16) | w;
    if (words % bs) bs = words < 16 ? words : 1;
    D2_MADR = (u32)d; D2_BCR = ((u32)(words / bs) << 16) | bs; D2_CHCR = 0x01000201;
}

/* ------------------------------------------------------------ VRAM (formatos.md 4) */
/* paleta c: CLUT de 32 en x = 32 (c % 11), y = 240 + c / 11; identificador de CLUT de la GPU = y << 6 | x >> 4 */
#define PAL_X(c) (32 * ((c) % 11))
#define PAL_Y(c) (240 + (c) / 11)
#define CLUT_ID(x, y) ((u32)((y) << 6 | (x) >> 4))
#define CL_RESTA32 CLUT_ID(32, 246)
#define CL_SUMA32 CLUT_ID(64, 246)
/* pagina A p (0-9) = bloques 2p y 2p+1: x = 384 + 64 (2p % 10), y = 256 (2p / 10); hueco s: U = 16 (s & 15),
   V = 16 ((s >> 4) & 15), se sube a (x + 8 (s & 15), y + 16 ((s >> 4) & 15)), 8 x 16 halfwords */
#define NPAG 10
#define NHUECO (NPAG * 256)
#define PAG_X(p) (384 + 64 * ((2 * (p)) % 10))
#define PAG_Y(p) (256 * ((2 * (p)) / 10))
/* E1: pagina de 8 bpp (bit 7), dibujar en el area visible (bit 10); los bits 5-6 son el modo de semitransparencia */
#define E1_PAG(p) (0xE1000480u | (u32)(PAG_X(p) >> 6) | (u32)((PAG_Y(p) >> 8) << 4))

/* codigo -> hueco (0xFFFF: no esta) en dos niveles (diseno 2.1): raiz de 128 grupos de 256 codigos y 67 hojas que se
   dan al vuelo y se devuelven cuando se vacian; si hacen falta mas de 67, el tile no se registra (vid_sin_hoja) */
#define NHOJA 67
static u8 ts_raiz[128];                   /* hoja + 1 del grupo (0: ninguna) */
static u16 ts_hoja[NHOJA][256], ts_n[NHOJA];   /* huecos del grupo y cuantos codigos tiene */
static int ts_libres;
u32 vid_sin_hoja;
static inline u16 ts_get(u32 c) { u32 r = ts_raiz[c >> 8]; return r ? ts_hoja[r - 1][c & 255] : 0xFFFF; }
static void ts_quita(u32 c)
{
    u32 r = ts_raiz[c >> 8];
    if (!r || ts_hoja[r - 1][c & 255] == 0xFFFF) return;
    ts_hoja[r - 1][c & 255] = 0xFFFF;
    if (--ts_n[r - 1] == 0) { ts_raiz[c >> 8] = 0; ts_libres++; }
}
static int ts_pon(u32 c, u16 h)
{
    u32 r = ts_raiz[c >> 8], k;
    if (!r) {
        if (!ts_libres) { vid_sin_hoja++; return -1; }
        for (r = 0; r < NHOJA && ts_n[r]; r++);
        for (k = 0; k < 256; k++) ts_hoja[r][k] = 0xFFFF;
        ts_libres--; ts_raiz[c >> 8] = (u8)(r + 1); r++;
    }
    ts_hoja[r - 1][c & 255] = h; ts_n[r - 1]++;
    return 0;
}
static u16 hcode[NHUECO], hstamp[NHUECO]; /* por hueco: codigo (0xFFFF libre) y ultimo cuadro en que se vio */
static u8 hsec[NHUECO]; static u16 hsub[NHUECO];   /* seccion de MENUS.PAK de la que vino (0xFF: suelto) y sector en ella */
static u16 carga_ini; static int en_carga; /* desde que cuadro protege la carga en curso lo que sube */
static int hand;                          /* aguja del reloj de victimas */
static u16 now;                           /* cuadro actual (vid_menu) */
static u16 pal_sh[64][32];                /* sombra de la paleta del juego */
static u16 pal_c[6][352] __attribute__((aligned(4)));   /* las 6 filas de S0 ya convertidas (11 paletas de 32 por fila) */
static u16 efe[128] __attribute__((aligned(4)));         /* fila 246: RESTA (0-15), SUMA (16-31), RESTA32 (32-63), SUMA32 (64-95) */
static u8 tbuf[2][256] __attribute__((aligned(4)));      /* tile de 8 bpp expandido (doble: la DMA lee el anterior) */
static int tb;
static u32 te1[NPAG], clid[64], fuv[4][4];
u32 vid_faltas, vid_faltas_cuadro, vid_desalojos, vid_lineas, vid_vict_it, vid_sin_sitio;

/* ---- paletas: entrada 0 siempre 0 (transparente), negro -> 0x8000 (opaco) */
static u16 c15(u32 v)
{
    u32 c = ((v >> 10) & 31) | (v & 0x3E0) | ((v & 31) << 10);
    return (u16)(c ? c : 0x8000);
}
static void pal_conv(int c)
{
    const u16 *s = (const u16 *)(rc_pal + c * 64); u16 *d = &pal_c[c / 11][PAL_X(c)]; int k;
    for (k = 0; k < 16; k++) ((u32 *)pal_sh[c])[k] = ((const u32 *)s)[k];
    d[0] = 0;
    for (k = 1; k < 32; k++) d[k] = c15(s[k]);
}
void vid_paletas(void)
{
    int c, k, n = 0, f[6] = {0, 0, 0, 0, 0, 0};
    for (c = 0; c < 64; c++) {
        const u32 *s = (const u32 *)(rc_pal + c * 64), *h = (const u32 *)pal_sh[c];
        for (k = 0; k < 16; k++) if (s[k] != h[k]) break;
        if (k == 16) continue;
        pal_conv(c); f[c / 11] = 1; n++;
    }
    if (!n) return;
    if (n <= 6) {                        /* pocas: fila a fila */
        for (k = 0; k < 6; k++) if (f[k]) upload(0, 240 + k, 352, 1, pal_c[k]);
    } else upload(0, 240, 352, 6, pal_c);   /* fundido: las 6 filas de una vez (1 056 palabras) */
}
void vid_init(void)
{
    int c, k, p;
    ts_libres = NHOJA;
    for (c = 0; c < NHUECO; c++) { hcode[c] = 0xFFFF; hstamp[c] = 0xFFFF; hsec[c] = 0xFF; }
    for (c = 0; c < 64; c++) { pal_conv(c); clid[c] = CLUT_ID(PAL_X(c), PAL_Y(c)); }
    upload(0, 240, 352, 6, pal_c);
    /* celdas de efecto (diseno 4.3): pens 1-7 restan p/31 (RESTA), 8 suma 8/31, 9-15 suman (16 - p)/31 (SUMA); el
       texel 0 no se dibuja; las de 16 (B) por si acaso con la misma regla sobre 4 bits */
    for (k = 0; k < 128; k++) efe[k] = 0;
    for (p = 1; p < 8; p++) { efe[p] = efe[32 + p] = (u16)(0x8000 | p * 0x0421); }
    efe[16 + 8] = efe[64 + 8] = (u16)(0x8000 | 8 * 0x0421);
    for (p = 9; p < 16; p++) { efe[16 + p] = efe[64 + p] = (u16)(0x8000 | (16 - p) * 0x0421); }
    upload(0, 246, 128, 1, efe);
    for (p = 0; p < NPAG; p++) te1[p] = E1_PAG(p);
    /* U/V de los cuadrilateros con espejo: de 31 a 15 dentro de la ventana de textura del tile (main.c del WR1) */
    for (p = 0; p < 4; p++) {
        u32 uL = (p & 1) ? 31 : 0, uR = (p & 1) ? 15 : 16, vT = (p & 2) ? 31 : 0, vB = (p & 2) ? 15 : 16;
        fuv[p][0] = (vT << 8) | uL; fuv[p][1] = (vT << 8) | uR; fuv[p][2] = (vB << 8) | uL; fuv[p][3] = (vB << 8) | uR;
    }
    dma_wait();
}

/* ---- huecos */
int vid_hueco(u32 codigo) { u16 h = ts_get(codigo & 0x7FFF); return h == 0xFFFF ? -1 : h; }
static int sec_cargando = -1;             /* la seccion que se esta cargando no se desaloja a si misma */
void vid_carga_ini(int sec) { carga_ini = now; en_carga = 1; sec_cargando = sec; }
void vid_carga_fin(void) { en_carga = 0; sec_cargando = -1; }
static u32 prot[2]; static int agotado;   /* secciones del modo actual: se desalojan las ultimas (una pasada entera cuando no queda otra cosa) */
void vid_sec_prot(u32 lo, u32 hi) { prot[0] = lo; prot[1] = hi; agotado = 0; }
/* victima: el primer hueco libre o no visto en este cuadro a partir de la aguja (nunca uno visible, ni uno subido
   por la carga en curso: si no, una seccion grande se desaloja a si misma). Primero los huecos de otros modos y los
   sueltos viejos (> 10 s sin verse); si no queda ninguno, cualquiera */
static int victima(void)
{
    int k, h, paso;
    for (paso = agotado; paso < 2; paso++) {
        for (k = 0; k < NHUECO; k++) {
            h = hand; hand = hand + 1 < NHUECO ? hand + 1 : 0; vid_vict_it++;
            if (hcode[h] == 0xFFFF) return h;
            if (hstamp[h] == now) continue;
            if (en_carga && (u16)(hstamp[h] - carga_ini) <= (u16)(now - carga_ini)) continue;
            if (hsec[h] == sec_cargando) continue;
            if (!paso) {
                if (hsec[h] == 0xFF) { if ((u16)(now - hstamp[h]) < 600) continue; }
                else if (prot[hsec[h] >> 5] & (1u << (hsec[h] & 31))) continue;
            }
            ts_quita(hcode[h]); vid_desalojos++;
            if (hsec[h] != 0xFF) carga_baja(hsec[h], hsub[h]);
            return h;
        }
        agotado = 1;
    }
    vid_sin_sitio++;
    return -1;
}
/* registro A (formatos.md 2.1): nibbles bajos en 2-129 (par en el nibble bajo), bit 4 en 130-161 (el de la izquierda
   en el bit alto) -> 256 bytes de pen, y a la VRAM por GP0 A0h */
int vid_sube_a(const u8 *reg, int sec, int sub)
{
    u32 c = reg[0] | (u32)reg[1] << 8; int h, y, x; u8 *d;
    if (c >= 32768) return -1;
    h = ts_get(c);
    if (h != 0xFFFF) { hstamp[h] = now; if (sec != 0xFF) { hsec[h] = (u8)sec; hsub[h] = (u16)sub; } return h; }
    if ((h = victima()) < 0) return -1;
    if (ts_pon(c, (u16)h) < 0) { if (hcode[h] != 0xFFFF) { hcode[h] = 0xFFFF; } return -1; }   /* sin hoja: el hueco queda libre */
    hcode[h] = (u16)c; hstamp[h] = now; hsec[h] = (u8)sec; hsub[h] = (u16)sub;
    dma_wait();                               /* el otro bufer puede estar en la DMA anterior */
    d = tbuf[tb ^= 1];
    for (y = 0; y < 16; y++) {
        const u8 *n = reg + 2 + y * 8; u32 b = (u32)reg[130 + y * 2] << 8 | reg[131 + y * 2];
        for (x = 0; x < 8; x++) {
            u32 v = n[x];
            d[y * 16 + x * 2] = (u8)((v & 15) | ((b >> (14 - x * 2)) & 2) << 3);
            d[y * 16 + x * 2 + 1] = (u8)((v >> 4) | ((b >> (14 - x * 2)) & 1) << 4);
        }
    }
    upload(PAG_X(h >> 8) + 8 * (h & 15), PAG_Y(h >> 8) + 16 * ((h >> 4) & 15), 8, 16, d);
    return h;
}

/* ---- lista: cabecera del cuadro */
static u32 cur_e1; static int win;
void vid_marco(int fb)
{
    u32 y = fb ? 256u : 0u;
    l_room(12);
    lp[0] = 0xE3000000 | (y << 10); lp[1] = 0xE4000000 | ((y + H - 1) << 10) | (W - 1); lp[2] = 0xE5000000 | (y << 11);
    lp[3] = 0x02000000 | rgb24(0); lp[4] = y << 16; lp[5] = (H << 16) | W;
    /* una primitiva con otra CLUT (RESTA32) y un texel transparente (la entrada 0 de la paleta 0 en S0, vista como
       pagina de 8 bpp en x 0): la GPU guarda la ultima CLUT y no la relee si la paleta cambio en la VRAM */
    lp[6] = 0xE2000000; lp[7] = 0xE1000480; lp[8] = 0x7C000000; lp[9] = 0;
    lp[10] = (CL_RESTA32 << 16) | (240 << 8); lp[11] = (1 << 16) | 1; lp += 12;
    cur_e1 = 0; win = 0;
}
/* un tile 16 x 16 del hueco h en (x, y) con la CLUT cl: sin espejo, rectangulo 0x7D (+ E1 si cambia de pagina o de
   modo); con espejo (fl: bit 0 X, bit 1 Y), cuadrilatero 0x2D con la ventana de textura (E2) ajustada al tile, para
   que U/V puedan ir de 31 a 15 sin salirse (las GPU antiguas no hacen espejo en los rectangulos). semi: modo de
   semitransparencia (0 opaco, 1 B+F, 2 B-F) */
static void emit_a(int x, int y, u32 h, u32 cl, u32 fl, u32 semi)
{
    u32 e1 = te1[h >> 8] | (semi << 5), uv = ((h >> 4) & 15) << 12 | (h & 15) << 4;
    u32 x0 = (u32)x & 0xFFFF, y0 = ((u32)y & 0xFFFF) << 16;
    if (l_full(12)) return;
    if (!fl) {
        l_room(5);
        if (win) { *lp++ = 0xE2000000; win = 0; }
        if (e1 != cur_e1) { *lp++ = e1; cur_e1 = e1; }
        lp[0] = semi ? 0x7F000000 : 0x7D000000; lp[1] = y0 | x0; lp[2] = (cl << 16) | uv; lp += 3;
    } else {
        const u32 *f = fuv[fl]; u32 x1 = (x0 + 16) & 0xFFFF, y1 = y0 + (16 << 16);
        l_room(11);
        if (e1 != cur_e1) { *lp++ = e1; cur_e1 = e1; }
        lp[0] = 0xE20003DE | ((uv & 0xF0) << 7) | ((uv & 0xF000) << 4);
        lp[1] = semi ? 0x2F808080 : 0x2D808080;
        lp[2] = y0 | x0; lp[3] = (cl << 16) | f[0];
        lp[4] = y0 | x1; lp[5] = ((e1 & 0xFFFF) << 16) | f[1];
        lp[6] = y1 | x0; lp[7] = f[2];
        lp[8] = y1 | x1; lp[9] = f[3];
        lp += 10; win = 1;
    }
}
/* falta: cuadrado del color proxy del mosaico (main.c) y peticion al cargador. En el negro de una transicion
   (FEC298 = 0) la paleta esta apagada y el plano trae lo de antes o lo de despues: no cuenta ni se pide */
static int negro;
static void falta(int x, int y, u32 col, u32 c)
{
    rect16(x, y, rgb24(col * 32 + 1 + (c % 15)));
    if (negro) return;
    vid_faltas++; vid_faltas_cuadro++; carga_falta(c);
}

/* ---- copia S(N) de los sprites: entradas activas de los dos bancos (bit 15 de la palabra 0 a 0) con sus celdas */
#define SPN 256
#define SCN 1024
typedef struct { u16 d0, d1, d2, ci; } spr_t;
static spr_t sent[2][SPN]; static int nsent[2];
static u16 scel[2][SCN];
void vid_spr_copia(void)
{
    const u16 *v = (const u16 *)rc_vram; int b;
    for (b = 0; b < 2; b++) {
        u32 j, nc = 0, ne = 0;
        for (j = b * 0x800; j < (u32)b * 0x800 + 0x800; j += 4) {
            u32 d0 = v[j], d1 = v[j + 1], d2 = v[j + 2], d3 = v[j + 3], n, k;
            if ((d0 & 0x8000) || !(d1 & 0x200)) continue;
            n = (((d2 >> 12) & 15) + 1) * (((d1 >> 12) & 15) + 1);
            if (ne >= SPN || nc + n > SCN) break;
            sent[b][ne].d0 = (u16)d0; sent[b][ne].d1 = (u16)d1; sent[b][ne].d2 = (u16)d2; sent[b][ne].ci = (u16)nc;
            for (k = 0; k < n; k++) scel[b][nc + k] = v[((d3 >> 1) + k) & 0x7FFF];
            nc += n; ne++;
        }
        nsent[b] = (int)ne;
    }
}

/* ---- el cuadro de un menu */
void vid_menu(void)
{
    const u16 *vr = (const u16 *)rc_vregs, *v = (const u16 *)rc_vram;
    const u16 *m = v + ((((vr[2] >> 9) & 7) * 0x2000) >> 1);
    int sx = (v[0x2802 >> 1] + 0x14 + 8) & 0x3FF, sy = (v[0x2800 >> 1] + 1 + 16) & 0x1FF, i, j, n = 0, b;   /* +16: la pantalla son las lineas 16-255 de MAME */
    int ox = sx & 15, oy = sy & 15;
    u32 ent[384]; u16 ord[384]; int cnt[65];
    u16 t0 = T1;
    now++; vid_faltas_cuadro = 0; negro = *(const u16 *)(rc_ram + 0xC298) == 0;
    /* plano 0: hueco | espejo << 12 | posicion << 14 | paleta << 23, y luego por paleta (cuenta y coloca) */
    for (i = 0; i < 65; i++) cnt[i] = 0;
    for (j = 0; j < 16; j++)
        for (i = 0; i < 24; i++) {
            int e = ((((sy >> 4) + j) & 31) * 64 + (((sx >> 4) + i) & 63)) * 2;
            u32 w0 = m[e], c = m[e + 1], col = (w0 >> 9) & 0x3F, h;
            if (!c) continue;                             /* el tile 0 esta vacio */
            h = ts_get(c & 0x7FFF);
            if (h == 0xFFFF) { falta(i * 16 - ox, j * 16 - oy, col, c); continue; }
            hstamp[h] = now;
            ent[n++] = h | ((w0 >> 7) & 1) << 12 | ((w0 >> 5) & 2) << 12 | (u32)(j * 24 + i) << 14 | col << 23;
            cnt[col + 1]++;
        }
    for (i = 1; i < 65; i++) cnt[i] += cnt[i - 1];
    for (i = 0; i < n; i++) ord[cnt[ent[i] >> 23]++] = (u16)i;
    for (i = 0; i < n; i++) {
        u32 e = ent[ord[i]], pos = (e >> 14) & 0x1FF;
        emit_a((int)(pos % 24) * 16 - ox, (int)(pos / 24) * 16 - oy, e & 0xFFF, clid[e >> 23], (e >> 12) & 3, 0);
    }
    carga_poll();                                       /* el plano son ~3 ms */
    /* sprites de la copia del vblank anterior con el banco de vregs[1] bit 4 actual, en orden (los de despues tapan) */
    b = (vr[3] & 0x10) ? 1 : 0;
    for (j = 0; j < nsent[b]; j++) {
        const spr_t *s = &sent[b][j]; const u16 *cel = scel[b] + s->ci;
        u32 d0 = s->d0, d1 = s->d1, d2 = s->d2, x, y;
        u32 xs = ((d2 >> 12) & 15) + 1, ys = ((d1 >> 12) & 15) + 1, fx = (d1 >> 11) & 1, fy = (d1 >> 10) & 1, fl = fx | fy << 1;
        for (y = 0; y < ys; y++)
            for (x = 0; x < xs; x++) {
                u32 c = cel[y * xs + x], code = ((((d0 & 0x1FF) << 10) + (c & 0xFFF)) & 0xFFFF), col = (((d0 >> 9) & 0x7F) + (c >> 12)) & 0x7F, h;
                u32 ex = fx ? xs - 1 - x : x, ey = fy ? ys - 1 - y : y;
                int px = (int)(((d2 & 0x3FF) + ex * 16) & 0x3FF) - 127 - 8, py = (int)(((d1 & 0x1FF) + ey * 16) & 0x1FF) - 16;
                if (!code || px <= -16 || px >= W || py <= -16 || py >= H) continue;
                h = ts_get(code & 0x7FFF);
                if ((col & 0x3F) == 0x3F) {               /* celda de efecto: sin proxy */
                    if (h == 0xFFFF) { if (!negro) { vid_faltas++; vid_faltas_cuadro++; carga_falta(code); } continue; }
                    hstamp[h] = now;
                    emit_a(px, py, h, CL_RESTA32, fl, 2); emit_a(px, py, h, CL_SUMA32, fl, 1);
                    continue;
                }
                if (h == 0xFFFF) { falta(px, py, col, code); continue; }
                hstamp[h] = now;
                emit_a(px, py, h, clid[col & 0x3F], fl, 0);
            }
    }
    if (win) { l_room(1); *lp++ = 0xE2000000; win = 0; }
    vid_lineas = (u16)(T1 - t0);
}

/* ---- CARGANDO (letra de 5 x 7 en rectangulos, dos veces mas grande) en el bufer que se ve */
static const u8 glifo[7][7] = {
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},   /* C */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},   /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},   /* R */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},   /* G */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},   /* N */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},   /* D */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};  /* O */
static const u8 texto[8] = {0, 1, 2, 3, 1, 4, 5, 6};
void vid_cargando(int fb)
{
    int i, r, k, x0 = (W - 8 * 14) / 2, y0 = 112;
    u32 y = fb ? 256u : 0u;
    dma_wait();
    l_begin(); l_room(3);
    lp[0] = 0xE3000000 | (y << 10); lp[1] = 0xE4000000 | ((y + H - 1) << 10) | (W - 1); lp[2] = 0xE5000000 | (y << 11); lp += 3;
    rect(x0 - 8, y0 - 8, 8 * 14 + 12, 30, 0x000000);
    for (i = 0; i < 8; i++)
        for (r = 0; r < 7; r++)
            for (k = 0; k < 5; k++)
                if (glifo[texto[i]][r] & (0x10 >> k)) rect(x0 + i * 14 + k * 2, y0 + r * 2, 2, 2, 0xFFFFFF);
    l_send(); dma_wait();
}

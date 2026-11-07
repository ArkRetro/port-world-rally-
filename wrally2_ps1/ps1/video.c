/* Imagen de verdad de los menus (H3, formato A de doc/formatos.md): cada tile es un hueco de 8 x 16 halfwords (texels
   de 8 bits = pen 0-31) en una de las 10 paginas de textura de 8 bpp (bloques b0-b19 por pares); la CLUT es la paleta
   del juego, subida a S0 cuando cambia (fundidos gratis). Plano 0: 24 x 16 posiciones agrupadas por paleta (la GPU
   recarga la CLUT de 256 entradas cada vez que cambia); sprites de la copia del vblank anterior con el banco actual,
   en su orden; celdas de efecto como RESTA (B-F) y SUMA (B+F) con las CLUT fijas de la fila 246. Lo que no esta en la
   VRAM se pide al cargador (carga_falta) y se dibuja mientras como en el mosaico.
   Demo y mapa de la demo (H3, formato B): huecos de 4 x 16 halfwords (4 bpp) en b0-b16, cada uno con su CLUT de 16
   (C0, b17-b19, S0 y S1) hecha de la receta del registro y la paleta viva; los tiles con indice = pen usan la CLUT
   del juego (16 bajas o altas). Tres repartos: A (menus), A con b12-b15 en B (titulo: prelectura de la demo) y B.
   Tambien la lista de la GPU y las primitivas sin textura que usa main.c (cifras, mosaico). Sin libc; comentarios
   sin acentos. */
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
#define LSTN 8448                                 /* 33 KB (el diseno 2.1 daba 48 por el peor cuadro de la placa, 42 KB;
                                                     medido en el port: 27 KB en 12 000 cuadros de attract y Portugal con
                                                     los tres coches; lo que no quepa se pierde ese cuadro) */
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
u32 vid_lst_max;                              /* palabras de la lista del peor cuadro (cifras) */
void l_send(void)
{
    if ((u32)(lp - lst) > vid_lst_max) vid_lst_max = (u32)(lp - lst);
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
static u32 cur_e1; static int win;        /* E1 puesto y ventana de textura puesta (lista del cuadro) */
static int fuente;                        /* la letra de las cifras ya esta en S1 (vid_init) */
/* cifras: con la letra de 4 bpp de S1 (x 0-79, y 496-505; CLUT en (32, 510)) un rectangulo con textura por cifra;
   antes de vid_init, con rectangulos de 2 x 2 */
#define CL_LETRA ((510u << 6) | (32 >> 4))
void digits(int x, int y, u32 v, int n)
{
    int i, k, d[8];
    for (i = 0; i < n; i++) { d[i] = v % 10; v /= 10; }
    rect(x - 2, y - 2, n * 8 + 2, 14, 0x000000);
    if (fuente) {
        if (l_full(2 + 4 * n)) return;
        l_room(2); lp[0] = 0xE2000000; lp[1] = 0xE1000410; lp += 2; cur_e1 = 0xE1000410; win = 0;
        for (i = 0; i < n; i++) {
            l_room(4);
            lp[0] = 0x65000000; lp[1] = ((u32)(y & 0xFFFF) << 16) | ((x + i * 8) & 0xFFFF);
            lp[2] = (CL_LETRA << 16) | (240 << 8) | (u32)(d[n - 1 - i] * 8); lp[3] = (10 << 16) | 8; lp += 4;
        }
        return;
    }
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
#define CL_RESTA16 CLUT_ID(0, 246)
#define CL_SUMA16 CLUT_ID(16, 246)
/* pagina A p (0-9) = bloques 2p y 2p+1: x = 384 + 64 (2p % 10), y = 256 (2p / 10); hueco s: U = 16 (s & 15),
   V = 16 ((s >> 4) & 15), se sube a (x + 8 (s & 15), y + 16 ((s >> 4) & 15)), 8 x 16 halfwords */
#define NPAG 10
#define NHUECO (NPAG * 256)
#define PAG_X(p) (384 + 64 * ((2 * (p)) % 10))
#define PAG_Y(p) (256 * ((2 * (p)) / 10))
/* E1: pagina de 8 bpp (bit 7), dibujar en el area visible (bit 10); los bits 5-6 son el modo de semitransparencia */
#define E1_PAG(p) (0xE1000480u | (u32)(PAG_X(p) >> 6) | (u32)((PAG_Y(p) >> 8) << 4))
/* B: hueco s (0-4 351) en el bloque s >> 8 (pagina de 4 bpp), U/V como en A; se sube a (x + 4 (s & 15), y + 16 (...)) */
#define NB 4352
#define NBCL 4095                         /* huecos con CLUT propia (C0 512 + b17-b19 3 072 + S0 207 + S1 304) */
#define BT_LO 3072                        /* titulo: b12-b15 (paginas A 6 y 7) en B; sus CLUT van en C0, S0 y S1 */
#define BT_HI 4096
#define BLK_X(b) (384 + 64 * ((b) % 10))
#define BLK_Y(b) (256 * ((b) / 10))
#define E1_B(b) (0xE1000400u | (u32)(BLK_X(b) >> 6) | (u32)((BLK_Y(b) >> 8) << 4))

/* codigo -> hueco (0xFFFF: no esta) en dos niveles (diseno 2.1): raiz de 512 grupos de 64 codigos y 264 hojas que se
   dan al vuelo y se devuelven cuando se vacian; si no quedan, el tile no se registra (vid_sin_hoja). Hojas de 64 y no
   de 256 como dice el diseno: los codigos de una demo estan dispersos (DEMO3: 70 grupos de 256 pero 175 de 64) y asi
   ocupan 23 KB en vez de 36. En carrera hacen falta mas: GAMEc + STkk llegan a 244 grupos, mas las emergencias (con
   184 se acababan en todas las carreras: tiles subidos sin registrar, que faltaban y se volvian a pedir sin parar).
   Con la L2 del coche N (choques y vuelcos) el peor tramo da 220 grupos con N y 246 con R (paquetes enteros); medido
   en carrera, 218 como mucho */
#define TSB 6
#define TSM ((1 << TSB) - 1)
#define NHOJA 264
static u16 ts_raiz[32768 >> TSB];          /* hoja + 1 del grupo (0: ninguna) */
static u16 ts_hoja[NHOJA][1 << TSB]; static u8 ts_n[NHOJA];   /* huecos del grupo y cuantos codigos tiene */
static int ts_libres, vp_n;                     /* vp_n: victimas en la reserva (b_barre) */
u32 vid_sin_hoja, vid_hojas_max;                 /* tiles sin registrar por falta de hoja; hojas usadas como mucho */
static inline u16 ts_get(u32 c) { u32 r = ts_raiz[c >> TSB]; return r ? ts_hoja[r - 1][c & TSM] : 0xFFFF; }
static void ts_quita(u32 c)
{
    u32 r = ts_raiz[c >> TSB];
    if (!r || ts_hoja[r - 1][c & TSM] == 0xFFFF) return;
    ts_hoja[r - 1][c & TSM] = 0xFFFF;
    if (--ts_n[r - 1] == 0) { ts_raiz[c >> TSB] = 0; ts_libres++; }
}
static void ts_vacia(void)
{
    int k;
    for (k = 0; k < (32768 >> TSB); k++) ts_raiz[k] = 0;
    for (k = 0; k < NHOJA; k++) ts_n[k] = 0;
    ts_libres = NHOJA; vp_n = 0;               /* la reserva de victimas (b_barre) tampoco vale */
}
static int ts_pon(u32 c, u16 h)
{
    u32 r = ts_raiz[c >> TSB], k;
    if (!r) {
        if (!ts_libres) { vid_sin_hoja++; return -1; }
        for (r = 0; r < NHOJA && ts_n[r]; r++);
        for (k = 0; k <= TSM; k++) ts_hoja[r][k] = 0xFFFF;
        ts_libres--; ts_raiz[c >> TSB] = (u16)(r + 1); r++;
        if ((u32)(NHOJA - ts_libres) > vid_hojas_max) vid_hojas_max = (u32)(NHOJA - ts_libres);
    }
    ts_hoja[r - 1][c & TSM] = h; ts_n[r - 1]++;
    return 0;
}
/* por hueco (A 0-2 559, B 0-4 351; en el titulo A 0-1 535 y 2 048-2 559 con B 3 072-4 095, sin solaparse): codigo
   (0xFFFF libre) y ultimo cuadro en que se vio; A: seccion de MENUS.PAK de la que vino (0xFF: suelto) y sector en
   ella (255: de mas alla); B: paleta de la CLUT, muerte (ultimo avance FEFD08 + margen, en pasos de 4 redondeando
   hacia arriba; 0xFF nunca) y banderas. 7 bytes por hueco (el diseno 2.1 da 8) */
static u16 hcode[NB], hstamp[NB];
static u8 hmu[NB], hpal[NB], hfl[NB];
#define hsec hpal
#define hsub hmu
static u16 carga_ini; static int en_carga; /* desde que cuadro protege la carga en curso lo que sube */
static int hand;                          /* aguja del reloj de victimas */
static u16 now;                           /* cuadro actual (vid_menu) */
static u16 pal_c[6][352] __attribute__((aligned(4)));   /* las 6 filas de S0 ya convertidas (11 paletas de 32 por fila) */
static u16 efe[128] __attribute__((aligned(4)));         /* fila 246: RESTA (0-15), SUMA (16-31), RESTA32 (32-63), SUMA32 (64-95) */
static u8 tbuf[2][256] __attribute__((aligned(4)));      /* tile de 8 bpp expandido (doble: la DMA lee el anterior) */
static int tb;
static u32 te1[NPAG], te1b[17], clid[64], fuv[4][4];
static int lay;                           /* reparto: 0 A, 1 A con b12-b15 en B (titulo), 2 B */
int vid_cifras;
u32 vid_faltas_spr, vid_falta_spr_ult, vid_falta_ult, vid_a_en_b, vid_faltas, vid_faltas_cuadro, vid_desalojos, vid_lineas, vid_vict_it, vid_sin_sitio;

/* ---- paletas: entrada 0 siempre 0 (transparente), negro -> 0x8000 (opaco) */
static u16 c15(u32 v)
{
    u32 c = ((v >> 10) & 31) | (v & 0x3E0) | ((v & 31) << 10);
    return (u16)(c ? c : 0x8000);
}
static u32 pal_firma[64];                 /* firma de cada paleta (pal_sig) al convertirla */
/* firma de las 32 entradas de la paleta c (16 palabras): cada cuadro se mira si cambio con la mitad de lecturas que
   comparandola con su copia (la RAM es lo que cuesta: ~6 ciclos por lectura). Rotar y sumar: un cambio que deje la
   misma firma tendria que coincidir en 32 bits */
static inline u32 pal_sig(int c)
{
    const u32 *s = (const u32 *)(rc_pal + c * 64); u32 h = 0x9E3779B9u; int k;
    for (k = 0; k < 16; k++) h = ((h << 5) | (h >> 27)) + (s[k] ^ (u32)k);
    return h;
}
static void pal_conv(int c)
{
    const u16 *s = (const u16 *)(rc_pal + c * 64); u16 *d = &pal_c[c / 11][PAL_X(c)]; int k;
    d[0] = 0;
    for (k = 1; k < 32; k++) d[k] = c15(s[k]);
    pal_firma[c] = pal_sig(c);
}
static u16 pal_t[64];                     /* B: cuadro en que cambio cada paleta (las CLUT dibujadas antes se rehacen) */
void vid_paletas(void)
{
    int c, k, n = 0, f[6] = {0, 0, 0, 0, 0, 0};
    for (c = 0; c < 64; c++) {
        u32 g = pal_sig(c);
        if (g == pal_firma[c]) continue;
        pal_conv(c); f[c / 11] = 1; n++; pal_t[c] = (u16)(now + 1);   /* now + 1: el cuadro que se va a dibujar */
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
    for (c = 0; c < NB; c++) { hcode[c] = 0xFFFF; hstamp[c] = 0xFFFF; hsec[c] = 0xFF; hfl[c] = 0; }
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
    for (p = 0; p < 17; p++) te1b[p] = E1_B(p);
    {   /* letra de las cifras: 10 cifras de 6 x 10 texels (celdas de 2 x 2) cada 8 texels, texel 1 = blanco */
        static u16 lt[10 * 20] __attribute__((aligned(4))), cl[16] __attribute__((aligned(4)));
        int d, r, x;
        for (k = 0; k < 200; k++) lt[k] = 0;
        for (d = 0; d < 10; d++) for (r = 0; r < 10; r++) for (x = 0; x < 6; x++)
            if (font[d] & (1 << (14 - ((r >> 1) * 3 + (x >> 1))))) lt[r * 20 + d * 2 + (x >> 2)] |= (u16)(1 << ((x & 3) * 4));
        for (k = 0; k < 16; k++) cl[k] = 0;
        cl[1] = 0x7FFF;
        upload(0, 496, 20, 10, lt); upload(32, 510, 16, 1, cl); dma_wait(); fuente = 1;
    }
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
static int evita67;                       /* la seccion del titulo no va a las paginas 6 y 7 (alli va la prelectura B) */
/* now avanza: lo dibujado en el cuadro anterior (un negro de transicion pinta aun la pantalla vieja) no cuenta como
   visible mientras dura la carga */
void vid_carga_ini(int sec, int titulo) { carga_ini = ++now; en_carga = 1; sec_cargando = sec; evita67 = titulo; }
void vid_carga_fin(void) { en_carga = 0; sec_cargando = -1; evita67 = 0; }
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
            if ((lay || evita67) && h >= 1536 && h < 2048) continue;
            if (hcode[h] == 0xFFFF) return h;
            if (hstamp[h] == now) continue;
            if (en_carga && (u16)(hstamp[h] - carga_ini) <= (u16)(now - carga_ini)) continue;
            if (hsec[h] == sec_cargando) continue;
            if (!paso) {
                if (hsec[h] == 0xFF) { if ((u16)(now - hstamp[h]) < 600) continue; }
                else if (prot[hsec[h] >> 5] & (1u << (hsec[h] & 31))) continue;
            }
            ts_quita(hcode[h]); vid_desalojos++;
            if (hsec[h] != 0xFF && hsub[h] != 0xFF) carga_baja(hsec[h], hsub[h]);
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
    if (lay == 2) { vid_a_en_b++; return -1; }   /* en el reparto B no hay paginas A (no deberia pasar) */
    h = ts_get(c);
    if (h != 0xFFFF) { hstamp[h] = now; if (sec != 0xFF) { hsec[h] = (u8)sec; hsub[h] = (u8)(sub < 255 ? sub : 255); } return h; }
    if ((h = victima()) < 0) return -1;
    if (ts_pon(c, (u16)h) < 0) { if (hcode[h] != 0xFFFF) { hcode[h] = 0xFFFF; } return -1; }   /* sin hoja: el hueco queda libre */
    hcode[h] = (u16)c; hstamp[h] = now; hsec[h] = (u8)sec; hsub[h] = (u8)(sub < 255 ? sub : 255);
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
   semitransparencia (0 opaco, 1 B+F, 2 B-F). e1: pagina (A de 8 bpp o B de 4 bpp) */
/* estado de la lista (lp, node, cur_e1, win) en una copia local: en el bucle de vid_b va en registros (las globales
   costarian una lectura de la RAM cada vez); se pasa a las globales antes de lo que dibuje por su cuenta (rect16) */
typedef struct { u32 *lp, *node, e1; int win; } lst_t;
#define LST_IN(s) ((s).lp = lp, (s).node = node, (s).e1 = cur_e1, (s).win = win)
#define LST_OUT(s) (lp = (s).lp, node = (s).node, cur_e1 = (s).e1, win = (s).win)
static inline __attribute__((always_inline)) void emit_tl(lst_t *s, int x, int y, u32 e1, u32 h, u32 cl, u32 fl, u32 semi)
{
    u32 uv = ((h >> 4) & 15) << 12 | (h & 15) << 4;
    u32 x0 = (u32)x & 0xFFFF, y0 = ((u32)y & 0xFFFF) << 16;
    u32 *q = s->lp;
    if (q + 14 >= lst + LSTN) return;         /* lleno (l_full, aqui sin llamada: es el bucle mas caliente) */
    if (!fl) {
        if (q + 4 - s->node > 16) { s->node[0] = ((u32)(q - s->node - 1) << 24) | ((u32)q & 0xFFFFFF); s->node = q; q++; }
        if (s->win) { *q++ = 0xE2000000; s->win = 0; }
        if (e1 != s->e1) { *q++ = e1; s->e1 = e1; }
        q[0] = semi ? 0x7F000000 : 0x7D000000; q[1] = y0 | x0; q[2] = (cl << 16) | uv; q += 3;
    } else {
        const u32 *f = fuv[fl]; u32 x1 = (x0 + 16) & 0xFFFF, y1 = y0 + (16 << 16);
        if (q + 10 - s->node > 16) { s->node[0] = ((u32)(q - s->node - 1) << 24) | ((u32)q & 0xFFFFFF); s->node = q; q++; }
        if (e1 != s->e1) { *q++ = e1; s->e1 = e1; }
        q[0] = 0xE20003DE | ((uv & 0xF0) << 7) | ((uv & 0xF000) << 4);
        q[1] = semi ? 0x2F808080 : 0x2D808080;
        q[2] = y0 | x0; q[3] = (cl << 16) | f[0];
        q[4] = y0 | x1; q[5] = ((e1 & 0xFFFF) << 16) | f[1];
        q[6] = y1 | x0; q[7] = f[2];
        q[8] = y1 | x1; q[9] = f[3];
        q += 10; s->win = 1;
    }
    s->lp = q;
}
static inline __attribute__((always_inline)) void emit_t(int x, int y, u32 e1, u32 h, u32 cl, u32 fl, u32 semi)
{
    lst_t s; LST_IN(s); emit_tl(&s, x, y, e1, h, cl, fl, semi); LST_OUT(s);
}
static void __attribute__((noinline)) emit_af(int x, int y, u32 h, u32 cl, u32 fl, u32 semi)
{   /* menus (reparto A): una sola copia de emit_t (en linea solo en el reparto B, el de la carrera) */
    emit_t(x, y, te1[h >> 8] | (semi << 5), h, cl, fl, semi);
}
#define emit_a(x, y, h, cl, fl, semi) emit_af(x, y, h, cl, fl, semi)
#define emit_b(x, y, h, cl, fl, semi) emit_tl(&ls, x, y, te1b[(h) >> 8] | ((semi) << 5), h, cl, fl, semi)   /* vid_b: con ls */
/* falta: cuadrado del color proxy del mosaico (main.c) y peticion al cargador. En el negro de una transicion
   (FEC298 = 0) la paleta esta apagada y el plano trae lo de antes o lo de despues: no cuenta ni se pide */
static int negro;
static void falta(int x, int y, u32 col, u32 c)
{
    rect16(x, y, rgb24(col * 32 + 1 + (c % 15)));
    if (negro) return;
    vid_faltas++; vid_faltas_cuadro++; vid_falta_ult = c; carga_falta(c);
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
            u32 d01 = *(const u32 *)(v + j), d0 = d01 & 0xFFFF, d1 = d01 >> 16, d2, d3, n, k;
            if ((d0 & 0x8000) || !(d1 & 0x200)) continue;   /* casi todas: solo la primera lectura */
            __asm__ volatile ("" ::: "memory");         /* que el compilador no adelante estas dos a antes del salto */
            d2 = v[j + 2]; d3 = v[j + 3];
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
    static u32 ent[384]; static u16 ord[384]; static int cnt[65];   /* fuera de la pila (1 KB en el scratchpad) */
    u16 t0 = T1;
    now++; vid_faltas_cuadro = 0; negro = *(const u16 *)(rc_ram + 0xC298) == 0;
    /* plano 0: hueco | espejo << 12 | posicion << 14 | paleta << 23, y luego por paleta (cuenta y coloca) */
    for (i = 0; i < 65; i++) cnt[i] = 0;
    for (j = 0; j < 16; j++) {
        if ((j & 3) == 3) carga_poll();                   /* el CD cada ~1 ms (un sector a 2x son 6,7: en la PS1 de
                                                             verdad, si no se recoge a tiempo, el siguiente lo pisa) */
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
    }
    for (i = 1; i < 65; i++) cnt[i] += cnt[i - 1];
    for (i = 0; i < n; i++) ord[cnt[ent[i] >> 23]++] = (u16)i;
    for (i = 0; i < n; i++) {
        u32 e = ent[ord[i]], pos = (e >> 14) & 0x1FF;
        if ((i & 63) == 63) carga_poll();
        emit_a((int)(pos % 24) * 16 - ox, (int)(pos / 24) * 16 - oy, e & 0xFFF, clid[e >> 23], (e >> 12) & 3, 0);
    }
    carga_poll();                                       /* el plano son ~3 ms */
    /* sprites de la copia del vblank anterior con el banco de vregs[1] bit 4 actual, en orden (los de despues tapan) */
    b = (vr[3] & 0x10) ? 1 : 0;
    for (j = 0; j < nsent[b]; j++) {
        const spr_t *s = &sent[b][j]; const u16 *cel = scel[b] + s->ci;
        if ((j & 7) == 7) carga_poll();
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

/* ------------------------------------------------------------ formato B (doc/formatos.md 2.2 y 4) */
#define F_NUEVO 1                         /* VO rotativo aun sin dibujar: no se desaloja (se subio porque hara falta) */
#define F_EFE 2                           /* banderas del registro (byte 141) en los bits 1-3 (el 0, sprite, no sirve aqui) */
#define F_IDX 4
#define F_ALTA 8
#define F_FIJO 0x10                       /* VO fijo: no se desaloja mientras viva (avance <= muerte) */
#define F_L2 0x20                         /* vino de la L2: en hrec el indice (la receta esta en la RAM) */
#define F_SUCIA 0x40                      /* la CLUT hay que rehacerla antes de dibujar */
#define F_MAPA 0x80                       /* del mapa: se quita en el negro antes de la demo */
#define PROPIA(f) (!((f) & (F_IDX | F_ALTA)))
#define MAXL2 48                          /* subidas de la L2 por cuadro (lo demas, cuadrado y al cuadro siguiente) */
#define MAXL2S 64                         /* y aparte para los sprites: el plano va antes y en las curvas gastaba las 48, y
                                             el coche, que cambia de pose al girar, salia con cuadrados */
static int l2_tope;                       /* hasta cuantas subidas van en este cuadro (plano: MAXL2; sprites: MAXL2S mas) */
static u8 *hrec;                          /* 10 bytes por hueco en el area de tramo: receta o indice de la L2 */
static const u8 *l2base;                  /* sectores de la L2 en el area (14 registros de 144 B por sector) */
static int ba_lo, ba_hi, bhand, nl2;      /* agujas de huecos libres (con CLUT desde abajo, sin ella desde arriba) */
static u16 cbuf[2][16] __attribute__((aligned(4))); static int cbi;
u32 vid_b_l2, vid_b_clut, vid_b_otrapal, vid_b_muertos, vid_b_sinsitio, vid_b_efemal, vid_b_rot, vid_b_fijos, vid_b_maxl2;
int vid_lay(void) { return lay; }
void vid_area(u8 *rec, const u8 *l2) { hrec = rec; l2base = l2; }
static inline const u8 *l2reg(u32 j) { return l2base + (j / 14) * 2048 + 32 + (j % 14) * 144; }
static inline u32 l2idx(int h) { return hrec[h * 10] | (u32)hrec[h * 10 + 1] << 8; }
static const u8 *receta(int h) { return (hfl[h] & F_L2) ? l2reg(l2idx(h)) + 130 : hrec + h * 10; }
/* CLUT del hueco h (< 4 095): b17-b19 (4 por fila), C0, filas 247-255 de S0 y S1 desde x 64 */
static inline __attribute__((always_inline)) void cl_pos(int h, int *x, int *y)
{
    if (h < 3072) { *x = BLK_X(17) + 64 * (h >> 10) + 16 * (h & 3); *y = BLK_Y(17) + ((h >> 2) & 255); }   /* b17-b19 en la misma fila: sin dividir */
    else if (h < 3584) { *x = 368; *y = h - 3072; }
    else if (h < 3791) { int i = h - 3584; *x = 16 * (i % 23); *y = 247 + i / 23; }
    else { int i = h - 3791; *x = 64 + 16 * (i % 19); *y = 496 + i / 19; }
}
/* CLUT = [0, pal[c * 32 + receta[1]], ...] ya convertida (pal_c, al dia por vid_paletas) */
static void b_clut(int h)
{
    const u8 *r = receta(h); const u16 *p = &pal_c[hpal[h] / 11][PAL_X(hpal[h])]; u16 *d = cbuf[cbi ^= 1];
    u32 k; int x, y;
    d[0] = 0;
    for (k = 1; k < 16; k++) { u32 b = (k - 1) * 5; d[k] = p[((r[b >> 3] | (u32)r[(b >> 3) + 1] << 8) >> (b & 7)) & 31]; }
    cl_pos(h, &x, &y); upload(x, y, 16, 1, d); vid_b_clut++;
}
/* ts para B: hueco (< 4 352), 0x8000 | indice de la L2 (en la RAM) o 0xFFFF */
static void ts_set(u32 c, u16 v)
{
    u32 r = ts_raiz[c >> TSB];
    if (v == 0xFFFF) { ts_quita(c); return; }
    if (r && ts_hoja[r - 1][c & TSM] != 0xFFFF) { ts_hoja[r - 1][c & TSM] = v; return; }
    ts_pon(c, v);
}
static void b_quita(int h)
{
    if (hcode[h] == 0xFFFF) return;
    if (lay == 2) ts_set(hcode[h], (hfl[h] & F_L2) ? (u16)(0x8000 | l2idx(h)) : 0xFFFF);
    hcode[h] = 0xFFFF; hfl[h] = 0;
}
/* hueco libre en el reparto (titulo: b12-b15; B: todos): con CLUT propia por abajo (< 4 095), sin ella por arriba */
static int b_libre(int propia)
{
    int lo = lay == 2 ? 0 : BT_LO, hi = lay == 2 ? NB : BT_HI;
    if (propia) {
        if (ba_lo < lo) ba_lo = lo;
        for (; ba_lo < NBCL && ba_lo < hi; ba_lo++) if (hcode[ba_lo] == 0xFFFF) return ba_lo;
        return -1;
    }
    if (ba_hi >= hi) ba_hi = hi - 1;
    for (; ba_hi >= lo; ba_hi--) if (hcode[ba_hi] == 0xFFFF) return ba_hi;
    return -1;
}
/* victima (solo en el reparto B): libre; muerto (en la demo y la carrera, FEFD08 ya paso su muerte, tambien los fijos);
   VO aun sin dibujar; rotativo no visto en vthr cuadros. Nunca uno dibujado en este cuadro ni un fijo.
   Barrido incremental (reloj): cada cuadro se miran NB/8 huecos y los que valen van a una reserva de VP; la victima
   sale de la reserva (si su sello cambio desde que entro, se vio otra vez y no vale). El umbral de edad se adapta: si
   una vuelta entera llena poco la reserva, baja; si se llena enseguida, sube. Antes cada cuadro con tiles nuevos
   recorria los 4 352 huecos (dos veces): en las curvas, 80 lineas de mas y el cuadro saltado */
#define VP 128
static u16 vpool[VP], vpst[VP]; static int vcur, vthr = 40, v_vuelta;
static void b_barre(int n)
{
    int dem = *(const u16 *)(rc_ram + 0xC298) == 5 || *(const u16 *)(rc_ram + 0xC298) == 1;
    u16 av4 = (u16)(*(const u16 *)(rc_ram + 0xFD08) >> 2);
    while (n-- > 0 && vp_n < VP) {
        int h = vcur; u32 f;
        if (++vcur >= NB) {                    /* una vuelta: el umbral segun lo que dio */
            vcur = 0;
            if (v_vuelta < VP / 2) { if (vthr > 2) vthr >>= 1; } else if (vthr < 56) vthr += 4;
            v_vuelta = 0;
        }
        if (hcode[h] == 0xFFFF) continue;     /* los libres los da b_libre */
        if (hstamp[h] == now) continue;
        f = hfl[h];
        if (!(dem && hmu[h] != 0xFF && av4 > hmu[h])) {      /* muerto: vale aunque sea fijo */
            if (f & F_FIJO) continue;
            if (!(f & F_NUEVO) && (u16)(now - hstamp[h]) < (u16)vthr) continue;
        }
        vpool[vp_n] = (u16)h; vpst[vp_n] = hstamp[h]; vp_n++; v_vuelta++;
    }
}
static int b_victima_exacta(int propia);
static int b_victima(int propia)
{
    int k;
    if (*(const u16 *)(rc_ram + 0xC298) != 1) return b_victima_exacta(propia);
    for (k = 0; k < 9; k++) {                 /* reserva; vacia: otro octavo (como mucho una vuelta y algo) */
        while (vp_n) {
            int h = vpool[--vp_n];
            if (propia && h >= NBCL) continue;
            if (hcode[h] == 0xFFFF) return h;
            if (hstamp[h] != vpst[vp_n] || hstamp[h] == now) continue;   /* se vio otra vez */
            if (hfl[h] & F_FIJO) { u16 av4 = (u16)(*(const u16 *)(rc_ram + 0xFD08) >> 2); if (hmu[h] == 0xFF || av4 <= hmu[h]) continue; vid_b_muertos++; }
            b_quita(h);
            return h;
        }
        b_barre(NB / 8);
    }
    vid_b_sinsitio++;
    return -1;
}
/* fuera de la carrera (la demo del attract): la de siempre, exacta (un recorrido entero guarda las 96 mejores por clase,
   como mucho 2 por cuadro); en la demo la reserva incremental dejaba mas faltas al empezar */
#define NCAND 96
#define NREC 2
static u16 cand[2][NCAND], cand_now[2]; static u8 cand_n[2], cand_i[2], cand_ok[2], cand_rec[2];
static u8 cand_hist[60 + 256];             /* cuentas hasta 255 (basta para pasar de NCAND) */
static int b_victima_exacta(int propia)
{
    int k, h, n = 0, lim = propia ? NBCL : NB, dem = *(const u16 *)(rc_ram + 0xC298) == 5 || *(const u16 *)(rc_ram + 0xC298) == 1;
    u16 av = *(const u16 *)(rc_ram + 0xFD08), age; u32 key, umbral, acu;
    propia = propia != 0;
    if (cand_now[propia] != now) { cand_now[propia] = now; cand_rec[propia] = 0; cand_ok[propia] = 0; }
    if (cand_ok[propia]) {
        while (cand_i[propia] < cand_n[propia]) {
            h = cand[propia][cand_i[propia]++];
            if (hcode[h] == 0xFFFF) return h;
            if (hstamp[h] == now || (hfl[h] & F_FIJO)) continue;
            b_quita(h);
            return h;
        }
        cand_ok[propia] = 0;
        if (cand_rec[propia] >= NREC) { vid_b_sinsitio++; return -1; }   /* hasta el cuadro siguiente sin sitio */
    }
    /* primera vuelta: salidas rapidas como siempre y el histograma de claves (edad 0-59; VO nuevo 60 + muerte) */
    for (k = 0; k < 60 + 256; k++) cand_hist[k] = 0;
    for (k = 0; k < NB; k++) {
        h = bhand; bhand = bhand + 1 < NB ? bhand + 1 : 0;
        if (h >= lim) continue;
        if (hcode[h] == 0xFFFF) return h;
        if (hstamp[h] == now) continue;
        if (dem && hmu[h] != 0xFF && (av >> 2) > hmu[h]) { vid_b_muertos++; b_quita(h); return h; }
        if (hfl[h] & F_FIJO) continue;
        if (hfl[h] & F_NUEVO) key = 60u + hmu[h];
        else {
            age = (u16)(now - hstamp[h]);
            if (age >= 60) { b_quita(h); return h; }
            key = age;
        }
        if (cand_hist[key] < 255) cand_hist[key]++;
    }
    cand_rec[propia]++;
    /* umbral: las NCAND de clave mas alta (VO nuevo que mas tarde muere; si no, el rotativo mas viejo) */
    for (umbral = 60 + 256, acu = 0; umbral > 0 && acu < NCAND; ) acu += cand_hist[--umbral];
    if (!acu) { cand_ok[propia] = 1; cand_n[propia] = 0; cand_i[propia] = 0; vid_b_sinsitio++; return -1; }
    /* segunda vuelta: las de clave por encima del umbral y las del umbral hasta llenar; luego por clave, de mayor a menor */
    {
        static u16 ck[NCAND]; int pas, j;
        for (pas = 0; pas < 2 && n < NCAND; pas++)
            for (k = 0; k < lim && n < NCAND; k++) {
                if (hcode[k] == 0xFFFF || hstamp[k] == now || (hfl[k] & F_FIJO)) continue;
                key = (hfl[k] & F_NUEVO) ? 60u + hmu[k] : (u16)(now - hstamp[k]);
                if (pas ? key != umbral : key <= umbral) continue;
                for (j = n++; j > 0 && ck[j - 1] < key; j--) { ck[j] = ck[j - 1]; cand[propia][j] = cand[propia][j - 1]; }
                ck[j] = (u16)key; cand[propia][j] = (u16)k;
            }
    }
    cand_ok[propia] = 1; cand_n[propia] = (u8)n; cand_i[propia] = 1;
    h = cand[propia][0];
    b_quita(h);
    return h;
}
/* registro B en el hueco h: texels directos del registro (128 B = 4 x 16 halfwords, alineado a 4) por DMA; la CLUT
   se hace al dibujarlo (F_SUCIA). l2 >= 0: indice de la L2 (el registro se queda en la RAM) */
static void b_pon(int h, const u8 *reg, u32 extra, int l2)
{
    u32 c = reg[128] | (u32)reg[129] << 8; u8 *q = hrec + h * 10; int k;
    k = reg[142] | reg[143] << 8;
    hcode[h] = (u16)c; hstamp[h] = (u16)(now - 1); hpal[h] = reg[140] & 63;
    hmu[h] = (u8)((k > 1015 || (!extra && l2 < 0)) ? 0xFF : (k + 3) >> 2);   /* emergencia (TILES.BIN, avance 0): no muere por avance */
    hfl[h] = (u8)((reg[141] & 14) | extra | F_SUCIA);
    if (l2 >= 0) { q[0] = (u8)l2; q[1] = (u8)(l2 >> 8); hfl[h] |= F_L2; }
    else for (k = 0; k < 10; k++) q[k] = reg[130 + k];
    upload(BLK_X(h >> 8) + 4 * (h & 15), BLK_Y(h >> 8) + 16 * ((h >> 4) & 15), 4, 16, reg);
    if (lay == 2) ts_set(c, (u16)h);
}
/* registro B del cargador (mapa, VO, emergencias): extra F_MAPA, F_FIJO o F_NUEVO; rot: puede desalojar (solo en B).
   -1 sin sitio, -2 ya estaba (si es VO fijo, el hueco pasa a fijo) */
int vid_sube_b(const u8 *reg, u32 extra, int rot)
{
    u32 c = reg[128] | (u32)reg[129] << 8; int h, pr = PROPIA(reg[141]);
    if (c >= 32768 || !hrec || !lay) return -1;
    if (lay == 2) {
        u16 v = ts_get(c);
        if (v < NB) {                         /* ya estaba (p. ej. del mapa): si es fijo, pasa a fijo; si es de GAMEc (sin
                                                 muerte), no muere (la del mapa es 0: se daba por muerto en la carrera) */
            if (extra & F_FIJO) { hfl[v] = (u8)((hfl[v] | F_FIJO) & ~F_MAPA); if ((reg[142] | (u32)reg[143] << 8) > 1015) hmu[v] = 0xFF; }
            return -2;
        }
    }
    h = b_libre(pr);
    if (h < 0 && rot && lay == 2) h = b_victima(pr);
    if (h < 0) return -1;
    b_pon(h, reg, extra, -1);
    return h;
}
/* la L2 en la RAM: el codigo apunta al registro (se sube al dibujarlo) */
static u32 l2_reg, l2_pre;                  /* registros de la L2 apuntados; siguiente para la prelectura */
u32 vid_b_pre, vid_b_tope;                    /* subidas por prelectura; tiles de la L2 sin subir por el tope (cifras) */
void vid_pon_l2(const u8 *reg, int j)
{
    u32 c = reg[128] | (u32)reg[129] << 8;
    if (lay == 2 && c < 32768 && ts_get(c) == 0xFFFF) ts_set(c, (u16)(0x8000 | j));
    if ((u32)j + 1 > l2_reg) l2_reg = (u32)j + 1;
}
static int b_l2(u32 j)
{
    const u8 *reg = l2reg(j); int h, pr = PROPIA(reg[141]);
    if (nl2 >= l2_tope) return -1;
    h = b_libre(pr);
    if (h < 0) h = b_victima(pr);
    if (h < 0) return -1;
    nl2++; vid_b_l2++;
    b_pon(h, reg, 0, (int)j);
    return h;
}

/* repaso de la L2: un codigo que estaba en la VRAM al apuntar la L2 (del mapa, de una emergencia) no se apunto; al
   quitarse ese hueco (no es de la L2) el codigo se quedaba sin nada aunque esta en la RAM. n registros por llamada */
static u32 l2_rep;
static void b_reapunta(u32 n)
{
    while (n-- && l2_reg) {
        const u8 *reg; u32 c;
        if (l2_rep >= l2_reg) l2_rep = 0;
        reg = l2reg(l2_rep); c = reg[128] | (u32)reg[129] << 8;
        if (c < 32768 && ts_get(c) == 0xFFFF) ts_set(c, (u16)(0x8000 | l2_rep));
        l2_rep++;
    }
}
/* prelectura por avance (diseno 9: la L2 esta ordenada por primer avance FEFD08, por sectores con el primero en la
   cabecera, bytes 12-15): cada cuadro de la demo o la carrera se suben los registros cuyo sector empieza antes de
   avance + PRE_AV, como mucho PRE_N, a huecos libres o victimas, marcados como nuevos (no se desalojan antes de
   dibujarse). Asi lo que va a salir esta en la VRAM unos cuadros antes y las curvas (scroll en los dos ejes, muchos
   tiles nuevos a la vez) no topan con las MAXL2 subidas por cuadro ni con las emergencias */
#define PRE_AV 8                              /* ~30 cuadros: el avance sube 0,28 por cuadro */
#define PRE_N 4
#ifdef CON_PRE
static void b_prelee(u32 av)
{
    int n = 0;
    while (n < PRE_N && l2_pre < l2_reg) {
        const u8 *s = l2base + (l2_pre / 14) * 2048, *reg;
        u32 a0 = s[12] | (u32)s[13] << 8, c; u16 v; int h;
        if (a0 > av + PRE_AV) break;
        reg = l2reg(l2_pre); c = reg[128] | (u32)reg[129] << 8;
        if (c < 32768 && ((v = ts_get(c)) & 0x8000) && v != 0xFFFF && (v & 0x7FFF) == l2_pre) {   /* aun solo en la L2 */
            int pr = PROPIA(reg[141]);
            h = b_libre(pr);
            if (h < 0) h = b_victima(pr);
            if (h < 0) break;
            b_pon(h, reg, F_NUEVO, (int)l2_pre); vid_b_pre++; n++;
        }
        l2_pre++;
    }
}
#endif
/* ---- repartos */
void vid_lay_a(void)
{   /* vuelta a los menus: la VRAM de los bloques ya no vale para nada; desde el titulo (reparto 1) solo se vacian los
   huecos B de la prelectura (los de A siguen ahi y carga.c los cuenta como cargados) */
    int h;
    if (lay == 1) {
        for (h = BT_LO; h < BT_HI; h++) { hcode[h] = 0xFFFF; hstamp[h] = 0xFFFF; hsec[h] = 0xFF; hfl[h] = 0; }
        lay = 0; return;
    }
    ts_vacia();
    for (h = 0; h < NB; h++) { hcode[h] = 0xFFFF; hstamp[h] = 0xFFFF; hsec[h] = 0xFF; hfl[h] = 0; }
    lay = 0; hand = 0;
}
void vid_lay_titulo(void)
{   /* b12-b15 (paginas A 6 y 7) pasan a B para la prelectura de la demo: lo de A que hubiera alli se desaloja */
    int h;
    for (h = 1536; h < 2048; h++)
        if (hcode[h] != 0xFFFF) {
            ts_quita(hcode[h]); vid_desalojos++;
            if (hsec[h] != 0xFF && hsub[h] != 0xFF) carga_baja(hsec[h], hsub[h]);
            hcode[h] = 0xFFFF; hsec[h] = 0xFF;
        }
    for (h = BT_LO; h < BT_HI; h++) { hcode[h] = 0xFFFF; hfl[h] = 0; }
    lay = 1; ba_lo = BT_LO; ba_hi = BT_HI - 1;
}
/* al reparto B (negro antes del mapa): fuera A; con guarda, se quedan los huecos de la prelectura (sus codigos a la
   tabla; un codigo repetido se queda en el primero) y todas las CLUT se rehacen al dibujar */
void vid_lay_b(int guarda)
{
    int h; u16 v;
    guarda = guarda && lay == 1;
    ts_vacia();
    for (h = 0; h < NB; h++) {
        if (guarda && h >= BT_LO && h < BT_HI && hcode[h] != 0xFFFF) {
            if ((v = ts_get(hcode[h])) == 0xFFFF) { ts_set(hcode[h], (u16)h); hfl[h] |= F_SUCIA; continue; }
            hfl[v] |= hfl[h] & F_FIJO;
        }
        hcode[h] = 0xFFFF; hstamp[h] = 0xFFFF; hfl[h] = 0; hpal[h] = 0;
    }
    lay = 2; ba_lo = 0; ba_hi = NB - 1; bhand = 0; l2_reg = l2_pre = l2_rep = 0;
}
void vid_quita_mapa(void)
{   /* negro antes de la demo: los huecos del mapa quedan libres para el resto del VO */
    int h;
    for (h = 0; h < NB; h++) if (hfl[h] & F_MAPA) b_quita(h);
    ba_lo = 0; ba_hi = NB - 1;
    l2_rep = 0; b_reapunta(l2_reg);           /* lo del mapa que tambien esta en la L2 */
}

/* ---- el cuadro de la demo o del mapa (reparto B) */
/* hueco del codigo: si esta en la L2, se sube ahora (como mucho MAXL2 por cuadro). -1 no esta (emergencia), -2 en la
   L2 pero sin subir (cuadrado y al cuadro siguiente) */
static inline __attribute__((always_inline)) int b_busca(u32 code)
{
    u16 v = ts_get(code & 0x7FFF);
    if (v == 0xFFFF) return -1;
    if (v & 0x8000) { int h = b_l2(v & 0x7FFF); return h < 0 ? -2 : h; }
    return v;
}
/* CLUT con la que se dibuja el hueco h con la paleta col: indice = pen, la del juego (16 bajas o altas); si no, la
   suya: si la paleta no es la de la CLUT y es el primer uso en el cuadro, se rehace con esta; si ya se dibujo con
   otra en este cuadro, se cuenta (vid_b_otrapal) y va con la que tiene. Fundidos: la CLUT esta al dia desde el
   ultimo cuadro en que se dibujo (hstamp); si su paleta cambio despues (pal_t), se rehace (sin recorrer los huecos
   en cada cambio de paleta) */
static inline __attribute__((always_inline)) u32 b_cl(int h, u32 col)
{
    u32 f = hfl[h];
    if (f & F_IDX) return clid[col];
    if (f & F_ALTA) return clid[col] + 1;     /* x + 16: la CLUT siguiente */
    if (hpal[h] != col) { if (hstamp[h] != now) { hpal[h] = (u8)col; f |= F_SUCIA; } else vid_b_otrapal++; }
    if ((s16)(pal_t[hpal[h]] - hstamp[h]) > 0) f |= F_SUCIA;
    if (f & F_SUCIA) { b_clut(h); hfl[h] = (u8)(f & ~F_SUCIA); }
    { int x, y; cl_pos(h, &x, &y); return CLUT_ID(x, y); }
}
void vid_b(void)
{
    const u16 *vr = (const u16 *)rc_vregs, *v = (const u16 *)rc_vram;
    const u16 *m = v + ((((vr[2] >> 9) & 7) * 0x2000) >> 1);
    int sx = (v[0x2802 >> 1] + 0x14 + 8) & 0x3FF, sy = (v[0x2800 >> 1] + 1 + 16) & 0x1FF, i, j, b, h;
    int ox = sx & 15, oy = sy & 15;
    u16 t0 = T1; lst_t ls;
    now++; vid_faltas_cuadro = 0; nl2 = 0; l2_tope = MAXL2; negro = *(const u16 *)(rc_ram + 0xC298) == 0;
    {   /* prelectura en la demo y la carrera */
        u16 md = *(const u16 *)(rc_ram + 0xC298);
        if (md == 1) b_barre(NB / 8);                    /* la reserva de victimas de la carrera, al dia poco a poco */
        if ((md == 5 || md == 1) && l2base) {
            b_reapunta(64);
#ifdef CON_PRE                                  /* medido: menos tiles de un cuadro por el tope, pero mas cuadros lentos */
            b_prelee(*(const u16 *)(rc_ram + 0xFD08));
#endif
        }
    }
    /* plano 0 en orden (cada tile con su CLUT: no hay nada que agrupar) */
    LST_IN(ls);
    for (j = 0; j < 16; j++) {
        if ((j & 3) == 3) carga_poll();
        for (i = 0; i < 24; i++) {
            int e = ((((sy >> 4) + j) & 31) * 64 + (((sx >> 4) + i) & 63)) * 2;
            u32 w = *(const u32 *)(m + e), w0 = w & 0xFFFF, c = w >> 16, col = (w0 >> 9) & 0x3F, cl;   /* una lectura */
            if (!c) continue;
            if ((h = b_busca(c)) < 0) {
                LST_OUT(ls);
                if (h == -2) vid_b_tope++, rect16(i * 16 - ox, j * 16 - oy, rgb24(col * 32 + 1 + (c % 15)));
                else falta(i * 16 - ox, j * 16 - oy, col, c);
                LST_IN(ls);
                continue;
            }
            cl = b_cl(h, col); hstamp[h] = now; hfl[h] &= (u8)~F_NUEVO;
            emit_b(i * 16 - ox, j * 16 - oy, (u32)h, cl, ((w0 >> 7) & 1) | ((w0 >> 5) & 2), 0);
        }
    }
    carga_poll();
    l2_tope = nl2 + MAXL2S;                    /* los sprites (el coche) con su propio cupo */
    b = (vr[3] & 0x10) ? 1 : 0;
    for (j = 0; j < nsent[b]; j++) {
        const spr_t *s = &sent[b][j]; const u16 *cel = scel[b] + s->ci;
        if ((j & 7) == 7) carga_poll();
        u32 d0 = s->d0, d1 = s->d1, d2 = s->d2, x, y;
        u32 xs = ((d2 >> 12) & 15) + 1, ys = ((d1 >> 12) & 15) + 1, fx = (d1 >> 11) & 1, fy = (d1 >> 10) & 1, fl = fx | fy << 1;
        for (y = 0; y < ys; y++)
            for (x = 0; x < xs; x++) {
                u32 c = cel[y * xs + x], code = ((((d0 & 0x1FF) << 10) + (c & 0xFFF)) & 0xFFFF), col = (((d0 >> 9) & 0x7F) + (c >> 12)) & 0x7F;
                u32 ex = fx ? xs - 1 - x : x, ey = fy ? ys - 1 - y : y;
                int px = (int)(((d2 & 0x3FF) + ex * 16) & 0x3FF) - 127 - 8, py = (int)(((d1 & 0x1FF) + ey * 16) & 0x1FF) - 16;
                if (!code || px <= -16 || px >= W || py <= -16 || py >= H) continue;
                h = b_busca(code);
                if ((col & 0x3F) == 0x3F) {               /* celda de efecto: indice = pen con RESTA y SUMA de 16 */
                    if (h == -1 && !negro) { vid_faltas++; vid_faltas_cuadro++; vid_falta_ult = code; carga_falta(code); }
                    if (h < 0) continue;
                    if (!(hfl[h] & F_IDX)) vid_b_efemal++;
                    hstamp[h] = now; hfl[h] &= (u8)~F_NUEVO;
                    emit_b(px, py, (u32)h, CL_RESTA16, fl, 2); emit_b(px, py, (u32)h, CL_SUMA16, fl, 1);
                    continue;
                }
                if (h < 0) {
                    LST_OUT(ls);
                    if (h == -2) vid_b_tope++, rect16(px, py, rgb24(col * 32 + 1 + ((c & 0xFFF) % 15)));
                    else { falta(px, py, col, code); vid_faltas_spr++; vid_falta_spr_ult = code; }
                    LST_IN(ls);
                    continue;
                }
                { u32 cl = b_cl(h, col & 0x3F); hstamp[h] = now; hfl[h] &= (u8)~F_NUEVO; emit_b(px, py, (u32)h, cl, fl, 0); }
            }
    }
    LST_OUT(ls);
    if (win) { l_room(1); *lp++ = 0xE2000000; win = 0; }
    if (nl2 > (int)vid_b_maxl2) vid_b_maxl2 = (u32)nl2;
    if (vid_cifras && (now & 31) == 0) {                  /* cifras: huecos fijos y rotativos ocupados (recorre todos: solo
                                                             con las cifras a la vista, era un cuadro lento de cada 32) */
        u32 f = 0, r = 0;
        for (h = 0; h < NB; h++) if (hcode[h] != 0xFFFF) { if (hfl[h] & F_FIJO) f++; else r++; }
        vid_b_fijos = f; vid_b_rot = r;
    }
    vid_lineas = (u16)(T1 - t0);
}

/* ---- pantalla de carga al estilo del juego (la del port del WR1): bandas de cuadros de bandera arriba y abajo,
   CARGANDO en cursiva con el degradado amarillo-rojo y el borde negro de las cifras del marcador, y la barra
   START-FINISH del marcador, que se va llenando (vid_cargando_barra). Directo por GP0 en el bufer que se ve */
static void gp_rect(int x, int y, int w, int h, u32 c)
{
    dma_wait(); gpu_wait(); GP0 = 0xE6000000; GP0 = 0xE2000000; GP0 = 0xE3000000; GP0 = 0xE4000000 | (511 << 10) | 1023; GP0 = 0xE5000000;
    GP0 = 0x60000000 | c; GP0 = ((u32)y << 16) | (u32)x; GP0 = ((u32)h << 16) | (u32)w;
}
static const char lchars[] = "CARGNDOSTFIHPU";
static const u8 lglyph[14][7] = {                   /* 5x7, bit 4 a la izquierda */
    {14, 17, 16, 16, 16, 17, 14}, {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 20, 18, 17}, {14, 17, 16, 23, 17, 17, 15},
    {17, 25, 21, 19, 17, 17, 17}, {30, 17, 17, 17, 17, 17, 30}, {14, 17, 17, 17, 17, 17, 14}, {15, 16, 16, 14, 1, 1, 30},
    {31, 4, 4, 4, 4, 4, 4}, {31, 16, 16, 30, 16, 16, 16}, {14, 4, 4, 4, 4, 4, 14}, {17, 17, 17, 31, 17, 17, 17},
    {30, 17, 17, 30, 16, 16, 16}, {17, 17, 17, 17, 17, 17, 14}};
static const u32 lgrad[7] = {0x60F0FF, 0x40D8FF, 0x20B8FF, 0x1098FF, 0x0070FF, 0x0048F0, 0x0020D0};   /* 0xBBGGRR */
static const u32 lwhite[7] = {0xF8F8F8, 0xF8F8F8, 0xF0F0F0, 0xE8E8E8, 0xE0E0E0, 0xD0D0D0, 0xC0C0C0};
static const u32 lgreen[6] = {0x90FF90, 0x40F040, 0x20D820, 0x10B810, 0x089808, 0x047804};
static void gp_text(int y0, int x, int y, const char *t, int sc, const u32 *col)
{
    int pass, k, r, c, n;
    for (pass = 0; pass < 2; pass++)
        for (k = 0; t[k]; k++) {
            for (n = 0; lchars[n] && lchars[n] != t[k]; n++);
            if (!lchars[n]) continue;
            for (r = 0; r < 7; r++) for (c = 0; c < 5; c++) if (lglyph[n][r] & (16 >> c)) {
                int px = x + k * (6 * sc) + c * sc + (6 - r) * sc / 3, py = y0 + y + r * sc;
                if (!pass) gp_rect(px - 1, py - 1, sc + 3, sc + 2, 0);     /* borde negro */
                else gp_rect(px, py, sc + 1, sc, col[r]);
            }
        }
}
#define BAR_W 240
#define BAR_X ((W - BAR_W) / 2)
#define BAR_Y 134
void vid_cargando(int fb)
{
    int k, sc = 4, tw = 8 * 6 * sc - sc, y0 = fb ? 256 : 0;
    dma_wait(); gpu_wait();
    GP0 = 0x02000000; GP0 = (u32)y0 << 16; GP0 = ((u32)H << 16) | W;
    for (k = 0; k < 46 * 4; k++) {                 /* cuadros de bandera: 2 filas arriba y 2 abajo */
        int row = k / 46, col = k % 46, y = row < 2 ? row * 8 : 224 + (row - 2) * 8;
        if ((row + col) & 1) gp_rect(col * 8, y0 + y, 8, 8, 0xE0E0E0);
    }
    gp_text(y0, (W - tw) / 2, 88, "CARGANDO", sc, lgrad);
    gp_rect(BAR_X - 3, y0 + BAR_Y - 1, BAR_W + 6, 8, 0xB0B0B0);   /* marco gris redondeado y el interior oscuro */
    gp_rect(BAR_X - 1, y0 + BAR_Y - 3, BAR_W + 2, 12, 0xB0B0B0);
    gp_rect(BAR_X - 1, y0 + BAR_Y - 1, BAR_W + 2, 8, 0x181818);
    gp_text(y0, BAR_X - 6, BAR_Y + 12, "START", 1, lwhite);
    gp_text(y0, BAR_X + BAR_W - 6 * 6 + 4, BAR_Y + 12, "FINISH", 1, lwhite);
}
void vid_cargando_barra(int fb, int hecho, int total)
{
    int r, w = total > 0 ? hecho * BAR_W / total : 0, y0 = fb ? 256 : 0;
    if (w > BAR_W) w = BAR_W;
    if (w > 0) for (r = 0; r < 6; r++) gp_rect(BAR_X, y0 + BAR_Y + r, w, 1, lgreen[r]);
}
/* pausa (START en carrera, como el WR1): el cuadro que se ve oscurecido a la mitad (rectangulo negro semitransparente,
   B/2 + F/2) con PAUSA encima, con las mismas letras que CARGANDO */
void vid_pausa(int fb)
{
    int sc = 4, y0 = fb ? 256 : 0;
    dma_wait(); gpu_wait();
    GP0 = 0xE1000400; GP0 = 0xE6000000; GP0 = 0xE2000000; GP0 = 0xE3000000; GP0 = 0xE4000000 | (511 << 10) | 1023; GP0 = 0xE5000000;
    GP0 = 0x62000000; GP0 = (u32)y0 << 16; GP0 = ((u32)H << 16) | W;
    gp_text(y0, (W - (5 * 6 * sc - sc)) / 2, 100, "PAUSA", sc, lgrad);
}

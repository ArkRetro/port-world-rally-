/* World Rally -> PlayStation, fase 2: el programa original del 68000 recompilado a MIPS (rc68.py) con la
   MCU (8051 interpretado), y la imagen dibujada cada cuadro desde la VRAM/paletas/sprites del juego.
   Uso personal. */
#include "../rc/rc_rt.h"
#include "../rc/mcs51.h"
#include "cd.h"
#define REG(a) (*(volatile u32 *)(a))
#define GP0 REG(0x1F801810)
#define GP1 REG(0x1F801814)
#define I_STAT REG(0x1F801070)
#define I_MASK REG(0x1F801074)
#define D2_MADR REG(0x1F8010A0)
#define D2_BCR REG(0x1F8010A4)
#define D2_CHCR REG(0x1F8010A8)
#define DPCR REG(0x1F8010F0)
#define JOY_DATA (*(volatile u8 *)0x1F801040)
#define JOY_STAT (*(volatile u16 *)0x1F801044)
#define JOY_MODE (*(volatile u16 *)0x1F801048)
#define JOY_CTRL (*(volatile u16 *)0x1F80104A)
#define JOY_BAUD (*(volatile u16 *)0x1F80104E)

void rc_mem_init(void); void rc_set_io(u16 dsw, u16 in);
extern const u8 mcu_rom[0x8000];
static mcs51_t mcu;
static u16 io_in = 0xFFEF, io_dsw = 0xFFDF;
u16 gdecrypt(u16 ep, u16 dp, u16 e);

void mcu_run_c(mcs51_t *c);
void rc_mcu(void) { mcu_run_c(&mcu); }
void rc_divzero(u32 pc) { (void)pc; for (;;); }
static volatile u32 bad_pc;
void rc_bad(u32 pc) { bad_pc = pc; GP1 = 0x03000000; for (;;); }

/* ------------------------------------------------------------ GPU */
#define W 368
#define H 240
#define CLUT_Y 240
#define TEX_X 384
#ifndef FRAMESKIP
#define FRAMESKIP 1          /* si un cuadro llega tarde no se dibuja el siguiente: la logica sigue a 60 Hz */
#endif
static void gpu_wait(void) { while (!(GP1 & (1 << 26))); }
static void dma_wait(void) { while (D2_CHCR & (1 << 24)); }
static void upload(int x, int y, int w, int h, const void *d)
{
    int words = w * h / 2, bs = 16;
    dma_wait(); gpu_wait();
    GP0 = 0x01000000;
    GP0 = 0xA0000000; GP0 = (y << 16) | x; GP0 = (h << 16) | w;
    if (words % bs) bs = words < 16 ? words : 1;
    D2_MADR = (u32)d; D2_BCR = ((words / bs) << 16) | bs; D2_CHCR = 0x01000201;
    dma_wait();
}
/* lista para la GPU: un cubo por paso de dibujo, cada uno una cadena de trozos de CHW palabras de un deposito
   comun. Las capas y la tabla de sprites se recorren una sola vez repartiendo cada tile en sus cubos, y al final
   los cubos se enlazan en el orden de la placa con nodos vacios (o con un E2 si el cubo acabo con la ventana de
   textura puesta). La DMA no lee del scratchpad: los trozos estan en la RAM principal */
#define CHW 1024
#define NCH 32
static u32 pool[NCH][CHW];
static int pool_n;
static u32 hdr[10];
/* cabecera de un nodo de n palabras (la direccion del siguiente en los 24 bits bajos): las de la RAM son
   0x80xxxxxx, asi que basta con sumar */
#define NODE(p, n) ((p)[0] = (u32)((p) + 1 + (n)) + (((u32)(n) << 24) - 0x80000000u))
/* bucles calientes: sin la planificacion antes de asignar registros, que lo adelanta todo y acaba guardando
   constantes en la pila (cada lectura de la RAM cuesta ~6 ciclos); y sin convertir bucles en memset */
#define HOT_OPT optimize("no-schedule-insns", "no-tree-loop-distribute-patterns")
#define HOT __attribute__((noinline, HOT_OPT))
typedef struct { u32 *p, *end, win; } bucket_t;     /* el primer trozo del cubo k es pool[k] */
enum { BK_A, BK_B, BK_C, BK_D, BK_E00, BK_E01, BK_S0, BK_F, BK_E10, BK_E11, BK_S1, BK_DBG, NBK };
/* scratchpad (1 KB, lectura en 1 ciclo; las de RAM cuestan ~6): estado de los cubos y tablas que se leen por
   cada tile. Se usan 0x1F800000-0x1F8002E7 */
typedef struct { bucket_t bk[NBK]; u32 pal_wf[32], cl16[32], te1[20], fuv[4][4], rec[(W / 16 + 2) * 2]; } spad_t;
#define SPAD ((spad_t *)0x1F800000)
typedef char spad_cabe[sizeof(spad_t) <= 0x300 ? 1 : -1];
static u32 *bk_cur[NBK];
/* trozo nuevo para un cubo (empieza con la ventana de textura apagada). Deposito lleno: se reescribe el ultimo
   trozo del cubo (se pierde lo dibujado en el, como al llenarse la lista antes) */
static __attribute__((noinline)) u32 *chunk_new(bucket_t *b, u32 *p)
{
    u32 *c;
    if (pool_n < NCH) { c = pool[pool_n++]; p[0] = (u32)c & 0xFFFFFF; bk_cur[b - SPAD->bk] = c; b->end = c + CHW - 2; }
    else c = bk_cur[b - SPAD->bk];
    NODE(c, 1); c[1] = 0xE2000000;
    return c + 2;
}
static void bk_init(void)
{
    bucket_t *b = SPAD->bk; int k;
    for (k = 0; k < NBK; k++, b++) { u32 *c = pool[k]; bk_cur[k] = b->p = c; b->end = c + CHW - 2; b->win = 0; }
    pool_n = NBK;
}
/* enlaza los cubos detras de la cabecera (bufer fy: area de dibujo, desplazamiento, fondo negro) y lo manda */
static void pk_send(int fy)
{
    bucket_t *b = SPAD->bk; int k;
    u32 *h = hdr;
    h[0] = (8u << 24) | ((u32)pool[0] & 0xFFFFFF);
    h[1] = 0xE3000000 | (fy << 10); h[2] = 0xE4000000 | ((fy + H - 1) << 10) | (W - 1); h[3] = 0xE5000000 | (fy << 11);
    h[4] = 0xE6000000; h[5] = 0xE2000000;
    h[6] = 0x02000000; h[7] = (u32)fy << 16; h[8] = (H << 16) | W;          /* fondo negro */
    for (k = 0; k < NBK; k++, b++) {
        u32 *p = b->p, nx = k + 1 < NBK ? (u32)pool[k + 1] & 0xFFFFFF : 0xFFFFFF;
        if (b->win) { p[0] = (1u << 24) | nx; p[1] = 0xE2000000; } else p[0] = nx;
    }
    dma_wait(); gpu_wait(); D2_MADR = (u32)hdr; D2_BCR = 0; D2_CHCR = 0x01000401;
}
static int vbl_count;
/* espera al siguiente vblank y lo reconoce: la bandera que haya al volver a entrar es que ya paso otro */
static void vsync(void) { I_STAT = ~1u; while (!(I_STAT & 1)); I_STAT = ~1u; vbl_count++; }

/* paletas: 64 del juego (0x000-0x3FF) en 4 variantes: 0 opaca, 1 pen 0 transparente, 2 solo pens 1-7,
   3 solo pens 8-15 (para las prioridades por grupo de pens de la capa delantera). Se miran solo las que usa
   el cuadro (0-31 las capas, 32-47 los sprites; 48-63 nadie) y se convierten las que han cambiado; las que no se
   usan pueden quedar viejas en la VRAM hasta que se vuelvan a usar */
#define NPAL 48
static u16 clut[4][64][16];
static u32 pal_prev[NPAL * 8];
static int pal_first = 1;
static u16 c15(u16 v)
{
    u32 b = (v >> 8) & 15, r = (v >> 4) & 15, g = v & 15, c;
    c = ((r << 1) | (r >> 3)) | (((g << 1) | (g >> 3)) << 5) | (((b << 1) | (b >> 3)) << 10);
    return c ? c : 0x8000;
}
static __attribute__((noinline)) int pal_convert(u32 m0, u32 m1)    /* m0: paletas 0-31 que se usan, m1: 32-47 */
{
    int p, k, ch = 0;
    const u32 *q = (const u32 *)rc_pal; u32 *o = pal_prev;
    if (pal_first) m0 = m1 = ~0u;
    for (p = 0; p < NPAL; p++, q += 8, o += 8) {
        if (!(((p < 32 ? m0 : m1) >> (p & 31)) & 1)) continue;
        if (!pal_first && !((q[0] ^ o[0]) | (q[1] ^ o[1]) | (q[2] ^ o[2]) | (q[3] ^ o[3]) |
                             (q[4] ^ o[4]) | (q[5] ^ o[5]) | (q[6] ^ o[6]) | (q[7] ^ o[7]))) continue;
        ch = 1;
        for (k = 0; k < 8; k++) o[k] = q[k];
        for (k = 0; k < 16; k++) {
            u16 c = c15(((const u16 *)q)[k]);
            clut[0][p][k] = c;
            clut[1][p][k] = k ? c : 0;
            clut[2][p][k] = (k && k < 8) ? c : 0;
            clut[3][p][k] = k >= 8 ? c : 0;
        }
    }
    pal_first = 0;
    return ch;
}
#define CLUTID(v, p) ((u32)((((CLUT_Y + ((v) * 4 + ((p) >> 4))) << 6) | ((p) & 15))))
static inline u32 tpage_of(int s) { int tp = s >> 8; return ((TEX_X + (tp % 10) * 64) >> 6) | (((tp / 10) & 1) << 4); }

/* ---- cache de tiles en VRAM: 5120 huecos de un tile (20 paginas de textura de 256 tiles). Al cambiar de
   recorrido (puntero de la tabla de filas del juego en 0xFEC190) se cargan del CD el paquete del recorrido y el
   de carrera (sprites); lo que falte se lee de TILES.BIN al vuelo. Reemplazo por reloj con edad. */
#define NTS 5120
static u16 tslot[16384], slot_tile[NTS];
static u32 slot_age[NTS];
static int hand, frame_id = 10;
static u16 miss_list[512]; static int nmiss;
static u8 miss_mark[16384 / 8];
static u32 cd_loads, tiles_up, pack_err;
static int tiles_lba = -1, race_lba = -1, level_lba[12], cur_level = -1, level_sec[12], race_sec;
static u32 cur_lvl_ptr;
static const u32 level_ptr[12] = {0x1500a, 0x6d61e, 0x71628, 0x7d646, 0x75632, 0x8565a, 0x81650, 0x89664, 0x7963c, 0x8d66e, 0x91678, 0x95682};
static inline int tile_slot(u32 t)
{
    u32 s = tslot[t];
    if (s == 0xFFFF) {
        if (!(miss_mark[t >> 3] & (1 << (t & 7))) && nmiss < 512) { miss_mark[t >> 3] |= 1 << (t & 7); miss_list[nmiss++] = t; }
        return -1;
    }
    slot_age[s] = frame_id;
    return (int)s;
}
static void cache_init(void)
{
    int i;
    for (i = 0; i < 16384; i++) tslot[i] = 0xFFFF;
    for (i = 0; i < NTS; i++) { slot_tile[i] = 0xFFFF; slot_age[i] = 0; }
}
static int slot_alloc(void)
{
    int pass, n;
    static const int old[3] = {1800, 120, 2};
    for (pass = 0; pass < 3; pass++)
        for (n = 0; n < NTS; n++) {
            int h = hand; hand = hand + 1 == NTS ? 0 : hand + 1;
            if (slot_tile[h] == 0xFFFF || (s32)(frame_id - slot_age[h]) >= old[pass]) return h;
        }
    return -1;
}
static void upload(int x, int y, int w, int h, const void *d);
static void tile_put(u32 t, const void *data)
{
    int s;
    if (tslot[t] != 0xFFFF) { slot_age[tslot[t]] = frame_id; return; }
    s = slot_alloc();
    if (s < 0) return;
    if (slot_tile[s] != 0xFFFF) tslot[slot_tile[s]] = 0xFFFF;
    slot_tile[s] = t; tslot[t] = s; slot_age[s] = frame_id; tiles_up++;
    upload(TEX_X + ((s >> 8) % 10) * 64 + (s & 15) * 4, ((s >> 8) / 10) * 256 + ((s >> 4) & 15) * 16, 4, 16, data);
}
static u8 cdbuf[16 * 2048] __attribute__((aligned(4)));
static u16 packids[4096];
/* carga un paquete (cabecera con la lista de tiles y luego los datos) */
/* ---- pantalla de carga: barra dibujada directamente en los dos bufers */
static void gp_fill(int x, int y, int w, int h, u32 c)
{
    dma_wait(); gpu_wait(); GP0 = 0x02000000 | c; GP0 = ((u32)y << 16) | x; GP0 = ((u32)h << 16) | w;
}
static void gp_rect(int x, int y, int w, int h, u32 c)
{
    dma_wait(); gpu_wait(); GP0 = 0xE6000000; GP0 = 0xE2000000; GP0 = 0xE3000000; GP0 = 0xE4000000 | (511 << 10) | 1023; GP0 = 0xE5000000;
    GP0 = 0x60000000 | c; GP0 = ((u32)y << 16) | x; GP0 = ((u32)h << 16) | w;
}
static const u16 lfont[8] = {0x7927, 0x2BED, 0x6BAD, 0x796F, 0x2BED, 0x6B6D, 0x6B6E, 0x7B6F};   /* CARGANDO */
static int load_total, load_done, loading;
static void load_bar(void)
{
    int b, w = load_total ? load_done * 256 / load_total : 0;
    for (b = 0; b < 2; b++) gp_fill(56, b * 256 + 150, (w + 15) & ~15, 8, 0xC0C0C0);
}
static void load_screen(int total)
{
    int b, n, k;
    load_total = total; load_done = 0; loading = 1;
    for (b = 0; b < 2; b++) {
        gp_fill(0, b * 256, 368, 240, 0);
        gp_fill(48, b * 256 + 142, 272, 24, 0x404040);
        gp_fill(56, b * 256 + 150, 256, 8, 0x101010);
        for (n = 0; n < 8; n++) for (k = 0; k < 15; k++)
            if (lfont[n] & (1 << (14 - k))) gp_rect(120 + n * 16 + (k % 3) * 4, b * 256 + 110 + (k / 3) * 4, 4, 4, 0x40C0FF);
    }
}
static void pack_load(int lba)
{
    int n, hs, i, s0;
    if (lba < 0) return;
    if (cd_read(lba, 1, cdbuf)) { pack_err++; return; }
    n = *(u16 *)cdbuf; if (n > 4000) { pack_err += 100; return; }
    hs = (2 + n * 2 + 2047) / 2048;
    if (hs > 1 && cd_read(lba, hs, cdbuf)) return;
    for (i = 0; i < n; i++) packids[i] = ((u16 *)cdbuf)[1 + i];
    for (s0 = 0; s0 * 16 < n; s0 += 16) {
        int ns = (n - s0 * 16 + 15) / 16; if (ns > 16) ns = 16;
        if (cd_read(lba + hs + s0, ns, cdbuf)) { pack_err += 10000; return; }
        for (i = 0; i < ns * 16 && s0 * 16 + i < n; i++) tile_put(packids[s0 * 16 + i], cdbuf + i * 128);
        if (loading) { load_done += ns; load_bar(); }
    }
    cd_loads++;
}
static void level_check(void)
{
    u32 p = ((u32)*(u16 *)(rc_ram + 0x190) << 16) | *(u16 *)(rc_ram + 0x192);
    int k;
    if (p == cur_lvl_ptr) return;
    cur_lvl_ptr = p;
    for (k = 0; k < 12 && level_ptr[k] != p; k++);
    if (k == 12 || k == cur_level) return;
    cur_level = k;
    {   extern int snd_cd_busy; void snd_resume(void);
        int was = snd_cd_busy;
        load_screen(level_sec[k] + race_sec);
        pack_load(level_lba[k]);
        pack_load(race_lba);
        loading = 0;
        if (was) snd_resume();
    }
}
static void cache_load(void)
{
    int i, j;
    extern int snd_cd_busy; void snd_resume(void);
    int was = snd_cd_busy;
    if (!nmiss || tiles_lba < 0) { nmiss = 0; return; }
    /* ordenar (insercion: pocas) y leer por tramos de sectores de TILES.BIN */
    for (i = 1; i < nmiss; i++) { u16 v = miss_list[i]; for (j = i; j > 0 && miss_list[j - 1] > v; j--) miss_list[j] = miss_list[j - 1]; miss_list[j] = v; }
    i = 0;
    while (i < nmiss) {
        int s0 = miss_list[i] >> 4, s1 = s0, k;
        for (j = i + 1; j < nmiss && (miss_list[j] >> 4) - s1 <= 2 && (miss_list[j] >> 4) - s0 < 16; j++) s1 = miss_list[j] >> 4;
        if (cd_read(tiles_lba + s0, s1 - s0 + 1, cdbuf) == 0) {
            cd_loads++;
            for (k = i; k < j; k++) tile_put(miss_list[k], cdbuf + (miss_list[k] - s0 * 16) * 128);
        }
        i = j;
    }
    for (i = 0; i < nmiss; i++) miss_mark[miss_list[i] >> 3] = 0;
    nmiss = 0;
    if (was) snd_resume();
}
/* tablas en el scratchpad: E1 (pagina de textura) por pagina de la cache, CLUT (<<16) de cada paleta de las
   capas en la variante 0 (las otras variantes estan 4 filas mas abajo cada una: + CLV(v)), y V<<8 | U de las
   cuatro esquinas del cuadrilatero de un tile con espejo */
#define CLV(v) ((u32)((v) * 4) << 22)
static void tables_init(void)
{
    int p;
    for (p = 0; p < 20; p++) SPAD->te1[p] = 0xE1000400 | ((TEX_X + (p % 10) * 64) >> 6) | (((p / 10) & 1) << 4);
    for (p = 0; p < 32; p++) { SPAD->cl16[p] = CLUTID(0, p) << 16; SPAD->pal_wf[p] = 0; }
    for (p = 0; p < 4; p++) {
        u32 uL = (p & 1) ? 31 : 0, uR = (p & 1) ? 15 : 16, vT = (p & 2) ? 31 : 0, vB = (p & 2) ? 15 : 16;
        SPAD->fuv[p][0] = (vT << 8) | uL; SPAD->fuv[p][1] = (vT << 8) | uR; SPAD->fuv[p][2] = (vB << 8) | uL; SPAD->fuv[p][3] = (vB << 8) | uR;
    }
}
/* un tile 16x16 en (x0, y0>>16) (x0 e y0 ya en el formato de la GPU) con a = CLUT<<16 | V<<8 | U: sin espejo,
   sprite 0x7D (+E1); con espejo, cuadrilatero 0x2D con la ventana de textura (E2) ajustada al tile, para que U/V
   puedan ir de 31 a 15 sin salirse (las GPU antiguas no hacen espejo en los rectangulos). *win: si la ventana
   quedo puesta */
static inline __attribute__((always_inline)) u32 *emit_tile(u32 *p, u32 *win, u32 x0, u32 y0, u32 a, u32 fl, u32 tp, u32 semi)
{
    if (semi) tp |= (semi - 1) << 5;
    if (!fl) {
        if (*win) { NODE(p, 1); p[1] = 0xE2000000; p += 2; *win = 0; }
        NODE(p, 4);
        p[1] = 0xE1000400 | tp; p[2] = semi ? 0x7F000000 : 0x7D000000;
        p[3] = y0 | x0;
        p[4] = a;
        return p + 5;
    } else {
        const u32 *f = SPAD->fuv[fl];
        u32 x1 = (x0 + 16) & 0xFFFF, y1 = y0 + (16 << 16);
        NODE(p, 10);
        p[1] = 0xE20003DE | ((a & 0xF0) << 7) | ((a & 0xF000) << 4);
        p[2] = semi ? 0x2F808080 : 0x2D808080;
        p[3] = y0 | x0; p[4] = (a & 0xFFFF0000) | f[0];
        p[5] = y0 | x1; p[6] = (tp << 16) | f[1];
        p[7] = y1 | x0; p[8] = f[2];
        p[9] = y1 | x1; p[10] = f[3];
        *win = 1;
        return p + 11;
    }
}
/* lo mismo en un cubo cualquiera (mira si cabe) */
static inline __attribute__((always_inline)) void emit_bk(bucket_t *b, u32 x0, u32 y0, u32 a, u32 fl, u32 tp, u32 semi)
{
    u32 *p = b->p, win = b->win;
    if (p + 11 > b->end) { p = chunk_new(b, p); win = 0; }
    b->p = emit_tile(p, &win, x0, y0, a, fl, tp, semi); b->win = win;
}
/* capas: cada tile visible en sus cubos segun la categoria (bit 5 del atributo); los de la columna y la fila de
   mas solo se piden a la cache (para tenerlos antes) y cuentan para pal_w. Orden de la placa: fondo, frente
   cat. 0, fondo cat. 1, frente cat. 1 (pens 1-7), sprites, frente cat. 1 (pens 8-15), sprites de prioridad alta:
   capa 1 (fondo): todos -> A (variante 0); cat. 1 tambien -> C (variante 1)
   capa 0 (frente): cat. 0 -> B (variante 1); cat. 1 -> D (variante 2) y F (variante 3)
   Por filas: los tiles sin espejo del cubo principal de la capa se emiten segun se leen; los demas (con espejo,
   y los de la cat. 1 para los otros cubos) van a SPAD->rec, dos palabras por tile (CLUT<<16 | V<<8 | U de la
   variante 0, y x | categoria<<16 | espejo<<17 | pagina<<24): los sin espejo desde el principio y los con espejo
   desde el final, y luego se emiten con un bucle corto por cubo (dentro de una capa los tiles no se tapan: el
   orden da igual) */
#define ROWW ((W / 16 + 2) * 11 + 2)      /* lo mas que puede ocupar una fila de tiles en un cubo */
#define NROW (W / 16 + 2)
#define REC_CAT (1u << 16)
/* fila: c columna del mapa, nv columnas que se ven (las demas solo se piden a la cache), xy = y | x de la primera
   con x en 11 bits con signo (la GPU no mira los bits 11-15: asi xy + 16 * k no pasa a la y), b cubo principal
   con la variante v (todas: todas las categorias; si no, solo la 0). Devuelve cuantos tiles hay en rec sin
   espejo, con espejo (<<8) y si hay de la cat. 1 (<<16) */
static inline __attribute__((always_inline, HOT_OPT)) u32 row_run(const u32 *row, u32 c, u32 nv, u32 xy, bucket_t *b, u32 v, const int todas)
{
    spad_t *__restrict sp = SPAD;
    u32 *__restrict p = b->p, *r = sp->rec, *rf = r + NROW * 2, win = b->win, any = 0, k = NROW - nv;
    if (p > b->end - ROWW) { p = chunk_new(b, p); win = 0; }
    for (; nv; nv--, xy += 16, c = (c + 1) & 63) {
        u32 w = row[c], d2 = w >> 16, pi = d2 & 31, a, t;
        int s = tile_slot(w & 0x3FFF);
        if (s < 0) continue;
        sp->pal_wf[pi]++;
        a = sp->cl16[pi] | ((s & 0xF0) << 8) | ((s & 15) << 4);
        t = sp->te1[s >> 8];
        any |= d2;
        if (!(d2 & 0xC0)) {
            if (todas || !(d2 & 0x20)) {         /* sin espejo: sprite 16x16 en el cubo principal */
                if (win) { NODE(p, 1); p[1] = 0xE2000000; p += 2; win = 0; }
                NODE(p, 4);
                p[1] = t; p[2] = 0x7D000000;
                p[3] = xy;
                p[4] = a + v;
                p += 5;
            }
            if (d2 & 0x20) { r[0] = a; r[1] = (xy & 0xFFFF) | (((d2 >> 5) & 7) << 16) | (t << 24); r += 2; }
        } else { rf -= 2; rf[0] = a; rf[1] = (xy & 0xFFFF) | (((d2 >> 5) & 7) << 16) | (t << 24); }
    }
    for (; k; k--, c = (c + 1) & 63) {           /* fuera de la pantalla: solo se piden a la cache */
        u32 w = row[c];
        if (tile_slot(w & 0x3FFF) >= 0) sp->pal_wf[(w >> 16) & 31]++;
    }
    b->p = p; b->win = win;
    return ((r - sp->rec) >> 1) | (((sp->rec + NROW * 2 - rf) >> 1) << 8) | ((any & 0x20) << 11);
}
static HOT u32 row_run1(const u32 *row, u32 c, u32 nv, u32 xy) { return row_run(row, c, nv, xy, &SPAD->bk[BK_A], CLV(0), 1); }
static HOT u32 row_run0(const u32 *row, u32 c, u32 nv, u32 xy) { return row_run(row, c, nv, xy, &SPAD->bk[BK_B], CLV(1), 0); }
/* los tiles de rec con (categoria & cm) == cv a un cubo, con la CLUT de la variante (v = CLV(variante)) */
static HOT void row_emit(bucket_t *b, const u32 *rec, u32 n, u32 y0, u32 v, u32 cm, u32 cv)
{
    u32 *__restrict p = b->p, win = b->win;
    const u32 *r = rec, *re = rec + (n & 255) * 2;
    if (p + ((n & 255) + ((n >> 8) & 255)) * 11 + 2 > b->end) { p = chunk_new(b, p); win = 0; }
    if (win && r < re) { NODE(p, 1); p[1] = 0xE2000000; p += 2; win = 0; }
    for (; r < re; r += 2) {                     /* sin espejo: sprite 16x16 */
        u32 a = r[0], e = r[1];
        if ((e & cm) != cv) continue;
        NODE(p, 4);
        p[1] = 0xE1000400 | (e >> 24); p[2] = 0x7D000000;
        p[3] = y0 | (e & 0xFFFF);
        p[4] = a + v;
        p += 5;
    }
    for (r = rec + (NROW - ((n >> 8) & 255)) * 2, re = rec + NROW * 2; r < re; r += 2) {   /* con espejo */
        u32 a = r[0] + v, e = r[1], x0 = e & 0xFFFF, x1 = (x0 + 16) & 0xFFFF, y1 = y0 + (16 << 16);
        const u32 *f = (const u32 *)((u8 *)SPAD->fuv + ((e >> 13) & 0x30));
        if ((e & cm) != cv) continue;
        NODE(p, 10);
        p[1] = 0xE20003DE | ((a & 0xF0) << 7) | ((a & 0xF000) << 4);
        p[2] = 0x2D808080;
        p[3] = y0 | x0; p[4] = (a & 0xFFFF0000) | f[0];
        p[5] = y0 | x1; p[6] = ((e >> 24) << 16) | f[1];
        p[7] = y1 | x0; p[8] = f[2];
        p[9] = y1 | x1; p[10] = f[3];
        p += 11; win = 1;
    }
    b->p = p; b->win = win;
}
/* capa: kr recibe la cat. 0 (y la 1 si todas) con la variante vr; kx, ky la cat. 1 (ky < 0: ninguno) */
static void layer(const u32 *m, int sx, int sy, int kr, u32 vr, int todas, int kx, u32 vx, int ky, u32 vy)
{
    bucket_t *bk = SPAD->bk;
    u32 *rec = SPAD->rec, y0 = (u32)-(sy & 15) << 16;
    int tx0 = sx >> 4, ty0 = sy >> 4, ox = -(sx & 15), oy = -(sy & 15), j;
    int nv = (W + 15 - ox) >> 4, nf = (H + 15 - oy) >> 4;   /* columnas y filas que se ven */
    for (j = 0; j < H / 16 + 2; j++, y0 += 16 << 16) {
        u32 n = (todas ? row_run1 : row_run0)(m + ((ty0 + j) & 31) * 64, tx0 & 63, j < nf ? nv : 0, y0 | (ox & 0x7FF));
        if (n & 0xFF00) row_emit(bk + kr, rec, n & 0xFF00, y0, vr, todas ? 0 : REC_CAT, 0);
        if (n >> 16) {
            row_emit(bk + kx, rec, n, y0, vx, REC_CAT, REC_CAT);
            if (ky >= 0) row_emit(bk + ky, rec, n, y0, vy, REC_CAT, REC_CAT);
        }
    }
}
/* sprites de efecto (faros, sombras): en la placa cambian el banco de paleta (1-7) de lo que tapan; aqui se
   dibujan semitransparentes con dos CLUT (lo que el banco k suma y lo que resta, de media, sobre la paleta
   normal), en x 256 y 272 de la fila 240 */
static u16 eff_clut[2][16];
#define EFF_CLUT(i) ((u32)((CLUT_Y << 6) | (16 + (i))))
/* suma de las componentes de los 16 colores de una paleta (r = bits 4-7, g = 0-3, b = 8-11): dos colores por
   lectura, en campos de 8 bits que no desbordan */
#define PS1(v) (a += (v) & 0x0F0F0F0F, b += ((v) >> 4) & 0x000F000F)
static inline __attribute__((always_inline)) void pal_sum(const u32 *w, int *r, int *g, int *bl)
{
    u32 a = 0, b = 0, v0 = w[0], v1 = w[1], v2 = w[2], v3 = w[3], v4 = w[4], v5 = w[5], v6 = w[6], v7 = w[7];
    PS1(v0); PS1(v1); PS1(v2); PS1(v3); PS1(v4); PS1(v5); PS1(v6); PS1(v7);
    *r = (b & 0xFFFF) + (b >> 16);
    *g = (a & 0xFF) + ((a >> 16) & 0xFF);
    *bl = ((a >> 8) & 0xFF) + (a >> 24);
}
/* tiles de cada paleta en los cuadros anteriores (los cuenta cada cuadro en SPAD->pal_wf): pesos del color de la
   luz. Por cada banco k, la media ponderada de lo que cambia cada componente del banco 0 al k */
static u32 pal_w[32];
static HOT void eff_update(void)
{
    const u32 *pr = (const u32 *)rc_pal;
    int k, i, p, c, d[8][3];
    u32 tw = 0;
    for (p = 0; p < 32; p++) tw += pal_w[p];
    if (!tw) return;
    for (k = 1; k < 8; k++) d[k][0] = d[k][1] = d[k][2] = 0;
    for (p = 0; p < 32; p++) {
        const u32 *q = pr + p * 8;
        int wgt = pal_w[p], r0, g0, b0, r, g, b;
        if (!wgt) continue;
        pal_sum(q, &r0, &g0, &b0);
        for (k = 1; k < 8; k++) {
            pal_sum(q + k * 512, &r, &g, &b);
            d[k][0] += (r - r0) * wgt; d[k][1] += (g - g0) * wgt; d[k][2] += (b - b0) * wgt;
        }
    }
    for (k = 1; k < 8; k++) {
        int pos[3], neg[3];
        /* media por color (4 bits) -> 5 bits */
        for (c = 0; c < 3; c++) { int v = d[k][c] * 3 / (int)(tw * 16); pos[c] = v > 0 ? (v > 31 ? 31 : v) : 0; neg[c] = v < 0 ? (-v > 31 ? 31 : -v) : 0; }
        eff_clut[0][8 + k] = (pos[0] | pos[1] << 5 | pos[2] << 10) ? (u16)(0x8000 | pos[0] | pos[1] << 5 | pos[2] << 10) : 0;
        eff_clut[1][8 + k] = (neg[0] | neg[1] << 5 | neg[2] << 10) ? (u16)(0x8000 | neg[0] | neg[1] << 5 | neg[2] << 10) : 0;
    }
    eff_clut[0][8] = eff_clut[1][8] = 0;
    for (i = 0; i < 8; i++) eff_clut[0][i] = eff_clut[1][i] = 0;
    for (p = 0; p < 32; p++) pal_w[p] = 0;
}
/* tabla de sprites en una pasada: entrada k = palabras 3+4k..6+4k, leidas de dos en dos (q[0] = w0 << 16,
   q[1] = w2 << 16 | w1, q[2] = w3 | ...). Primero la x, que descarta casi todas las que no se ven con una sola
   lectura. Prioridad 0 = bajo la parte alta de la capa delantera, 1 = encima de todo. De efecto (color >= 16):
   pasada 0 (suma) en E*0 y pasada 1 (resta) en E*1; normales en S* */
#define NSPR 511
static HOT u32 sprites(void)    /* devuelve las paletas 32-47 usadas */
{
    const u32 *q = (const u32 *)(rc_spr + 4), *te1 = SPAD->te1;
    u32 pm = 0;
    int k;
    for (k = 0; k < NSPR; k++, q += 2) {
        u32 w2 = q[1] >> 16, u = (w2 - 8) & 0x3FF, w0, num, tp, y0, uv;
        int s, pr;
        if (u >= W + 15) continue;                            /* x = ((w2 & 0x3FF) - 23) & 0x3FF fuera */
        w0 = q[0] >> 16;
        if ((w0 & 0xFF) == 0xF0) continue;                    /* y = -16: fuera */
        num = q[2] & 0x3FFF;
        s = tile_slot(num);
        if (s < 0) continue;
        pr = num >= 0x3700;
        tp = te1[s >> 8] & 0x1FF; uv = ((s & 0xF0) << 8) | ((s & 15) << 4);
        y0 = (u32)(((240 - (w0 & 0xFF)) & 0xFF) - 16) << 16;
        if (w2 & 0x4000) {
            bucket_t *b = &SPAD->bk[pr ? BK_E10 : BK_E00];
            emit_bk(b, (u - 15) & 0xFFFF, y0, (EFF_CLUT(0) << 16) | uv, w0 >> 14, tp, 2);
            emit_bk(b + 1, (u - 15) & 0xFFFF, y0, (EFF_CLUT(1) << 16) | uv, w0 >> 14, tp, 3);
        } else {
            emit_bk(&SPAD->bk[pr ? BK_S1 : BK_S0], (u - 15) & 0xFFFF, y0, (CLUTID(1, 32 + ((w2 >> 10) & 15)) << 16) | uv, w0 >> 14, tp, 0);
            pm |= 1 << ((w2 >> 10) & 15);
        }
    }
    return pm;
}

/* ------------------------------------------------------------ mando (SIO0 directo) */
static void joy_init(void) { JOY_CTRL = 0x40; JOY_BAUD = 0x88; JOY_MODE = 0x0D; JOY_CTRL = 0; }
/* un byte; tras el ultimo el mando no da ACK y no hay que esperarlo */
static u8 joy_x(u8 b, int ack)
{
    int t;
    for (t = 0; t < 2000 && !(JOY_STAT & 1); t++);
    JOY_DATA = b;
    for (t = 0; t < 2000 && !(JOY_STAT & 2); t++);
    b = JOY_DATA;
    if (ack) for (t = 0; t < 600 && !(JOY_STAT & 0x80); t++);
    JOY_CTRL |= 0x10;
    return b;
}
static u16 joy_read(void)
{
    u16 r = 0xFFFF; u8 id; int t;
    JOY_CTRL = 0x1003;
    for (t = 0; t < 300; t++) __asm__ volatile ("nop");
    joy_x(0x01, 1); id = joy_x(0x42, 1);
    if ((id & 0xF0) == 0x40 || (id & 0xF0) == 0x70 || (id & 0xF0) == 0x50) {
        joy_x(0, 1); r = joy_x(0, 1); r |= joy_x(0, 0) << 8;
    }
    JOY_CTRL = 0;
    return r;
}

static int fb, nframe, gear, sq_last, l2_last = 1, show_dbg = 0, skipped, eff_due;
static u32 skips;
static const u16 font[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
static void digits(int x, int y, u32 v)
{
    bucket_t *b = &SPAD->bk[BK_DBG];
    int n, k, d[5];
    for (n = 0; n < 5; n++) { d[n] = v % 10; v /= 10; }
    for (n = 0; n < 5; n++)
        for (k = 0; k < 15; k++)
            if (font[d[4 - n]] & (1 << (14 - k))) {
                u32 *p = b->p;
                if (p + 4 > b->end) p = chunk_new(b, p);
                NODE(p, 3); p[1] = 0x60FFFF00; p[2] = ((u32)(y + (k / 3) * 2) << 16) | (x + n * 8 + (k % 3) * 2); p[3] = (2 << 16) | 2;
                b->p = p + 4;
            }
}
static void upload(int x, int y, int w, int h, const void *d);
#define T1 (*(volatile u16 *)0x1F801110)
static u32 lines_acc, lines_avg, rend_acc, rend_avg; static u16 t_rend0, t_pad, t_pal, t_l0, t_l1, t_end, t_cd0; static u32 l1_acc, l1_avg, all_acc, all_avg; static u32 pad_acc, pal_acc, pad_avg, pal_avg;
static int lastv;
void rc_vblank(void)
{
    u16 pad;
    int used, draw = 1;
    {   /* lineas usadas por el juego desde la ultima vuelta (temporizador 1 contando HBLANK) */
        u16 t = T1;
        used = (u16)(t - lastv);
    }
    pad = joy_read();                    /* el mando antes de esperar: si sobra tiempo no cuesta nada */
    dma_wait();                          /* el cuadro anterior ya esta dibujado */
#if FRAMESKIP
    if ((I_STAT & 1) && !skipped) draw = 0;  /* ya paso el vblank: este cuadro no se dibuja (nunca dos seguidos) */
    skipped = !draw; skips += !draw;
#endif
    if (draw) {
        vsync();
        GP1 = 0x05000000 | ((fb ? 256 : 0) << 10);   /* mostrar el cuadro anterior */
        fb ^= 1;
    } else I_STAT = ~1u;
    GP1 = 0x03000000;                    /* pantalla encendida (tambien si el primero se salta: barra de carga) */
    t_rend0 = T1;
    /* la MCU da una vuelta a su bucle principal por cuadro (contestacion 0xA53E, etc.) */
    mcu_run_c(&mcu);
    { void rc_oki_frame(void); void snd_frame(void); rc_oki_frame(); snd_frame(); }
    /* entradas */
    {
        u16 in = 0xFFFF;
        if (!(pad & 0x0010)) in &= ~0x01;            /* arriba */
        if (!(pad & 0x0040)) in &= ~0x02;            /* abajo */
        if (!(pad & 0x0020)) in &= ~0x04;            /* derecha */
        if (!(pad & 0x0080)) in &= ~0x08;            /* izquierda */
        if (!(pad & 0x4000)) in &= ~0x20;            /* X: acelerar */
        if (!(pad & 0x8000) && sq_last) gear ^= 1;   /* cuadrado: cambio de marcha */
        sq_last = (pad & 0x8000) != 0;
        if (!(pad & 0x0100) && l2_last) show_dbg ^= 1;  /* L2: cifras de rendimiento */
        l2_last = (pad & 0x0100) != 0;
        if (!gear) in &= ~0x10;
        if (!(pad & 0x0001)) in &= ~0x40;            /* SELECT: moneda */
        if (!(pad & 0x0008)) in &= ~0x4000;          /* START */
        io_in = in;
        rc_set_io(io_dsw, io_in);
    }
    if (!(nframe & 3)) eff_due = 1;
    t_pad = T1;
    if (draw) {                          /* dibujar el estado actual en el otro bufer */
        const u16 *vr = (const u16 *)rc_vregs;
        u32 *pw = SPAD->pal_wf, m0 = 0, m1; int k;
        if (eff_due) { eff_due = 0; eff_update(); upload(256, CLUT_Y, 32, 1, eff_clut); }
        bk_init();
        t_l0 = T1;
        layer((const u32 *)(rc_vram + 0x2000), (vr[3] + 8) & 1023, (vr[2] + 16) & 511, BK_A, CLV(0), 1, BK_C, CLV(1), -1, 0);
        layer((const u32 *)rc_vram, (vr[1] + 12) & 1023, (vr[0] + 16) & 511, BK_B, CLV(1), 0, BK_D, CLV(2), BK_F, CLV(3));
        t_l1 = T1;
        m1 = sprites();
        t_end = T1;
        for (k = 0; k < 32; k++) if (pw[k]) { m0 |= 1u << k; pal_w[k] += pw[k]; pw[k] = 0; }   /* paletas de las capas usadas */
        if (pal_convert(m0, m1)) upload(0, CLUT_Y, 256, 16, clut);
        t_pal = T1;
        /* lineas de video por cuadro del juego (media de 32 cuadros; 263 = 60 por segundo) */
        lines_acc += used; pad_acc += (u16)(t_pad - t_rend0); pal_acc += (u16)(t_pal - t_end);
        l1_acc += (u16)(t_l1 - t_l0); all_acc += (u16)(t_end - t_l0);
        if ((nframe & 31) == 31) {
            lines_avg = lines_acc >> 5; pad_avg = pad_acc >> 5; pal_avg = pal_acc >> 5; l1_avg = l1_acc >> 5; all_avg = all_acc >> 5;
            lines_acc = pad_acc = pal_acc = l1_acc = all_acc = 0;
        }
        if (show_dbg) { digits(4, 4, lines_avg); digits(48, 4, rend_avg); digits(92, 4, cd_loads); digits(136, 4, tiles_up); digits(180, 4, pack_err);
            digits(4, 16, pad_avg); digits(48, 16, pal_avg); digits(92, 16, l1_avg); digits(136, 16, all_avg); digits(180, 16, skips); }
        pk_send(fb ? 256 : 0);
    }
    t_cd0 = T1;
    level_check();                       /* recorrido nuevo: paquetes del CD */
    cache_load();                        /* tiles que faltaban: del CD a la VRAM (para el cuadro siguiente) */
    frame_id++;
    nframe++;
    {
        u16 t = T1;
        rend_acc += (u16)(t_cd0 - t_rend0);
        if ((nframe & 31) == 0) { rend_avg = rend_acc >> 5; rend_acc = 0; }
        lastv = t;
    }
}

int main(void)
{
    int i;
    __asm__ volatile ("mtc0 $0, $12");
    *(volatile u32 *)0xFFFE0130 = 0x0001E988;     /* cache de instrucciones activada (como la BIOS) */
    I_MASK = 0;
    DPCR |= 0x800;
    GP1 = 0x00000000;
    GP1 = 0x04000002;
    GP1 = 0x08000041;
    GP1 = 0x06C70260;
    GP1 = 0x07040010;
    cache_init();
    tables_init();
    if (cd_init() == 0) {
        int sz, k; char nm[] = "LEVEL00.BIN";
        if (cd_find("TILES.BIN", &tiles_lba, &sz)) tiles_lba = -1;
        if (cd_find("RACE.BIN", &race_lba, &sz)) race_lba = -1; else race_sec = sz / 2048;
        for (k = 0; k < 12; k++) { nm[5] = '0' + k / 10; nm[6] = '0' + k % 10; if (cd_find(nm, &level_lba[k], &sz)) level_lba[k] = -1; else level_sec[k] = sz / 2048; }
        {   void snd_init(void *buf, int bufsize, int lba, int size); int sl;
            if (cd_find("SNDFX.BIN", &sl, &sz) == 0) snd_init(cdbuf, sizeof(cdbuf), sl, sz); }
    }
    joy_init();
    *(volatile u16 *)0x1F801114 = 0x0100;   /* temporizador 1: cuenta lineas */
    rc_mem_init();
    mcu.code = mcu_rom; mcu.xram = rc_ram;
    mcs51_reset(&mcu); mcs51_run(&mcu, 0x013F, 1000000);
    rc_r.a[7] = ((u32)*(u16 *)(rc_rom) << 16) | *(u16 *)(rc_rom + 2);
    rc_r.imask = 7;
    rc_r.cyc_next = CYC_FRAME;
    rc_run(((u32)*(u16 *)(rc_rom + 4) << 16) | *(u16 *)(rc_rom + 6));
    for (;;);
}

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
#define OTW 16384
static u32 ot[2][OTW];
static u32 *P, *pkend;            /* lista: un nodo (cabecera + palabras) por primitivo */
#define NODE(p, n) ((p)[0] = ((u32)(n) << 24) | ((u32)((p) + 1 + (n)) & 0xFFFFFF))
static void pk_begin(int b) { P = ot[b]; pkend = ot[b] + OTW - 64; }
static void pk_end(void) { P[0] = 0x00FFFFFF; }
static void pk_send(int b) { dma_wait(); gpu_wait(); D2_MADR = (u32)ot[b]; D2_BCR = 0; D2_CHCR = 0x01000401; }
static int vbl_count;
static void vsync(void) { I_STAT = ~1u; while (!(I_STAT & 1)); vbl_count++; }

/* paletas: 64 del juego (0x000-0x3FF) en 4 variantes: 0 opaca, 1 pen 0 transparente, 2 solo pens 1-7,
   3 solo pens 8-15 (para las prioridades por grupo de pens de la capa delantera). Se convierten solo las
   que cambian */
static u16 clut[4][64][16];
static u16 pal_prev[1024];
static int pal_first = 1;
static u16 c15(u16 v)
{
    u32 b = (v >> 8) & 15, r = (v >> 4) & 15, g = v & 15, c;
    c = ((r << 1) | (r >> 3)) | (((g << 1) | (g >> 3)) << 5) | (((b << 1) | (b >> 3)) << 10);
    return c ? c : 0x8000;
}
static int pal_convert(void)
{
    int p, k, ch = 0;
    const u16 *pr = (const u16 *)rc_pal;
    for (p = 0; p < 64; p++) {
        const u16 *q = pr + p * 16; u16 *o = pal_prev + p * 16;
        if (!pal_first) {
            for (k = 0; k < 16 && q[k] == o[k]; k++);
            if (k == 16) continue;
        }
        ch = 1;
        for (k = 0; k < 16; k++) {
            u16 c = c15(q[k]);
            o[k] = q[k];
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
static inline u32 tpage_s(int s) { int tp = s >> 8; return ((TEX_X + (tp % 10) * 64) >> 6) | (((tp / 10) & 1) << 4); }
#define UV_S(s) ((((((s) >> 4) & 15) * 16) << 8) | (((s) & 15) * 16))

/* tablas precalculadas: bits de pagina de textura por pagina de la cache, CLUT por variante y paleta */
static u32 tp_page[20], clutid[4][64];
static void tables_init(void)
{
    int p, v;
    for (p = 0; p < 20; p++) tp_page[p] = ((TEX_X + (p % 10) * 64) >> 6) | (((p / 10) & 1) << 4);
    for (v = 0; v < 4; v++) for (p = 0; p < 64; p++) clutid[v][p] = CLUTID(v, p);
}
/* un tile 16x16: sin espejo, sprite 0x7D (+E1); con espejo, cuadrilatero 0x2D con la ventana de textura (E2)
   ajustada al tile, para que U/V puedan ir de 31 a 15 sin salirse (las GPU antiguas no hacen espejo en los
   rectangulos). *win: si la ventana quedo puesta */
static inline u32 *emit_tile_s(u32 *p, int x, int y, u32 s, u32 clut, u32 fl, int *win, u32 semi)
{
    u32 tp = tp_page[s >> 8] | ((semi ? semi - 1 : 0) << 5), U = (s & 15) << 4, Vv = ((s >> 4) & 15) << 4;
    if (!fl) {
        if (*win) { NODE(p, 1); p[1] = 0xE2000000; p += 2; *win = 0; }
        NODE(p, 4);
        p[1] = 0xE1000400 | tp; p[2] = semi ? 0x7F000000 : 0x7D000000;
        p[3] = ((u32)(y & 0xFFFF) << 16) | (x & 0xFFFF);
        p[4] = (clut << 16) | (Vv << 8) | U;
        return p + 5;
    } else {
        u32 uL = (fl & 1) ? 31 : 0, uR = (fl & 1) ? 15 : 16, vT = (fl & 2) ? 31 : 0, vB = (fl & 2) ? 15 : 16;
        u32 x0 = x & 0xFFFF, x1 = (x + 16) & 0xFFFF, y0 = (u32)(y & 0xFFFF) << 16, y1 = (u32)((y + 16) & 0xFFFF) << 16;
        NODE(p, 10);
        p[1] = 0xE2000000 | 0x1E | (0x1E << 5) | ((U >> 3) << 10) | ((Vv >> 3) << 15);
        p[2] = semi ? 0x2F808080 : 0x2D808080;
        p[3] = y0 | x0; p[4] = (clut << 16) | (vT << 8) | uL;
        p[5] = y0 | x1; p[6] = (tp << 16) | (vT << 8) | uR;
        p[7] = y1 | x0; p[8] = (vB << 8) | uL;
        p[9] = y1 | x1; p[10] = (vB << 8) | uR;
        *win = 1;
        return p + 11;
    }
}
#define emit_tile(p, x, y, s, c, f, w) emit_tile_s(p, x, y, s, c, f, w, 0)
static u16 pal_w[32];
/* tiles visibles de cada capa: hueco | espejo<<13 | categoria<<15 | paleta<<16 | columna<<21 | fila<<26 */
static u32 vis[2][32 * 20]; static int nvis[2], vox[2], voy[2];
static void gather(int L, int sx, int sy)
{
    int tx0 = sx >> 4, ty0 = sy >> 4, i, j, n = 0;
    const u16 *m = (const u16 *)(rc_vram + L * 0x2000);
    vox[L] = -(sx & 15); voy[L] = -(sy & 15);
    for (j = 0; j < H / 16 + 2; j++) {
        const u16 *row = m + ((ty0 + j) & 31) * 128;
        for (i = 0; i < W / 16 + 2; i++) {
            int e = ((tx0 + i) & 63) * 2, sl;
            u32 d2 = row[e + 1];
            sl = tile_slot(row[e] & 0x3FFF);
            if (sl < 0) continue;
            pal_w[d2 & 0x1F]++;
            vis[L][n++] = (u32)sl | (((d2 >> 6) & 3) << 13) | (((d2 >> 5) & 1) << 15) | ((d2 & 0x1F) << 16) | ((u32)i << 21) | ((u32)j << 26);
        }
    }
    nvis[L] = n;
}
static int win_on;
static void layer_pass(int L, int cats, int v)
{
    u32 *p = P; int k, n = nvis[L], ox = vox[L], oy = voy[L], win = win_on;
    const u32 *cl = clutid[v], *vv = vis[L];
    if (p >= pkend) return;
    for (k = 0; k < n; k++) {
        u32 e = vv[k];
        if (!(cats & (1 << ((e >> 15) & 1)))) continue;
        p = emit_tile(p, ox + ((e >> 21) & 31) * 16, oy + ((e >> 26) & 31) * 16, e & 0x1FFF, cl[(e >> 16) & 0x1F], (e >> 13) & 3, &win);
    }
    P = p; win_on = win;
}
/* sprites de efecto (faros, sombras): en la placa cambian el banco de paleta (1-7) de lo que tapan; aqui se
   dibujan semitransparentes con dos CLUT (lo que el banco k suma y lo que resta, de media, sobre la paleta
   normal), en x 256 y 272 de la fila 240 */
static u16 eff_clut[2][16];
static int eff_any;
#define EFF_CLUT(i) ((u32)((CLUT_Y << 6) | (16 + (i))))
static u16 pal_w_unused[1];          /* tiles visibles de cada paleta (cuadro anterior): pesos del color de la luz */
static void eff_update(void)
{
    const u16 *pr = (const u16 *)rc_pal;
    int k, i, p, c;
    u32 tw = 0;
    for (p = 0; p < 32; p++) tw += pal_w[p];
    if (!tw) return;
    for (k = 1; k < 8; k++) {
        int d[3] = {0, 0, 0}, pos[3], neg[3];
        for (p = 0; p < 32; p++) {
            int wgt = pal_w[p], e[3] = {0, 0, 0};
            if (!wgt) continue;
            for (i = 0; i < 16; i++) {
                u16 a = pr[p * 16 + i], b = pr[k * 1024 + p * 16 + i];
                e[0] += (int)((b >> 4) & 15) - (int)((a >> 4) & 15);
                e[1] += (int)(b & 15) - (int)(a & 15);
                e[2] += (int)((b >> 8) & 15) - (int)((a >> 8) & 15);
            }
            for (c = 0; c < 3; c++) d[c] += e[c] * wgt;
        }
        /* media por color (4 bits) -> 5 bits */
        for (c = 0; c < 3; c++) { int v = d[c] * 3 / (int)(tw * 16); pos[c] = v > 0 ? (v > 31 ? 31 : v) : 0; neg[c] = v < 0 ? (-v > 31 ? 31 : -v) : 0; }
        eff_clut[0][8 + k] = (pos[0] | pos[1] << 5 | pos[2] << 10) ? (u16)(0x8000 | pos[0] | pos[1] << 5 | pos[2] << 10) : 0;
        eff_clut[1][8 + k] = (neg[0] | neg[1] << 5 | neg[2] << 10) ? (u16)(0x8000 | neg[0] | neg[1] << 5 | neg[2] << 10) : 0;
    }
    eff_clut[0][8] = eff_clut[1][8] = 0;
    for (i = 0; i < 8; i++) eff_clut[0][i] = eff_clut[1][i] = 0;
    for (p = 0; p < 32; p++) pal_w[p] = 0;
}
static void sprites_eff(int prio)
{
    int i, pass, win = win_on;
    u32 *p = P;
    const u16 *sp = (const u16 *)rc_spr;
    for (pass = 0; pass < 2; pass++)
        for (i = 3; i < (0x1000 - 6) / 2 && p < pkend; i += 4) {
            u16 w0 = sp[i], w2 = sp[i + 2], w3 = sp[i + 3];
            int sx = w2 & 0x3FF, sy = (240 - (w0 & 0xFF)) & 0xFF, num = w3 & 0x3FFF, s;
            if (!(w2 & 0x4000)) continue;
            if ((num >= 0x3700) != prio) continue;
            sx = ((sx - 15 - 8) & 0x3FF); if (sx >= 0x3F0) sx -= 0x400;
            sy = sy - 16;
            if (sx >= W || sy >= H || sx <= -16 || sy <= -16) continue;
            s = tile_slot(num);
            if (s < 0) continue;
            p = emit_tile_s(p, sx, sy, s, EFF_CLUT(pass), (w0 >> 14) & 3, &win, pass ? 3 : 2);
        }
    P = p; win_on = win;
}
/* sprites de prioridad prio (0 = bajo la parte alta de la capa delantera, 1 = encima de todo) */
static void sprites(int prio)
{
    int i, win = win_on;
    u32 *p = P;
    const u16 *sp = (const u16 *)rc_spr;
    for (i = 3; i < (0x1000 - 6) / 2 && p < pkend; i += 4) {
        u16 w0 = sp[i], w2 = sp[i + 2], w3 = sp[i + 3];
        int sx = w2 & 0x3FF, sy = (240 - (w0 & 0xFF)) & 0xFF, num = w3 & 0x3FFF, color = (w2 >> 10) & 0x1F;
        int s;
        if (color & 0x10) continue;          /* sombras/luces: fase 4 */
        if ((num >= 0x3700) != prio) continue;
        sx = ((sx - 15 - 8) & 0x3FF); if (sx >= 0x3F0) sx -= 0x400;
        sy = sy - 16;
        if (sx >= W || sy >= H || sx <= -16 || sy <= -16) continue;
        s = tile_slot(num);
        if (s < 0) continue;
        p = emit_tile(p, sx, sy, s, clutid[1][32 + (color & 15)], (w0 >> 14) & 3, &win);
    }
    P = p; win_on = win;
}

/* ------------------------------------------------------------ mando (SIO0 directo) */
static void joy_init(void) { JOY_CTRL = 0x40; JOY_BAUD = 0x88; JOY_MODE = 0x0D; JOY_CTRL = 0; }
static u8 joy_x(u8 b)
{
    int t;
    for (t = 0; t < 2000 && !(JOY_STAT & 1); t++);
    JOY_DATA = b;
    for (t = 0; t < 2000 && !(JOY_STAT & 2); t++);
    b = JOY_DATA;
    for (t = 0; t < 600 && !(JOY_STAT & 0x80); t++);   /* ACK */
    JOY_CTRL |= 0x10;
    return b;
}
static u16 joy_read(void)
{
    u16 r = 0xFFFF; u8 id; int t;
    JOY_CTRL = 0x1003;
    for (t = 0; t < 300; t++) __asm__ volatile ("nop");
    joy_x(0x01); id = joy_x(0x42);
    if ((id & 0xF0) == 0x40 || (id & 0xF0) == 0x70 || (id & 0xF0) == 0x50) {
        joy_x(0); r = joy_x(0); r |= joy_x(0) << 8;
    }
    JOY_CTRL = 0;
    return r;
}

static int fb, nframe, gear, sq_last, l2_last = 1, show_dbg = 0;
static const u16 font[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
static void digits(int x, int y, u32 v)
{
    int n, k, d[5];
    for (n = 0; n < 5; n++) { d[n] = v % 10; v /= 10; }
    for (n = 0; n < 5; n++)
        for (k = 0; k < 15; k++)
            if (font[d[4 - n]] & (1 << (14 - k))) {
                { u32 *p = P; NODE(p, 3); p[1] = 0x60FFFF00; p[2] = ((u32)(y + (k / 3) * 2) << 16) | (x + n * 8 + (k % 3) * 2); p[3] = (2 << 16) | 2; P = p + 4; }
            }
}
static void upload(int x, int y, int w, int h, const void *d);
static u16 t_cd0, t_cd1;
static u32 lines_acc, lines_avg, rend_acc, rend_avg; static u16 t_rend0, t_pad, t_pal, t_l0, t_l1, t_end; static u32 l1_acc, l1_avg, all_acc, all_avg; static u32 pad_acc, pal_acc, pad_avg, pal_avg;
static int lastv;
void rc_vblank(void)
{
    u16 pad;
    int fy, used;
    /* mostrar el cuadro anterior (dibujado por DMA mientras corria el juego) */
    {   /* lineas usadas por el juego desde la ultima vuelta (temporizador 1 contando HBLANK) */
        u16 t = *(volatile u16 *)0x1F801110;
        used = (u16)(t - lastv);
    }
    dma_wait();
    vsync();
    GP1 = 0x05000000 | ((fb ? 256 : 0) << 10);
    GP1 = 0x03000000;                    /* pantalla encendida */
    fb ^= 1;
    t_rend0 = *(volatile u16 *)0x1F801110;
    /* la MCU da una vuelta a su bucle principal por cuadro (contestacion 0xA53E, etc.) */
    mcu_run_c(&mcu);
    { void rc_oki_frame(void); void snd_frame(void); rc_oki_frame(); snd_frame(); }
    /* entradas */
    pad = joy_read();
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
    /* dibujar el estado actual en el otro bufer */
    fy = fb ? 256 : 0;
    t_pad = *(volatile u16 *)0x1F801110;
    if (pal_convert()) upload(0, CLUT_Y, 256, 16, clut);
    if (!(nframe & 3)) { eff_update(); upload(256, CLUT_Y, 32, 1, eff_clut); }
    t_pal = *(volatile u16 *)0x1F801110;
    pk_begin(fb);
    {
        u32 *p = P;
        NODE(p, 8);
        p[1] = 0xE3000000 | (fy << 10); p[2] = 0xE4000000 | ((fy + H - 1) << 10) | (W - 1); p[3] = 0xE5000000 | (fy << 11);
        p[4] = 0xE6000000; p[5] = 0xE2000000;
        p[6] = 0x02000000; p[7] = (u32)fy << 16; p[8] = (H << 16) | W;          /* fondo negro */
        P = p + 9; win_on = 0;
    }
    {   /* orden de la placa: fondo, frente cat. 0, fondo cat. 1, frente cat. 1 (pens 1-7), sprites,
           frente cat. 1 (pens 8-15), sprites de prioridad alta */
        const u16 *vr = (const u16 *)rc_vregs;
        int x1 = (vr[3] + 8) & 1023, y1 = (vr[2] + 16) & 511, x0 = (vr[1] + 12) & 1023, y0 = (vr[0] + 16) & 511;
        t_l0 = *(volatile u16 *)0x1F801110;
        gather(1, x1, y1); gather(0, x0, y0);
        t_l1 = *(volatile u16 *)0x1F801110;
        layer_pass(1, 3, 0);
        layer_pass(0, 1, 1);
        layer_pass(1, 2, 1);
        layer_pass(0, 2, 2);
        sprites_eff(0);
        sprites(0);
        layer_pass(0, 2, 3);
        sprites_eff(1);
        sprites(1);
    }
    /* lineas de video por cuadro del juego (media de 32 cuadros; 263 = 60 por segundo) */
    lines_acc += used;
    if ((nframe & 31) == 31) { lines_avg = lines_acc >> 5; lines_acc = 0; }
    pad_acc += (u16)(t_pad - t_rend0); pal_acc += (u16)(t_pal - t_pad);
    if ((nframe & 31) == 31) { pad_avg = pad_acc >> 5; pal_avg = pal_acc >> 5; pad_acc = pal_acc = 0; }
    if (win_on) { u32 *p = P; NODE(p, 1); p[1] = 0xE2000000; P = p + 2; win_on = 0; }
    t_end = *(volatile u16 *)0x1F801110;
    l1_acc += (u16)(t_l1 - t_l0); all_acc += (u16)(t_end - t_l0);
    if ((nframe & 31) == 31) { l1_avg = l1_acc >> 5; all_avg = all_acc >> 5; l1_acc = all_acc = 0; }
    if (show_dbg) { digits(4, 4, lines_avg); digits(48, 4, rend_avg); digits(92, 4, cd_loads); digits(136, 4, tiles_up); digits(180, 4, pack_err);
        digits(4, 16, pad_avg); digits(48, 16, pal_avg); digits(92, 16, l1_avg); digits(136, 16, all_avg); }
    pk_end();
    pk_send(fb);
    t_cd0 = *(volatile u16 *)0x1F801110;
    level_check();                       /* recorrido nuevo: paquetes del CD */
    cache_load();                        /* tiles que faltaban: del CD a la VRAM (para el cuadro siguiente) */
    t_cd1 = *(volatile u16 *)0x1F801110;
    frame_id++;
    nframe++;
    {
        u16 t = *(volatile u16 *)0x1F801110;
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

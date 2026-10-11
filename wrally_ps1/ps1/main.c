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
#define T1 (*(volatile u16 *)0x1F801110)
static u16 t_vbl;                                    /* temporizador 1 (lineas) en un vblank visto al momento */
/* espera al siguiente vblank (atendiendo al CD mientras tanto, salvo cerca del vblank: leer un sector tarda hasta
   ~11 lineas en psxprof, mas con un bus del CD lento, y retrasaria el cambio de bufer) y lo reconoce: la bandera que
   haya al volver a entrar es que ya paso otro. Cerca: de 32 lineas antes a 2 despues de donde toca, 263 por cuadro
   desde t_vbl, que se toma cuando la ultima vuelta del bucle no tardo (sin leer un sector: el vblank llego hace
   menos de 2 lineas) */
static void vsync(void)
{
    u16 tp = T1, ph; int late = 0;
    I_STAT = ~1u;
    while (!(I_STAT & 1)) {
        tp = T1; ph = (u16)(tp - t_vbl) % 263; late = 0;
        if (ph >= 2 && ph < 263 - 32) { cd_poll(); late = (u16)(T1 - tp) >= 2; }
    }
    if (!late) t_vbl = tp;
    I_STAT = ~1u; vbl_count++;
}

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

/* ---- cache de tiles en VRAM: 5120 huecos de un tile (20 paginas de textura de 256 tiles). Al arrancar se carga
   el paquete de menus, y de fondo el de carrera y el resto de menus. Al entrar en carrera (recorrido nuevo: puntero
   de la tabla de filas del juego en 0xFEC190; o el mismo de antes cuando el fondo vuelve a ser su mapa) se leen los
   sectores que falten del paquete del recorrido y del de carrera, que quedan fijos (y los menus, lo ultimo que se
   sustituye despues). Al salir (el fondo deja de ser el mapa) se sueltan y se vuelven a leer de fondo los paquetes
   de menus, que quedan fijos. Lo que falte se pide a TILES.BIN sin esperar y aparece cuando llega. Reemplazo por
   reloj con edad. */
#define NTS 5120
static u16 tslot[16384], slot_tile[NTS];
static u32 slot_age[NTS];
static int hand, frame_id = 10;
static u16 miss_list[512]; static int nmiss, miss_t0 = -1;   /* miss_t0: cuadro desde el que falta algo */
static u8 miss_mark[16384 / 8], miss_now[16384 / 8];          /* en la lista; pedidos en este cuadro */
static u32 cd_loads, tiles_up, pack_err;
static int tiles_lba = -1, level_lba[12], cur_level = -1, level_sec[12], in_race, race_cnt;
static u32 cur_lvl_ptr;
static const u32 level_ptr[12] = {0x1500a, 0x6d61e, 0x71628, 0x7d646, 0x75632, 0x8565a, 0x81650, 0x89664, 0x7963c, 0x8d66e, 0x91678, 0x95682};
static inline int tile_slot(u32 t)
{
    u32 s = tslot[t];
    if (s == 0xFFFF) {
        miss_now[t >> 3] |= 1 << (t & 7);
        if (!(miss_mark[t >> 3] & (1 << (t & 7))) && nmiss < 512) { miss_mark[t >> 3] |= 1 << (t & 7); miss_list[nmiss++] = t; }
        return -1;
    }
    slot_age[s] = frame_id;
    return (int)s;
}
/* huecos fijos: tiles de los paquetes del recorrido actual (o de los menus fuera de la carrera), que se sustituyen
   los ultimos; guardados: los de los paquetes que no tocan ahora pero si luego (los menus en carrera, el de
   carrera fuera), que se sustituyen antes que los fijos pero despues de lo demas. min_cls: clase minima que queda
   en este cuadro (evita recorrer la cache entera en cada tile cuando esta llena) */
static u8 slot_pin[NTS / 8], slot_low[NTS / 8];
#define PINNED(s) (slot_pin[(s) >> 3] & (1 << ((s) & 7)))
#define LOW(s) (slot_low[(s) >> 3] & (1 << ((s) & 7)))
static int min_cls, min_cls_frame = -1;
static void cache_init(void)
{
    int i;
    for (i = 0; i < 16384; i++) tslot[i] = 0xFFFF;
    for (i = 0; i < NTS; i++) { slot_tile[i] = 0xFFFF; slot_age[i] = 0; }
    for (i = 0; i < NTS / 8; i++) slot_pin[i] = slot_low[i] = 0;
}
/* orden de sustitucion: 0 libre o suelto sin usar en 2 s, 1 guardado sin usar en 2 s, 2 fijo sin usar en 10 s,
   3 suelto o guardado, 4 fijo; nunca lo usado en este cuadro o el anterior, ni en carrera lo fijo (el mapa del
   recorrido y los sprites del coche: el arco de la meta, p. ej., solo sale al principio y al final del tramo) */
static inline int slot_cls(int h)
{
    s32 age = frame_id - slot_age[h];
    if (slot_tile[h] == 0xFFFF) return 0;
    if (age < 2) return 9;
    if (!PINNED(h)) return age >= 120 ? (LOW(h) ? 1 : 0) : 3;
    return in_race ? 9 : age >= 600 ? 2 : 4;
}
#define NEXT(h) ((h) + 1 == NTS ? 0 : (h) + 1)
/* desde la mano, el primero de clase <= min_cls, o el mejor; maxc: clase maxima. Los de clase 0 se buscan en toda
   la cache (saltando los grupos de 8 fijos); si no hay, el mejor de un cuarto de la cache. Dentro de un cuadro las
   clases solo suben: min_cls dice desde que clase vale la pena buscar */
static int slot_alloc(int maxc)
{
    int n, h, c, best = -1, bc = 9;
    if (min_cls_frame != frame_id) { min_cls = 0; min_cls_frame = frame_id; }
    if (min_cls > maxc) return -1;
    if (!min_cls) {
        for (n = 0, h = hand; n < NTS; n++, h = NEXT(h)) {
            if (!(h & 7) && (slot_pin[h >> 3] | slot_low[h >> 3]) == 0xFF) { n += 7; h += 7; continue; }
            if (!PINNED(h) && !slot_cls(h)) { hand = NEXT(h); return h; }
        }
        min_cls = 1;
        if (maxc < 1) return -1;
    }
    for (n = 0; n < NTS; n++) {
        h = hand; hand = NEXT(hand);
        c = slot_cls(h);
        if (c <= min_cls) return h;
        if (c < bc) { bc = c; best = h; }
        if (n >= NTS / 4 && bc < 9) break;
    }
    min_cls = bc;
    return bc <= maxc ? best : -1;
}
static void upload(int x, int y, int w, int h, const void *d);
/* pone el tile t (si no estaba) y lo marca como usado; pin: 1 queda fijo, 2 guardado; pin < 0: de paso (solo en
   un hueco libre o viejo, nunca de un fijo, ni de un guardado salvo en carrera, y como ya usado) */
static void tile_put(u32 t, const void *data, int pin)
{
    int s = tslot[t];
    if (s == 0xFFFF) {
        s = slot_alloc(pin < 0 ? in_race : 8);
        if (s < 0) return;
        if (slot_tile[s] != 0xFFFF) tslot[slot_tile[s]] = 0xFFFF;
        slot_tile[s] = t; tslot[t] = s; tiles_up++;
        slot_pin[s >> 3] &= ~(1 << (s & 7)); slot_low[s >> 3] &= ~(1 << (s & 7));
        upload(TEX_X + ((s >> 8) % 10) * 64 + (s & 15) * 4, ((s >> 8) / 10) * 256 + ((s >> 4) & 15) * 16, 4, 16, data);
    } else if (pin < 0) return;
    slot_age[s] = pin < 0 ? frame_id - 2 : frame_id;
    if (pin > 0) (pin == 1 ? slot_pin : slot_low)[s >> 3] |= 1 << (s & 7);
}
static u8 cdbuf[16 * 2048] __attribute__((aligned(4)));
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
static void t1_update(void);
static void load_bar(void)
{
    int b, w = load_total ? load_done * 256 / load_total : 0;
    t1_update();
    if (w > 256) w = 256;
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
/* ---- paquetes: sectores de cabecera (u16 n, u16 na, u16 tiles[n], ordenados, y na pares (tile, igual a): tiles sin
   datos porque son iguales a otro del paquete o del de carrera; en los de recorrido el bit 15 marca los tiles de su
   mapa) y luego los datos de los n, 16 tiles por sector. hs: sectores de cabecera (0 mientras no se hayan leido
   todos; hn los que son, hr los que ya se han leido en orden). pin: lo que llega queda fijo (1), guardado (2) o
   suelto (0) */
typedef struct { u16 *ids; int max, n, hs, pin, lba, ns, hn, hr; u16 *al; int maxal, nal; } pack_t;
static u16 lvl_ids[4000], race_ids[2000], menu_ids[1280], menu2_ids[4000], lvl_al[768 * 2], race_al[256 * 2];
static pack_t menu_pk = {menu_ids, 1280, 0, 0, 0, -1}, menu2_pk = {menu2_ids, 4000, 0, 0, 0, -1};
static pack_t lvl_pk = {lvl_ids, 4000, 0, 0, 0, -1, 0, 0, 0, lvl_al, 768}, race_pk = {race_ids, 2000, 0, 0, 0, -1, 0, 0, 0, race_al, 256};
static u8 lvl_map[16384 / 8];                       /* tiles del mapa del recorrido de lvl_pk */
/* los tiles del paquete que ya estan quedan fijos o guardados segun p->pin (use: y como recien usados) */
static void pack_pin(pack_t *p, int use)
{
    int i, s;
    u8 *m = p->pin == 2 ? slot_low : slot_pin;
    for (i = 0; i < p->n; i++) if ((s = tslot[p->ids[i]]) != 0xFFFF) { if (use) slot_age[s] = frame_id; m[s >> 3] |= 1 << (s & 7); }
}
static void pack_sector(pack_t *p, int i, const u8 *d)
{
    const u16 *w = (const u16 *)d;
    int k, j;
    if (!i) {
        p->hn = (4 + w[0] * 2 + w[1] * 4 + 2047) / 2048; p->hs = p->hr = 0;
        p->n = w[0] <= p->max ? w[0] : 0; p->nal = w[1] <= p->maxal ? w[1] : 0;
        if (p == &lvl_pk) for (k = 0; k < 16384 / 32; k++) ((u32 *)lvl_map)[k] = 0;
    }
    if (i < p->hn) {
        if (i != p->hr) return;
        for (k = i ? 0 : 2; k < 1024; k++) {
            if ((j = i * 1024 + k - 2) < p->n) p->ids[j] = w[k] & 0x3FFF;
            else if ((j -= p->n) < p->nal * 2) p->al[j] = w[k];
            else break;
            if ((w[k] & 0x8000) && j >= 0) lvl_map[(w[k] >> 3) & 0x7FF] |= 1 << (w[k] & 7);
        }
        if (++p->hr == p->hn) p->hs = p->hn;
        return;
    }
    if (!p->hs) return;
    if (i == p->hs && p->pin) pack_pin(p, 1);          /* los que ya estan no se sustituyen durante la carga */
    i = (i - p->hs) * 16;
    for (k = 0; k < 16 && i + k < p->n; k++) tile_put(p->ids[i + k], d + k * 128, p->pin);
}
/* tiles iguales a otro (alias): en carrera usan el hueco de ese, que es fijo y no se sustituye; al salir se quitan */
static void alias_set(pack_t *p, int on)
{
    int i;
    for (i = 0; i < p->nal; i++) {
        u16 d = p->al[i * 2] & 0x3FFF, s = tslot[p->al[i * 2 + 1] & 0x3FFF];
        if (on) { if (tslot[d] == 0xFFFF && s != 0xFFFF) tslot[d] = s; }
        else if (tslot[d] != 0xFFFF && slot_tile[tslot[d]] != d) tslot[d] = 0xFFFF;
    }
}
/* sector s del paquete con algun tile que no este en la cache */
static int pack_need(pack_t *p, int s)
{
    int k = (s - p->hs) * 16, j;
    for (j = 0; j < 16 && k + j < p->n; j++) if (tslot[p->ids[k + j]] == 0xFFFF) return 1;
    return 0;
}
static int pack_missing(pack_t *p)
{
    int s, n = 0;
    for (s = p->hs; s < p->ns; s++) n += pack_need(p, s);
    return n;
}
/* sector del paquete con el tile t, o -1 */
static int pack_sec_of(const pack_t *p, int t)
{
    int a = 0, b = p->n - 1, m;
    while (a <= b) { m = (a + b) >> 1; if (p->ids[m] == t) return p->hs + m / 16; if (p->ids[m] < t) a = m + 1; else b = m - 1; }
    return -1;
}
static pack_t *run_pk; static int run_base;
static void run_cb(int i, const unsigned char *d) { pack_sector(run_pk, run_base + i, d); if (loading) { load_done++; load_bar(); } }
/* lee los sectores del paquete (desde from) que hacen falta, por tramos (los huecos cortos se leen de paso) */
static void pack_runs(pack_t *p, int from, int fast)
{
    int s, e, j;
    run_pk = p;
    for (s = from; s < p->ns; s = e + 1) {
        e = s;
        if (!pack_need(p, s)) continue;
        for (j = s + 1; j < p->ns && j - e <= 8; j++) if (pack_need(p, j)) e = j;
        run_base = s;
        if (cd_stream(p->lba + s, e - s + 1, run_cb, fast)) pack_err++; else cd_loads++;
    }
}
/* ---- trabajo del CD sin esperar (avanza en cd_poll; lo leido se sube al principio de los cuadros siguientes, con
   la GPU parada, como mucho UPF tiles por cuadro). Lo que falta en pantalla se pide a TILES.BIN de tramo en tramo,
   salvo que lo traiga el paquete de fondo que toca; si no falta nada, los paquetes de fondo, de su primer sector
   con algo que falte al ultimo, de una vez, por un anillo de 16 sectores. Mientras suena una frase de CD-DA no se
   lee, salvo que falten muchos tiles o desde hace rato: entonces se corta la frase (como mucho una vez cada 2 s) y
   se sigue cuando ya no falta nada */
#define UPF 64
static int job_on;                                  /* lectura en curso: 1 tiles (a cdbuf), 2 paquete de fondo (al anillo) */
static u16 job_t[256]; static int job_n, job_s0, job_s1, job_st, job_i, job_f; /* job_st: resultado aun por subir; job_i: por donde va; job_f: cuadro en que se pidio */
static int job_pf;                                  /* la lectura de tiles es de antemano (prefetch): lo que falte va antes */
static pack_t *bgq[4]; static int nbg, bg_from, bg_jump = -1, bg_err;     /* bg_from: siguiente sector de bgq[0] que mirar */
#define RING 16
static u8 ring[RING * 2048] __attribute__((aligned(4)));
static int bg_s0, bg_n, bg_got, ring_t;              /* lectura de bgq[0] desde bg_s0: sectores pedidos, llegados, tratados */
static int cut_end, cut_until;                       /* cuadro hasta el que el audio queda cortado como mucho, y hasta el que no se vuelve a cortar */
static int skipped;                                  /* este cuadro no se ha dibujado (rc_vblank) */
static u8 *ring_dst(int i) { return i - ring_t >= RING ? 0 : ring + (i & (RING - 1)) * 2048; }
static void miss_clear(void)
{
    int i;
    for (i = 0; i < nmiss; i++) miss_mark[miss_list[i] >> 3] = 0;
    for (i = job_i; i < job_n; i++) miss_mark[job_t[i] >> 3] = 0;
    if (job_on == 1) { cd_cancel(); job_on = 0; }
    nmiss = job_n = job_i = job_st = 0; miss_t0 = -1;
}
static void bg_stop(void) { if (job_on == 2) { cd_cancel(); job_on = 0; } nbg = bg_n = bg_got = ring_t = 0; bg_jump = -1; }
static void bg_add(pack_t *p) { if (p->lba >= 0 && p->ns > 0 && nbg < 4) { if (!nbg) bg_from = bg_err = 0; bgq[nbg++] = p; } }
static void bg_next(void) { int i; for (i = 1; i < nbg; i++) bgq[i - 1] = bgq[i]; nbg--; bg_from = bg_err = 0; bg_jump = -1; }
/* lo que toca leer de bgq[0]: [*a, *b). Sin la cabecera, todo lo que queda; si no, del primer sector con algo que
   falte desde bg_from (o desde el principio de los datos, si despues no queda nada) al ultimo */
static void bg_range(int *a, int *b)
{
    pack_t *p = bgq[0];
    int s, e;
    if (!p->hs) { *a = p->hr; *b = p->ns; return; }
    if (bg_from < p->hs) bg_from = p->hs;
    for (s = bg_from; s < p->ns && !pack_need(p, s); s++);
    if (s == p->ns) for (s = p->hs; s < p->ns && !pack_need(p, s); s++);
    for (e = p->ns - 1; e >= s && !pack_need(p, e); e--);
    *a = s; *b = e + 1;
}
/* de los que faltan se miran como mucho NMS, repartidos por la lista. El paquete de fondo (indice en bgq) que trae
   mas (al menos la cuarta parte), o -1; *ms: el primer sector de esos */
#define NMS 48
static int bg_covers(int *ms)
{
    int i, q, s, n, b, bq = -1, bn = 0, m = nmiss < NMS ? nmiss : NMS;
    for (q = 0; q < nbg; q++) {
        if (!bgq[q]->hs) continue;
        for (i = n = 0, b = 0x7FFF; i < m; i++) if ((s = pack_sec_of(bgq[q], miss_list[i * nmiss / m])) >= 0) { n++; if (s < b) b = s; }
        if (n > bn) { bn = n; bq = q; *ms = b; }
    }
    return bn * 4 >= m ? bq : -1;
}
/* si alguno llega pronto (en los 16 sectores siguientes) en la lectura de fondo en curso */
static int bg_ahead(void)
{
    int i, s, j = bg_s0 + cd_read_pos(), m = nmiss < NMS ? nmiss : NMS;
    for (i = 0; i < m; i++) if ((s = pack_sec_of(bgq[0], miss_list[i * nmiss / m])) >= j && s < j + 16 && s < bg_s0 + bg_n) return 1;
    return 0;
}
static void bg_start(void)
{
    pack_t *p = bgq[0];
    int a, b;
    bg_range(&a, &b);
    if (a >= b) { bg_next(); return; }
    bg_s0 = a; bg_n = b - a; bg_got = ring_t = 0;
    if (p->pin && p->hs) pack_pin(p, 0);
    if (cd_stream_async(p->lba + a, b - a, ring_dst)) { bg_n = 0; return; }
    job_on = 2; cd_poll();
}
static void cache_flush(void)
{
    u32 t0 = tiles_up;
    int st, k;
    /* tiles pedidos: primero los que faltaban, luego (de paso) sus vecinos */
    if (job_on == 1 && (st = cd_read_state())) { job_on = 0; job_st = st; job_i = 0; if (st > 0) cd_loads++; else pack_err++; }
    while (job_st && tiles_up - t0 < UPF) {
        if (job_i < job_n) {
            u16 t = job_t[job_i++];
            if (job_st > 0) tile_put(t, cdbuf + (t - job_s0 * 16) * 128, 0);
            miss_mark[t >> 3] &= ~(1 << (t & 7));
        } else if (job_st > 0 && (k = job_i++ - job_n) < (job_s1 - job_s0 + 1) * 16) tile_put(job_s0 * 16 + k, cdbuf + k * 128, -1);
        else job_st = 0;
    }
    /* paquete de fondo: lo que ha llegado al anillo, como mucho 4 sectores */
    if (!nbg) return;
    if (job_on == 2) {
        bg_got = cd_read_pos();
        if ((st = cd_read_state())) { job_on = 0; if (st > 0) cd_loads++; else bg_err++; }
    }
    for (k = 0; k < 4 && ring_t < bg_got && tiles_up - t0 < UPF; k++, ring_t++) pack_sector(bgq[0], bg_s0 + ring_t, ring + (ring_t & (RING - 1)) * 2048);
    if (job_on != 2 && bg_n && ring_t == bg_got) {      /* acabada (o cortada) y tratada */
        bg_from = bg_jump >= 0 ? bg_jump : bg_s0 + bg_got; bg_jump = -1; bg_n = bg_got = ring_t = 0;
        if (bg_err > 4) bg_next();
    }
}
/* de los tiles que faltan, cuantos se ven (sin la columna y la fila de mas, que solo se piden de antemano) */
static u8 vis_seen[16384 / 8]; static u16 vis_l[2 * 24 * 17 + 511];
#define VIS(t) { u32 b_ = 1 << ((t) & 7); if (tslot[t] == 0xFFFF && !(vis_seen[(t) >> 3] & b_)) { vis_seen[(t) >> 3] |= b_; vis_l[n++] = (t); } }
static int miss_vis(void)
{
    const u16 *vr = (const u16 *)rc_vregs;
    const u32 *q = (const u32 *)(rc_spr + 4);
    int L, i, j, k, n = 0;
    for (L = 0; L < 2; L++) {
        const u32 *m = (const u32 *)(rc_vram + (L ? 0x2000 : 0));
        int sx = L ? (vr[3] + 8) & 1023 : (vr[1] + 12) & 1023, sy = L ? (vr[2] + 16) & 511 : (vr[0] + 16) & 511;
        int nv = (W + 15 + (sx & 15)) >> 4, nf = (H + 15 + (sy & 15)) >> 4;
        for (j = 0; j < nf; j++) {
            const u32 *row = m + (((sy >> 4) + j) & 31) * 64;
            for (i = 0; i < nv; i++) { u32 t = row[((sx >> 4) + i) & 63] & 0x3FFF; VIS(t) }
        }
    }
    for (k = 0; k < 511; k++, q += 2) {                 /* los sprites que pide sprites() */
        u32 t = q[2] & 0x3FFF;
        if ((((q[1] >> 16) - 8) & 0x3FF) >= W + 15 || ((q[0] >> 16) & 0xFF) == 0xF0) continue;
        VIS(t)
    }
    for (i = 0; i < n; i++) vis_seen[vis_l[i] >> 3] = 0;
    return n;
}
/* en carrera, con el CD libre: los sectores de TILES.BIN de al lado de los sprites del jugador que se ven. Las poses
   del coche (girar, derrapar, volcar) y del haz de los faros van de 16 en 16 tiles en el orden en que se suceden: asi
   la siguiente ya esta cuando hace falta. pf_at: cuando se pidio cada sector (en cuadros / 8), para no repetirlo en 3 s */
static u8 pf_at[1024];
static int sec_missing(int s) { int k; if (s < 0 || s >= 1024) return 0; for (k = 0; k < 16; k++) if (tslot[s * 16 + k] == 0xFFFF) return 1; return 0; }
static void prefetch(void)
{
    const u32 *q = (const u32 *)(rc_spr + 4);
    int k, s, a, b, now = (frame_id >> 3) & 0xFF;
    for (k = 0; k < 511; k++, q += 2) {
        u32 t = q[2] & 0x3FFF;
        if ((((q[1] >> 16) - 8) & 0x3FF) >= W + 15 || ((q[0] >> 16) & 0xFF) == 0xF0) continue;
        if (!((t >= 0x2E00 && t < 0x3700) || (t >= 0x3BA4 && t < 0x3EF0))) continue;
        for (a = 0x7FFF, b = -1, s = (t >> 4) - 2; s <= (int)(t >> 4) + 2; s++)
            if (sec_missing(s) && (u8)(now - pf_at[s]) >= 23) { if (s < a) a = s; b = s; }
        if (b < 0) continue;
        for (s = a; s <= b; s++) pf_at[s] = now;
        job_n = job_i = 0; job_s0 = a; job_s1 = b; job_pf = 1;
        if (cd_read_async(tiles_lba + a, b - a + 1, cdbuf)) return;
        job_on = 1; job_f = frame_id; cd_poll();
        return;
    }
}
static void cache_load(void)
{
    int snd_cut(void), snd_cut_on(void); void snd_resume(void);
    int i, j, s0 = 0x7FFF, s1, urg, q, ms, au, nv;
    for (i = j = 0; i < nmiss; i++) {                   /* fuera los que ya estan y los que no se han pedido en este cuadro */
        u16 t = miss_list[i], b = 1 << (t & 7);
        if (tslot[t] != 0xFFFF || (!skipped && !(miss_now[t >> 3] & b))) { miss_mark[t >> 3] &= ~b; continue; }
        miss_now[t >> 3] &= ~b;
        miss_list[j++] = t;
        if ((t >> 4) < s0) s0 = t >> 4;
    }
    nmiss = j;
    if (!nmiss) miss_t0 = -1; else if (miss_t0 < 0) miss_t0 = frame_id;
    if (nmiss && job_st && job_i >= job_n && !job_pf) job_st = 0;  /* los vecinos que quedaban, ya no (lo leido de antemano si: puede ser lo que falta) */
    if (job_on == 1 && job_pf && nmiss) { cd_cancel(); job_on = 0; }
    if (job_on == 1) {          /* cdbuf ocupado; si una frase nueva la para, cortarla (si aun no suena, ya) */
        if (cd_audio() && !snd_cut_on() && (!cd_playing() || frame_id - job_f >= 6) && snd_cut()) cut_end = frame_id + 180;
        return;
    }
    if (job_st) return;
    /* seguir la frase cuando ya no falte nada ni queden paquetes de fondo (como mucho 3 s cortada); en carrera no se
       vuelve a cortar en 2 s, fuera en 0,5 s */
    if (snd_cut_on() && ((!nmiss && !nbg) || frame_id > cut_end)) { snd_resume(); cut_until = frame_id + (in_race ? 120 : 30); }
    /* fuera de la carrera una frase que aun no suena espera a los paquetes de fondo (los menus tras una carrera, como
       mucho 4 s): mejor empezar un poco tarde que cortarla luego */
    if (nbg && !in_race && cd_audio() && !cd_playing() && !snd_cut_on() && frame_id >= cut_until && snd_cut()) cut_end = frame_id + 240;
    if (nmiss && tiles_lba >= 0) {
        /* urgente: muchos en pantalla, o desde hace rato (en carrera antes). Con una frase sonando solo se mira lo
           que se ve (para cortarla) */
        au = cd_audio() && !snd_cut_on();
        nv = au && frame_id >= cut_until ? miss_vis() : nmiss;
        urg = nv >= 16 || (nv && frame_id - miss_t0 >= (in_race ? 6 : 10));
        /* si alguno llega en la lectura de fondo en curso, seguir con ella; si no, si los trae un paquete de fondo,
           que se lea ese empezando por ellos; si no, de TILES.BIN */
        if (job_on == 2 && bg_ahead()) {
            if (urg && au && frame_id >= cut_until && snd_cut()) cut_end = frame_id + 180;
            return;
        }
        q = bg_covers(&ms);
        if (job_on == 2) { bg_got = cd_read_pos(); cd_cancel(); job_on = 0; if (!q) bg_jump = ms; }
        if (q >= 0 && bg_n) return;                     /* antes, tratar lo que ya llego */
        if (au) {
            if (!urg || frame_id < cut_until || !snd_cut()) return;
            cut_end = frame_id + 180;
        }
        if (q >= 0) {
            if (q) { pack_t *t = bgq[0]; bgq[0] = bgq[q]; bgq[q] = t; bg_err = 0; }
            bg_from = ms; bg_start();
            return;
        }
        /* el primer tramo: hasta 16 sectores desde el mas bajo que falta */
        for (i = 0, s1 = s0; i < nmiss; i++) { int s = miss_list[i] >> 4; if (s > s1 && s < s0 + 16) s1 = s; }
        for (i = j = 0, job_n = 0; i < nmiss; i++) {
            u16 t = miss_list[i];
            if ((t >> 4) <= s1 && job_n < 256) job_t[job_n++] = t; else miss_list[j++] = t;
        }
        nmiss = j; job_s0 = s0; job_s1 = s1; job_i = 0; job_pf = 0;
        if (cd_read_async(tiles_lba + s0, s1 - s0 + 1, cdbuf)) { for (i = 0; i < job_n; i++) miss_mark[job_t[i] >> 3] = 0; job_n = 0; return; }
        job_on = 1; job_f = frame_id; cd_poll();
        return;
    }
    if (nbg && !job_on && !bg_n && !cd_audio()) bg_start();
    else if (in_race && !nmiss && !nbg && !job_on && !job_st && tiles_lba >= 0 && !cd_audio()) prefetch();
}
/* ---- carrera: entrar (paquetes del recorrido y de carrera fijos, los de menus guardados: se sustituyen antes que
   los fijos y despues de lo demas, y asi al salir hay menos que volver a leer) y salir (los de menus fijos y se
   vuelven a leer de fondo; el de carrera guardado) */
static void level_load(int k)
{
    int i, hs, fast, need, same = k == cur_level && lvl_pk.hs;
    alias_set(&lvl_pk, 0); alias_set(&race_pk, 0);
    for (i = 0; i < NTS / 8; i++) slot_pin[i] = slot_low[i] = 0;
    for (i = 0; i < NTS; i++) slot_age[i] = frame_id - 1000;
    min_cls_frame = -1; hand = 0;
    lvl_pk.pin = race_pk.pin = 1; menu_pk.pin = menu2_pk.pin = 2;
    pack_pin(&menu_pk, 0); pack_pin(&menu2_pk, 0);
    if (!race_pk.hs && race_pk.lba >= 0) {              /* la cabecera de carrera aun no se habia leido de fondo */
        hs = race_pk.ns < 2 ? race_pk.ns : 2;
        if (cd_read(race_pk.lba, hs, cdbuf, 0) == 0) for (i = 0; i < hs; i++) pack_sector(&race_pk, i, cdbuf + i * 2048);
    }
    pack_pin(&race_pk, 1);
    if (same) pack_pin(&lvl_pk, 1);
    need = pack_missing(&race_pk) + (same ? pack_missing(&lvl_pk) : level_sec[k]);
    if (need) {
        fast = need > 120;
        load_screen(need);
        /* el del recorrido entero de corrido (cabecera y datos), o lo que falte si ya estaba; del de carrera lo que falte */
        if (same) pack_runs(&lvl_pk, lvl_pk.hs, fast);
        else {
            lvl_pk.n = lvl_pk.hs = 0; lvl_pk.lba = level_lba[k]; lvl_pk.ns = level_sec[k];
            run_pk = &lvl_pk; run_base = 0;
            if (level_lba[k] < 0 || cd_stream(level_lba[k], level_sec[k], run_cb, fast)) pack_err++;
        }
        pack_runs(&race_pk, race_pk.hs, fast);
    }
    alias_set(&race_pk, 1); alias_set(&lvl_pk, 1);
    lvl_pk.pin = 0; race_pk.pin = 2;
    loading = 0;
}
static void t1_skip(void);
static void race_enter(int k)
{
    void snd_suspend(void), snd_resume(void);
    miss_clear(); bg_stop();                            /* cancelar lo pendiente antes de esperar al audio */
    snd_suspend();
    level_load(k);
    cd_speed(0);
    cur_level = k; in_race = 1; race_cnt = 0;
    t1_skip();                                          /* el tiempo de la carga no cuenta para el sonido */
    snd_resume();
}
static void race_exit(void)
{
    int i;
    in_race = 0; race_cnt = 0;
    alias_set(&lvl_pk, 0); alias_set(&race_pk, 0);
    for (i = 0; i < NTS / 8; i++) slot_pin[i] = slot_low[i] = 0;
    for (i = 0; i < NTS; i++) if (frame_id - slot_age[i] >= 2) slot_age[i] = frame_id - 1000;   /* lo de la carrera, viejo */
    min_cls_frame = -1;
    menu_pk.pin = menu2_pk.pin = 1;
    pack_pin(&menu_pk, 0); pack_pin(&menu2_pk, 0); pack_pin(&race_pk, 0);
    if (job_on == 1) {                                  /* los tiles que se estaban leyendo ya no son de esta pantalla */
        cd_cancel(); job_on = 0;
        for (i = 0; i < job_n; i++) miss_mark[job_t[i] >> 3] = 0;
        job_n = 0;
    }
    bg_stop(); bg_add(&menu_pk); bg_add(&menu2_pk);
}
/* de una muestra de 30 tiles del fondo visible, cuantos son del mapa del recorrido cargado */
static int map_score(void)
{
    const u16 *vr = (const u16 *)rc_vregs;
    const u32 *m = (const u32 *)(rc_vram + 0x2000);
    int sx = ((vr[3] + 8) & 1023) >> 4, sy = ((vr[2] + 16) & 511) >> 4, i, j, n = 0;
    for (j = 0; j < 15; j += 3) for (i = 0; i < 24; i += 4) {
        u32 t = m[((sy + j) & 31) * 64 + ((sx + i) & 63)] & 0x3FFF;
        n += (lvl_map[t >> 3] >> (t & 7)) & 1;
    }
    return n;
}
/* cada cuadro: recorrido nuevo (cambia el puntero), o entrar/salir de la carrera del recorrido cargado segun el fondo
   sea su mapa (2 cuadros seguidos para entrar; para salir 8, o 2 si falta mucho: ha cambiado la pantalla) */
static void level_check(void)
{
    u32 p = ((u32)*(u16 *)(rc_ram + 0x190) << 16) | *(u16 *)(rc_ram + 0x192);
    int k, sc;
    if (p != cur_lvl_ptr) {
        cur_lvl_ptr = p;
        for (k = 0; k < 12 && level_ptr[k] != p; k++);
        if (k < 12 && k != cur_level) { race_enter(k); return; }
    }
    if (cur_level < 0) return;
    sc = level_ptr[cur_level] == p ? map_score() : 0;
    if (in_race ? sc < 8 : sc >= 15) {
        if (++race_cnt >= (in_race ? (nmiss >= 16 ? 2 : 8) : 2)) { if (in_race) race_exit(); else race_enter(cur_level); }
    }
    else race_cnt = 0;
}
/* ---- arranque: tabla de pistas, efectos de sonido y paquete de menus, de una sola lectura si estan seguidos en
   el disco; luego, de fondo, el paquete de carrera y el resto de menus */
static int b_base, b_trk = -1, b_fx = -1, b_nfx;
static void boot_cb(int i, const unsigned char *d)
{
    void snd_tracks(const unsigned char *d), snd_fx_sector(int i, const unsigned char *d);
    int l = b_base + i;
    if (l == b_trk) snd_tracks(d);
    else if (l >= b_fx && l < b_fx + b_nfx) snd_fx_sector(l - b_fx, d);
    else if (l >= menu_pk.lba && l < menu_pk.lba + menu_pk.ns) pack_sector(&menu_pk, l - menu_pk.lba, d);
    load_done++; load_bar();
}
static void boot_read(int lba, int n) { if (lba >= 0 && n > 0) { b_base = lba; if (cd_stream(lba, n, boot_cb, 1)) pack_err++; } }
static void pk_find(pack_t *p, const char *nm) { int sz; if (cd_find(nm, &p->lba, &sz)) p->lba = -1; else p->ns = sz / 2048; }
static void boot_load(void)
{
    void snd_init(void);
    int sz, k; char nm[] = "LEVEL00.BIN";
    if (cd_find("TILES.BIN", &tiles_lba, &sz)) tiles_lba = -1;
    pk_find(&race_pk, "RACE.BIN");
    for (k = 0; k < 12; k++) { nm[5] = '0' + k / 10; nm[6] = '0' + k % 10; if (cd_find(nm, &level_lba[k], &sz)) level_lba[k] = -1; else level_sec[k] = sz / 2048; }
    if (cd_find("TRACKS.BIN", &b_trk, &sz)) b_trk = -1;
    if (cd_find("SNDFX.BIN", &b_fx, &sz)) b_fx = -1; else b_nfx = (sz + 2047) / 2048;
    pk_find(&menu_pk, "MENU.BIN"); pk_find(&menu2_pk, "MENU2.BIN");
    menu_pk.pin = menu2_pk.pin = 1; race_pk.pin = 2;
    snd_init();
    load_screen(1 + b_nfx + menu_pk.ns);
    GP1 = 0x05000000; GP1 = 0x03000000;          /* pantalla encendida con la barra de carga */
    if (b_trk >= 0 && b_fx == b_trk + 1 && menu_pk.lba == b_fx + b_nfx) boot_read(b_trk, menu_pk.lba + menu_pk.ns - b_trk);
    else { boot_read(b_trk, 1); boot_read(b_fx, b_nfx); boot_read(menu_pk.lba, menu_pk.ns); }
    loading = 0;
    bg_add(&race_pk); bg_add(&menu2_pk);
    cd_speed(0);
    t1_skip();
}
/* al principio de cada cuadro: avanzar el CD, subir los tiles que llegaron (la GPU esta parada) y llevar el tiempo
   del OKI en pasos de 1/60 s por el temporizador 1 (tiempo real, no cuadros del juego; OKI_CUADROS: como antes) */
static u16 t1_last; static u32 t1_acc;
static void t1_update(void) { u16 t = *(volatile u16 *)0x1F801110; t1_acc += (u16)(t - t1_last); t1_last = t; }
static void t1_skip(void) { t1_update(); t1_acc = 0; }
static void cd_frame(void)
{
    void rc_oki_tick(int n), rc_oki_frame(void), snd_frame(int n);
    int n;
    cd_poll();
    cache_flush();
    t1_update();
    n = t1_acc / 263; t1_acc -= n * 263;
#ifdef OKI_CUADROS
    rc_oki_frame(); snd_frame(1);
#else
    rc_oki_tick(n); snd_frame(n);
#endif
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
    void snd_spu(int n);
    u16 pad;
    int used, draw = 1;
    {   /* lineas usadas por el juego desde la ultima vuelta (temporizador 1 contando HBLANK) */
        u16 t = T1;
        used = (u16)(t - lastv);
    }
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
    pad = joy_read();                    /* el mando justo tras el vblank: la entrada mas reciente para el 68000 */
    t_rend0 = T1;
    /* la MCU da una vuelta a su bucle principal por cuadro (contestacion 0xA53E, etc.) */
    mcu_run_c(&mcu);
    cd_frame();                          /* CD (tiles que llegaron) y sonido */
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
    cd_poll();
    level_check();                       /* recorrido nuevo: paquetes del CD (con pantalla de carga) */
    cache_load();                        /* tiles que faltan: se piden al CD sin esperar */
    snd_spu(0);                          /* frases de la SPU que acaban de terminar: el juego las ve libres ya */
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
    joy_init();
    *(volatile u16 *)0x1F801114 = 0x0100;   /* temporizador 1: cuenta lineas */
    if (cd_init() == 0) boot_load();         /* el CD vigila sus respuestas con el temporizador 1 */
    rc_mem_init();
    mcu.code = mcu_rom; mcu.xram = rc_ram;
    mcs51_reset(&mcu); mcs51_run(&mcu, 0x013F, 1000000);
    rc_r.a[7] = ((u32)*(u16 *)(rc_rom) << 16) | *(u16 *)(rc_rom + 2);
    rc_r.imask = 7;
    rc_r.cyc_next = CYC_FRAME;
    rc_run(((u32)*(u16 *)(rc_rom + 4) << 16) | *(u16 *)(rc_rom + 6));
    for (;;);
}

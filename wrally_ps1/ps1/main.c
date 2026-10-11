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

/* ---- cache de tiles en VRAM: 5120 huecos de un tile (20 paginas de textura de 256 tiles). Al arrancar se carga
   el paquete de menus, y de fondo el de carrera y el resto de menus; al cambiar de recorrido (puntero de la
   tabla de filas del juego en 0xFEC190), los sectores que falten del paquete del recorrido y del de carrera, que
   quedan fijos (y los menus, lo ultimo que se sustituye despues). Lo que falte se pide a TILES.BIN sin esperar y
   aparece cuando llega. Reemplazo por reloj con edad. */
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
/* huecos fijos: tiles de los paquetes del recorrido actual (se sustituyen los ultimos). min_cls: clase minima
   que queda en este cuadro (evita recorrer la cache entera en cada tile cuando esta llena) */
static u8 slot_pin[NTS / 8];
#define PINNED(s) (slot_pin[(s) >> 3] & (1 << ((s) & 7)))
static int min_cls, min_cls_frame = -1;
static void cache_init(void)
{
    int i;
    for (i = 0; i < 16384; i++) tslot[i] = 0xFFFF;
    for (i = 0; i < NTS; i++) { slot_tile[i] = 0xFFFF; slot_age[i] = 0; }
    for (i = 0; i < NTS / 8; i++) slot_pin[i] = 0;
}
/* orden de sustitucion: 0 libre o suelto sin usar en 2 s, 1 suelto, 2 fijo sin usar en 10 s, 3 fijo; nunca lo
   usado en este cuadro o el anterior */
static inline int slot_cls(int h)
{
    s32 age = frame_id - slot_age[h];
    if (slot_tile[h] == 0xFFFF) return 0;
    if (age < 2) return 9;
    if (!PINNED(h)) return age >= 120 ? 0 : 1;
    return age >= 600 ? 2 : 3;
}
/* desde la mano: el primero de clase <= min_cls, o el mejor de un cuarto de la cache; maxc: clase maxima */
static int slot_alloc(int maxc)
{
    int n, best = -1, bc = 9;
    if (min_cls_frame != frame_id) { min_cls = 0; min_cls_frame = frame_id; }
    if (min_cls > maxc) return -1;
    for (n = 0; n < NTS; n++) {
        int h = hand, c; hand = hand + 1 == NTS ? 0 : hand + 1;
        c = slot_cls(h);
        if (c <= min_cls) return h;
        if (c < bc) { bc = c; best = h; }
        if (n >= NTS / 4 && bc < 9) break;
    }
    min_cls = bc;
    return bc <= maxc ? best : -1;
}
static void upload(int x, int y, int w, int h, const void *d);
/* pone el tile t (si no estaba) y lo marca como usado; pin: queda fijo; pin < 0: de paso (solo en un hueco
   libre o viejo, y como ya usado) */
static void tile_put(u32 t, const void *data, int pin)
{
    int s = tslot[t];
    if (s == 0xFFFF) {
        s = slot_alloc(pin < 0 ? 0 : 8);
        if (s < 0) return;
        if (slot_tile[s] != 0xFFFF) tslot[slot_tile[s]] = 0xFFFF;
        slot_tile[s] = t; tslot[t] = s; tiles_up++;
        slot_pin[s >> 3] &= ~(1 << (s & 7));
        upload(TEX_X + ((s >> 8) % 10) * 64 + (s & 15) * 4, ((s >> 8) / 10) * 256 + ((s >> 4) & 15) * 16, 4, 16, data);
    } else if (pin < 0) return;
    slot_age[s] = pin < 0 ? frame_id - 2 : frame_id;
    if (pin > 0) slot_pin[s >> 3] |= 1 << (s & 7);
}
static u8 cdbuf[16 * 2048] __attribute__((aligned(4)));
static u16 packids[4096], race_ids[2048], menu_ids[1280];
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
/* ---- paquetes: sectores de cabecera (u16 n, u16 tiles[n]) y luego los datos, 16 tiles por sector. Se leen de
   corrido (sin pausas ni busquedas) y se tratan sector a sector segun llegan */
typedef struct { u16 *ids; int max, n, hs, pin; } pack_t;
static pack_t menu_pk = {menu_ids, 1280}, lvl_pk = {packids, 4000}, race_pk = {race_ids, 2000};
/* los tiles del paquete que ya estan quedan fijos (use: y como recien usados) */
static void pack_pin(pack_t *p, int use)
{
    int i, s;
    for (i = 0; i < p->n; i++) if ((s = tslot[p->ids[i]]) != 0xFFFF) { if (use) slot_age[s] = frame_id; slot_pin[s >> 3] |= 1 << (s & 7); }
}
static void pack_sector(pack_t *p, int i, const u8 *d)
{
    const u16 *w = (const u16 *)d;
    int k;
    if (!i) { p->n = w[0] <= p->max ? w[0] : 0; p->hs = (2 + p->n * 2 + 2047) / 2048; }
    if (i < p->hs) {
        for (k = i ? 0 : 1; k < 1024; k++) if (i * 1024 + k - 1 < p->n) p->ids[i * 1024 + k - 1] = w[k];
        return;
    }
    if (i == p->hs && p->pin) pack_pin(p, 1);          /* los que ya estan no se sustituyen durante la carga */
    i = (i - p->hs) * 16;
    for (k = 0; k < 16 && i + k < p->n; k++) tile_put(p->ids[i + k], d + k * 128, p->pin);
}
/* sector s del paquete con algun tile que no este en la cache */
static int pack_need(pack_t *p, int s)
{
    int k = (s - p->hs) * 16, j;
    for (j = 0; j < 16 && k + j < p->n; j++) if (tslot[p->ids[k + j]] == 0xFFFF) return 1;
    return 0;
}
static pack_t *run_pk; static int run_base;
static void run_cb(int i, const unsigned char *d) { pack_sector(run_pk, run_base + i, d); if (loading) { load_done++; load_bar(); } }
/* lee los sectores del paquete (desde from) que hacen falta, por tramos (los huecos cortos se leen de paso) */
static void pack_runs(pack_t *p, int lba, int nsec, int from, int fast)
{
    int s, e, j;
    run_pk = p;
    for (s = from; s < nsec; s = e + 1) {
        e = s;
        if (!pack_need(p, s)) continue;
        for (j = s + 1; j < nsec && j - e <= 8; j++) if (pack_need(p, j)) e = j;
        run_base = s;
        if (cd_stream(lba + s, e - s + 1, run_cb, fast)) pack_err++; else cd_loads++;
    }
}
static int pack_missing(pack_t *p, int nsec)
{
    int s, n = 0;
    for (s = p->hs; s < nsec; s++) n += pack_need(p, s);
    return n;
}
/* ---- trabajo del CD sin esperar (avanza en cd_poll; lo leido se trata al principio del cuadro siguiente, con la
   GPU parada): primero los tiles que faltan, de TILES.BIN de tramo en tramo; si no falta nada, los paquetes que
   se leen de fondo despues de arrancar, de 16 en 16 sectores. No se lee mientras suena una frase de CD-DA */
static u16 job_t[256]; static int job_n, job_s0, job_s1, job_on;  /* job_on: 1 tiles, 2 paquete de fondo */
typedef struct { pack_t *p; int lba, n, next, err; } bg_t;
static bg_t bgq[2]; static int nbg;
static void miss_clear(void)
{
    int i;
    for (i = 0; i < nmiss; i++) miss_mark[miss_list[i] >> 3] = 0;
    if (job_on == 1) for (i = 0; i < job_n; i++) miss_mark[job_t[i] >> 3] = 0;
    nmiss = job_n = job_on = 0;
}
static void cache_flush(void)
{
    int i, st;
    if (!job_on || !(st = cd_read_state())) return;
    if (job_on == 2) {
        bg_t *b = &bgq[0];
        if (st > 0) { for (i = 0; i < job_n; i++) pack_sector(b->p, b->next + i, cdbuf + i * 2048); b->next += job_n; cd_loads++; }
        else if (++b->err > 4) b->next = b->n;
        if (b->next >= b->n) { bgq[0] = bgq[1]; nbg--; }
    } else {
        for (i = 0; i < job_n; i++) {
            u16 t = job_t[i];
            if (st > 0) tile_put(t, cdbuf + (t - job_s0 * 16) * 128, 0);
            miss_mark[t >> 3] &= ~(1 << (t & 7));
        }
        /* los vecinos de los sectores leidos, de paso si hay sitio (suelen ser del mismo grafico) */
        if (st > 0) for (i = job_s0 * 16; i < job_s1 * 16 + 16; i++) tile_put(i, cdbuf + (i - job_s0 * 16) * 128, -1);
        if (st > 0) cd_loads++; else pack_err++;
    }
    job_on = job_n = 0;
}
static void cache_load(void)
{
    int i, j, s0 = 0x7FFF, s1;
    if (job_on || cd_audio()) return;
    for (i = j = 0; i < nmiss; i++) {
        u16 t = miss_list[i];
        if (tslot[t] != 0xFFFF) { miss_mark[t >> 3] &= ~(1 << (t & 7)); continue; }
        miss_list[j++] = t;
        if ((t >> 4) < s0) s0 = t >> 4;
    }
    nmiss = j;
    if (!nmiss || tiles_lba < 0) {
        if (nbg) {
            bg_t *b = &bgq[0];
            job_n = b->n - b->next < 16 ? b->n - b->next : 16; job_on = 2;
            if (cd_read_async(b->lba + b->next, job_n, cdbuf)) job_on = job_n = 0; else cd_poll();
        }
        return;
    }
    /* el primer tramo: hasta 16 sectores desde el mas bajo que falta */
    for (i = 0, s1 = s0; i < nmiss; i++) { int s = miss_list[i] >> 4; if (s > s1 && s < s0 + 16) s1 = s; }
    for (i = j = 0, job_n = 0; i < nmiss; i++) {
        u16 t = miss_list[i];
        if ((t >> 4) <= s1 && job_n < 256) job_t[job_n++] = t; else miss_list[j++] = t;
    }
    nmiss = j; job_s0 = s0; job_s1 = s1; job_on = 1;
    if (cd_read_async(tiles_lba + s0, s1 - s0 + 1, cdbuf)) { miss_clear(); return; }
    cd_poll();
}
/* recorrido nuevo: paquete del recorrido y de carrera fijos; lo demas pasa a ser lo primero que se sustituye */
static void level_load(int k)
{
    int i, hs, fast, need;
    for (i = 0; i < NTS / 8; i++) slot_pin[i] = 0;
    for (i = 0; i < NTS; i++) slot_age[i] = frame_id - 1000;
    min_cls_frame = -1; hand = 0;
    lvl_pk.pin = 1; race_pk.pin = 1;
    if (!race_pk.n && race_lba >= 0) {                  /* la cabecera de carrera aun no se habia leido de fondo */
        hs = race_sec < 2 ? race_sec : 2;
        if (cd_read(race_lba, hs, cdbuf, 0) == 0) for (i = 0; i < hs; i++) pack_sector(&race_pk, i, cdbuf + i * 2048);
    }
    pack_pin(&menu_pk, 0);
    pack_pin(&race_pk, 1);
    need = pack_missing(&race_pk, race_sec);
    fast = level_sec[k] + need > 120;
    load_total = level_sec[k] + need;
    /* el del recorrido entero de corrido (cabecera y datos), del de carrera lo que falte */
    run_pk = &lvl_pk; run_base = 0; lvl_pk.n = 0;
    if (level_lba[k] < 0 || cd_stream(level_lba[k], level_sec[k], run_cb, fast)) pack_err++;
    pack_runs(&race_pk, race_lba, race_sec, race_pk.hs, fast);
    lvl_pk.pin = race_pk.pin = 0;
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
    {   void snd_suspend(void), snd_resume(void);
        snd_suspend();
        cd_cancel(); miss_clear(); nbg = 0;
        load_screen(level_sec[k]);
        level_load(k);
        loading = 0;
        cd_speed(0);
        snd_resume();
    }
}
/* ---- arranque: tabla de pistas, efectos de sonido y paquete de menus, de una sola lectura si estan seguidos en
   el disco; luego, de fondo, el paquete de carrera y el resto de menus */
static pack_t menu2_pk = {packids, 4000};
static int b_base, b_trk = -1, b_fx = -1, b_nfx, b_menu = -1, b_nmenu;
static void boot_cb(int i, const unsigned char *d)
{
    void snd_tracks(const unsigned char *d), snd_fx_sector(int i, const unsigned char *d);
    int l = b_base + i;
    if (l == b_trk) snd_tracks(d);
    else if (l >= b_fx && l < b_fx + b_nfx) snd_fx_sector(l - b_fx, d);
    else if (l >= b_menu && l < b_menu + b_nmenu) pack_sector(&menu_pk, l - b_menu, d);
    load_done++; load_bar();
}
static void boot_read(int lba, int n) { if (lba >= 0 && n > 0) { b_base = lba; if (cd_stream(lba, n, boot_cb, 1)) pack_err++; } }
static void bg_add(pack_t *p, int lba, int n) { if (lba >= 0 && n > 0) { bgq[nbg].p = p; bgq[nbg].lba = lba; bgq[nbg].n = n; bgq[nbg].next = bgq[nbg].err = 0; nbg++; } }
static void boot_load(void)
{
    void snd_init(void);
    int sz, k, m2 = -1, n2 = 0; char nm[] = "LEVEL00.BIN";
    if (cd_find("TILES.BIN", &tiles_lba, &sz)) tiles_lba = -1;
    if (cd_find("RACE.BIN", &race_lba, &sz)) race_lba = -1; else race_sec = sz / 2048;
    for (k = 0; k < 12; k++) { nm[5] = '0' + k / 10; nm[6] = '0' + k % 10; if (cd_find(nm, &level_lba[k], &sz)) level_lba[k] = -1; else level_sec[k] = sz / 2048; }
    if (cd_find("TRACKS.BIN", &b_trk, &sz)) b_trk = -1;
    if (cd_find("SNDFX.BIN", &b_fx, &sz)) b_fx = -1; else b_nfx = (sz + 2047) / 2048;
    if (cd_find("MENU.BIN", &b_menu, &sz)) b_menu = -1; else b_nmenu = sz / 2048;
    if (cd_find("MENU2.BIN", &m2, &sz)) m2 = -1; else n2 = sz / 2048;
    snd_init();
    load_screen(1 + b_nfx + b_nmenu);
    GP1 = 0x05000000; GP1 = 0x03000000;          /* pantalla encendida con la barra de carga */
    if (b_trk >= 0 && b_fx == b_trk + 1 && b_menu == b_fx + b_nfx) boot_read(b_trk, b_menu + b_nmenu - b_trk);
    else { boot_read(b_trk, 1); boot_read(b_fx, b_nfx); boot_read(b_menu, b_nmenu); }
    loading = 0;
    bg_add(&race_pk, race_lba, race_sec); bg_add(&menu2_pk, m2, n2);
    cd_speed(0);
}
/* al principio de cada cuadro: avanzar el CD, subir los tiles que llegaron (la GPU esta parada) y llevar el tiempo
   del OKI en pasos de 1/60 s por el temporizador 1 (tiempo real, no cuadros del juego; OKI_CUADROS: como antes) */
static u16 t1_last; static u32 t1_acc;
static void t1_update(void) { u16 t = *(volatile u16 *)0x1F801110; t1_acc += (u16)(t - t1_last); t1_last = t; }
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
    cd_frame();                          /* CD (tiles que llegaron) y sonido */
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
    cd_poll();
    level_check();                       /* recorrido nuevo: paquetes del CD (con pantalla de carga) */
    cache_load();                        /* tiles que faltan: se piden al CD sin esperar */
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

/* Conversion de video World Rally -> Neo Geo
   - capas de tiles 64x32 (16x16) como 21 columnas de sprites de 32 tiles (modo completo, envuelve en 512)
   - sprites del juego -> sprites Neo Geo de 1 tile
   - paleta xBRG_444 -> formato Neo Geo por tabla */
#include "ng.h"
#include "gen_cfg.h"
extern const u16 col_tab[4096];

#define GVRAM ((u16 *)0x100000)
#define GPAL  ((u16 *)0x104000)
#define GSPR  ((u16 *)0x109000)
#define VREGS ((vu16 *)0x108000)

#ifndef FULLW
#define FULLW 1                 /* 1: imagen completa (368 -> 322 px con reduccion horizontal 14/16) */
#endif
#if FULLW
#define NCOL 26
#define CROPX 8
#define HSHRINK 0x0C00          /* tiles de 13 px: 368 -> 299 px, centrado (cabe en la zona segura del televisor) */
#define TW 13
#define SX(u) ((s16)((((s16)(u)) * 13) >> 4) + 10)
#else
#define NCOL 21
#define CROPX 32
#define HSHRINK 0x0F00
#define TW 16
#define SX(u) ((s16)(u))
#endif
/* NCOL columnas a la vista por capa, en NSLOT ranuras (sprites): la columna c del mapa va siempre
   en la ranura c & 31 (64 columnas de mapa = 2 vueltas exactas, sin saltos al dar la vuelta);
   las ranuras sin columna a la vista se ocultan (altura 0: no cuentan en el limite por linea) */
#define NSLOT 32
#ifndef TAILN
#define TAILN 2
#endif
#define PREF 2              /* columnas precargadas (ocultas) a cada lado */
#define NWIN (NCOL + 2 * PREF)  /* ventana que se mantiene escrita (<= NSLOT). Con 2 ranuras de sobra,
                                   la camara que va y viene una columna no obliga a reescribir nada
                                   (con NCOL + 2*3 = 32 cada vaiven costaba una columna entera) */
#define SPR_L1 1            /* capa 1 (fondo) */
#define SPR_L0 (1 + NSLOT)  /* capa 0 */
/* objetos: dos juegos de ranuras por clase; uno se ve y el otro (oculto, altura 0) se rellena
   fuera del VBLANK. En el VBLANK solo se cambia la altura (SCB3) de los dos */
#define N0MAX 48
#define N1MAX 94
#define SPR_O0 (1 + 2 * NSLOT)          /* prioridad baja, juegos A y B */
#define SPR_HI (SPR_O0 + 2 * N0MAX)     /* capa 0, tiles de categoria 1 (pens 8-15) por encima de los objetos bajos */
#define SPR_O1 (SPR_HI + NSLOT)         /* prioridad alta, juegos A y B */
#define SPR_MAX (SPR_O1 + 2 * N1MAX)    /* 381: todos los sprites */
#define TILE_HI 0x4000          /* variante con solo pens 8-15 */
#define TILE_SH 0x8000          /* variante de sombra (damero) */
#define PAL_SH 64
#define CROPY 20

u8 tcol[2][NSLOT];     /* columna de tilemap que muestra cada ranura (0xff = ninguna: oculta) */
static u8 tfill[2][NSLOT];      /* columna que se esta escribiendo en cada ranura (0xff = ninguna) */
static u16 full_c0[2];          /* ventana para la que la capa esta completa (0xFFFF = no se sabe) */
static u8 fill_busy;            /* hay una escritura de columna en marcha (fuera del VBLANK) */
static u16 eff_tick;
u32 c1mask[64];          /* capa 0: un bit por fila con tile de categoria 1 */
/* constantes para el ensamblador */
const u16 vconst[9] = { SPR_L0 * 64, SPR_L1 * 64, SPR_HI * 64, BLANK_TILE, NCOL, (SPR_HI - SPR_L0) * 64, N0MAX, N1MAX, NSLOT };
extern const u8 tile_empty_tab[0x800];
#define TILE_EMPTY(c) (tile_empty_tab[((c) & 0x3FFF) >> 3] & (1 << ((c) & 7)))
u32 l0mask[64];          /* capa 0: un bit por fila con tile no vacio (las columnas vacias no ocupan sprites) */
/* direccion VRAM (SCB1, fila 0) de los sprites que muestran cada columna: [capa*64 + col], 0 = ninguno */
u16 cb1[128], cb2[128];
static void cb_set(u16 layer, u16 slot, u16 oldcol, u16 newcol)
{
    u16 base = ((layer ? SPR_L1 : SPR_L0) + slot) * 64, i;
    if (oldcol != 0xFF) {
        i = (layer << 6) + oldcol;
        if (cb1[i] == base) { cb1[i] = cb2[i]; cb2[i] = 0; }
        else if (cb2[i] == base) cb2[i] = 0;
    }
    if (newcol != 0xFF) {
        i = (layer << 6) + newcol;
        if (!cb1[i]) cb1[i] = base; else cb2[i] = base;
    }
}
/* estado por capa para escribir SCB3/SCB4 solo cuando cambian (en rafagas con VRAMMOD=1) */
static u16 lay_c0[2], lay_yv[2], lay_xb[2];
static u8 lay_force[2];

static s16 o0_x0, o0_x1, o0_y0, o0_y1;
static u16 hi_n0;
static u32 himask[64];
u8 hi_dirty;
u32 hi_rows;                     /* filas del mapa cuyas mascaras de capa 0 han cambiado */

u16 dbg_spr;
u16 dbg_c0[2];
#ifdef PROFB_C
#define PMB(n) (*(vu16 *)0x10BFF6 = (n))
#else
#define PMB(n) ((void)0)
#endif
#define dbg_mask (*(vu16 *)0x10BF00)   /* bit0 sin L1, bit1 sin L0, bit2 sin objetos, bit3 sin fix */

/* copia a la VRAM Neo Geo la entrada e (capa*0x800 + fila*64 + col) si su columna esta en pantalla;
   en la capa 0 tambien la variante "alta" (pens 8-15) */
extern const u16 alt1_tab[0x4000];
static void entry_put(u16 e)
{
    u16 layer = e >> 11, idx = e & 0x7FF, col = idx & 63, k;
    const u16 *t = GVRAM + (e << 1);
    u16 w0 = t[0] & 0x3FFF, d2 = t[1];
    u16 attr = ((16 + (d2 & 0x1F)) << 8) | ((d2 >> 6) & 3);
    if (layer && alt1_tab[w0]) { w0 = alt1_tab[w0]; attr += 0x7000; }   /* pen 0 opaco (capa 1) */
    if (!layer) {
        u32 bit = 1UL << (idx >> 6), o = c1mask[col];
        u32 nm = (d2 & 0x20) ? (o | bit) : (o & ~bit);
        if (nm != o) { c1mask[col] = nm; hi_dirty = 1; hi_rows |= bit; }
        o = l0mask[col];
        nm = TILE_EMPTY(w0) ? (o & ~bit) : (o | bit);
        if (nm != o) { l0mask[col] = nm; hi_dirty = 1; hi_rows |= bit; }
    }
    REG_VRAMMOD = 1;
    for (k = 0; k < 1; k++) {
        u16 slot = col & (NSLOT - 1);
        if (tcol[layer][slot] != col && tfill[layer][slot] != col) continue;
        REG_VRAMADDR = ((layer ? SPR_L1 : SPR_L0) + slot) * 64 + (idx >> 6) * 2;
        VW(w0);
        VW(attr);
        if (!layer) {
            REG_VRAMADDR = (SPR_HI + slot) * 64 + (idx >> 6) * 2;
            if (d2 & 0x20) { VW(w0 + TILE_HI); VW(attr); }
            else { VW(BLANK_TILE); VW(0); }
        }
    }
}

void video_mark(u32 a)        /* a = direccion de byte en VRAM (palabra escrita, long) */
{
    u16 w = (a - 0x100000) >> 1;
    u16 e0 = w >> 1, e1 = (w + 1) >> 1;
    entry_put(e0 & 0xFFF);
    if (e1 != e0) entry_put(e1 & 0xFFF);
}

#include "gdec.h"
/* escritura cifrada a VRAM del juego (camino lento: punteros o no alineada): descifra en sitio,
   reubica punteros y actualiza la VRAM Neo Geo. Se llama con las interrupciones desactivadas */
void vram_fix(u16 *p, u32 reloc)
{
    u32 a = (u32)p;
    u16 e0, e1, d0, d1;
    if (a - 0x100000 >= 0x4000) return;
    e0 = p[0]; e1 = p[1];
    d0 = dec_first[e0];
    d1 = gdec2(e0, e1);
    if (reloc) {
        u32 v = ((u32)d0 << 16) | d1;
        if (v - 0xFEC000 < 0x4000) v -= 0xEE0000;
        else if (v - 0x200000 < 0x4000) v = v - 0x200000 + 0x104000;
        else if (v - 0x440000 < 0x1000) v = v - 0x440000 + 0x109000;
        else if (v - 0x700000 < 0x100) v = v - 0x700000 + 0x10A000;
        d0 = v >> 16; d1 = v;
    }
    p[0] = d0;
    p[1] = d1;
    video_mark(a);
}

void col_write_asm(const u16 *src, u32 vaddr, u32 hiaddr, u32 *l0, u32 *c1);
/* escribe la columna col en la ranura (oculta). La ranura pasa a recibir ya los cambios de tiles
   de esa columna (tablas cb) para que no se pierda ninguno si el juego la toca mientras tanto
   (puede pasar: esto va con las interrupciones abiertas); solo al final se marca como suya
   (tcol), que es lo que permite mostrarla */
static void column_write(u16 layer, u16 slot, u16 col)
{
    u16 sr, old;
    u32 hb;
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    old = tcol[layer][slot];
    /* la ranura alta de la columna anterior esta vacia si esa columna no tiene tiles de categoria 1 */
    hb = (old != 0xFF && !c1mask[old]) ? 0x80000000UL : 0;
    cb_set(layer, slot, tcol[layer][slot], col);
    tcol[layer][slot] = 0xFF;
    full_c0[layer] = 0xFFFF;
    tfill[layer][slot] = col;
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    col_write_asm(GVRAM + (layer << 12) + (col << 1), ((layer ? SPR_L1 : SPR_L0) + slot) * 64,
                  layer ? 0 : ((SPR_HI + slot) * 64) | hb, &l0mask[col], &c1mask[col]);
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    tfill[layer][slot] = 0xFF;
    tcol[layer][slot] = col;
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
}

/* columna que el juego ha redibujado entera (copia de mapa en modo lote): se copia ya, como el
   original, que tambien cambia los tiles en el momento (se llama con las interrupciones cortadas) */
void col_refresh(u32 layer, u32 col)
{
    u16 slot = col & (NSLOT - 1), sr;
    u8 fb;
    u32 hb;
    /* comprobar la ranura y bloquear los rellenos desde el VBLANK, sin que este pueda colarse en
       medio: si no, el VBLANK podia dar la ranura a otra columna justo despues de la comprobacion
       y aqui se escribia encima la columna vieja (franjas de otra parte del mapa en pantalla) */
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    if (tcol[layer][slot] != col && tfill[layer][slot] != col) {
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
        return;
    }
    fb = fill_busy;
    hb = (tcol[layer][slot] == col && !c1mask[col]) ? 0x80000000UL : 0;   /* (alta vacia) */
    fill_busy = 1;                      /* que un relleno desde el VBLANK no use esta ranura a la vez */
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    col_write_asm(GVRAM + (layer << 12) + (col << 1), ((layer ? SPR_L1 : SPR_L0) + slot) * 64,
                  layer ? 0 : ((SPR_HI + slot) * 64) | hb, &l0mask[col], &c1mask[col]);
    fill_busy = fb;
    if (!layer) { hi_dirty = 1; hi_rows = 0xFFFFFFFFUL; }
}

void xburst(u32 vaddr, u32 xb, u32 s0, u32 n, u32 tw);
void hiburst(u32 yv, u32 hide, u32 vaddr, u32 win, const u32 *mask, u32 c0, const u8 *tab);
void himask_asm(u32 xs, u32 xa, u32 xz, u32 c0, u32 *himask);
/* Reglas (el hardware dibuja linea a linea; MAME no, por eso alli no se veia nada):
   - en el VBLANK solo se escriben alturas (SCB3) y posiciones (SCB4), ya calculadas antes;
   - una ranura solo se muestra si tiene ya la columna que le toca en pantalla; si no, se oculta;
   - las columnas (SCB1) se escriben en ranuras ocultas, fuera del VBLANK: se precargan PREF
     columnas a cada lado, asi al desplazarse la que entra ya esta escrita */
u32 vis_bits(const u8 *tab, u32 c0, const u32 *mask, u32 win);
void scb3_bits(u32 vaddr, u32 yv, u32 bits);
static u8 vis_dirty[2];
/* estado preparado (para el proximo VBLANK) y mostrado, por capa */
static u16 pr_c0[2], pr_xb[2], pr_yv[2];
static u32 pr_vis[2], pr_hivis, lay_vis[2], lay_hivis;
static u8 pr_ok, preparing, hi_xb_old;
static u16 l0v_c0; static u32 l0v_win; static u8 l0v_ok;
static u32 pr_win, lay_win;      /* filas en pantalla de la capa 0 (preparado / aplicado) */
/* calcula desplazamiento y que ranuras se ven (fuera del VBLANK si es posible) */
static void layer_prepare(u16 layer, u16 sx, u16 sy, const s16 *bb, u16 n0)
{
    u16 x0 = (sx + CROPX - 16) & 1023;     /* una columna antes: la imagen cubre todo el ancho */
    u16 c0 = x0 >> 4, fx = x0 & 15;
    u16 Y = (CROPY + sy) & 511, r0 = Y >> 4;
    u16 yv = ((((496 + Y) & 0x1FF) << 7) | 32), xb = (u16)SX(-(s16)fx - 16);
    u32 win = r0 ? ((0xFFFFUL << r0) | (0xFFFFUL >> (32 - r0))) : 0xFFFFUL;
    u8 hd;
    dbg_c0[layer] = c0 | (fx << 8);
    pr_c0[layer] = c0; pr_xb[layer] = xb; pr_yv[layer] = yv;
    if (layer) {
        u32 v;
        if (full_c0[1] == c0) {     /* completa: las NCOL ranuras de la ventana, sin recorrer */
            u16 r = c0 & (NSLOT - 1);
            v = (1UL << NCOL) - 1;
            v = r ? ((v << r) | (v >> (32 - r))) : v;
        } else v = vis_bits(tcol[1], c0, 0, 0);
        pr_vis[1] = (dbg_mask & 1) ? 0 : v;
        return;
    }
    /* lo mismo que el cuadro anterior si no cambio nada (ventana, filas, mascaras, ranuras) */
    /* (se borra antes: un cambio de mascara durante el calculo vuelve a marcarlo). Solo cuentan
       las filas que estan en pantalla: las que el juego escribe por fuera no cambian nada */
    pr_win = win;
    {
        u16 sr;
        __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
        hd = (hi_rows & win) != 0; hi_rows &= ~win; hi_dirty = hi_rows != 0;
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    }
    if (!(full_c0[0] == c0 && c0 == l0v_c0 && win == l0v_win && !hd && l0v_ok)) {
        pr_vis[0] = (dbg_mask & 2) ? 0 : vis_bits(tcol[0], c0, l0mask, win);
        l0v_c0 = c0; l0v_win = win; l0v_ok = full_c0[0] == c0;
    }
    /* capa alta (tiles de categoria 1 por encima de los objetos bajos): solo donde hay objetos bajos */
    pr_hivis = 0;
    if (!(dbg_mask & 16) && n0) {
        u16 ra = ((bb[2] + Y) & 511) >> 4, nr = ((bb[3] - bb[2]) >> 4) + 2;
        u32 hwin = win;
        if (nr < 32) {
            u32 rows = (1UL << nr) - 1;
            rows = ra ? ((rows << ra) | (rows >> (32 - ra))) : rows;
            hwin &= rows;
        }
        himask_asm((s16)xb, bb[0] - TW, bb[1], c0, himask);
        pr_hivis = vis_bits(tcol[0], c0, himask, hwin);
    }
}
/* VBLANK: aplica lo preparado (solo SCB3/SCB4) */
static void layer_apply(u16 layer)
{
    u16 c0 = pr_c0[layer], xb = pr_xb[layer], yv = pr_yv[layer], force = lay_force[layer];
    u16 base = layer ? SPR_L1 : SPR_L0;
    if (xb != lay_xb[layer] || c0 != lay_c0[layer] || force) {
        xburst(0x8400 + base, xb, c0 & (NSLOT - 1), NSLOT, TW);
        if (!layer) hi_xb_old = 1;
    }
    /* la capa alta solo se recoloca si se ve (casi siempre esta oculta) */
    if (!layer && hi_xb_old && (pr_hivis || lay_hivis)) {
        xburst(0x8400 + SPR_HI, xb, c0 & (NSLOT - 1), NSLOT, TW);
        hi_xb_old = 0;
    }
    if (pr_vis[layer] != lay_vis[layer] || yv != lay_yv[layer] || force) scb3_bits(0x8200 + base, yv, pr_vis[layer]);
    if (!layer && (pr_hivis != lay_hivis || ((yv != lay_yv[0] || force) && (pr_hivis || force)))) scb3_bits(0x8200 + SPR_HI, yv, pr_hivis);
    lay_xb[layer] = xb; lay_c0[layer] = c0; lay_yv[layer] = yv; lay_vis[layer] = pr_vis[layer];
    if (!layer) lay_win = pr_win;
    if (!layer) lay_hivis = pr_hivis;
    lay_force[layer] = 0;
}
/* una columna que falte (en pantalla primero, luego las precargadas) en una ranura oculta.
   Atomico respecto al VBLANK. Devuelve 0 si la capa esta completa */
static u16 scan_c0[2], scan_k[2];
u16 layer_fill_one(u16 arg)
{
    /* arg: capa; bit 8 = solo si falta una columna en pantalla (desde el VBLANK: las precargadas
       se escriben en el tiempo libre del bucle principal, sin quitarle tiempo al juego) */
    u16 layer = arg & 1, urgent = arg & 0x100;
    u16 sr, k, c0, slot = 0, col = 0, vis = 0, found = 0;
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    /* ventana de referencia: la preparada para el proximo VBLANK si la hay (sus ranuras visibles
       ya estan comprobadas y no se tocan); si el salto es grande se espera a que se aplique */
    c0 = lay_c0[layer];
    if (preparing || fill_busy) goto out;   /* (fill_busy: el VBLANK ha interrumpido otro relleno) */
    if (pr_ok) {
        s16 d = (s16)(((pr_c0[layer] - c0) + 32) & 63) - 32;
        if (d > PREF || d < -PREF) goto out;
        c0 = pr_c0[layer];
    }
    if (full_c0[layer] == c0) goto out;     /* ya completa para esta ventana */
    /* (las anteriores a scan_k ya estaban comprobadas para esta misma ventana: solo pasan a no
       tener columna en layer_catchup y al reiniciar, que borran esto) */
    for (k = scan_c0[layer] == c0 ? scan_k[layer] : 0; k < NWIN; k++) {
        /* orden: pantalla (0..NCOL-1), luego derecha y luego izquierda */
        s16 o = k < NCOL + PREF ? (s16)k : (s16)(NCOL + PREF - 1) - (s16)k;
        col = (c0 + o) & 63; slot = col & (NSLOT - 1);
        if (tcol[layer][slot] == col) continue;
        scan_c0[layer] = c0; scan_k[layer] = k;
        vis = k < NCOL;
        if (urgent && !vis) goto out;
        found = 1;
        fill_busy = 1;
        break;
    }
    if (!found) full_c0[layer] = c0;
out:
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    if (!found) return 0;
    /* la ranura esta oculta (no tiene su columna): se escribe por tramos con las interrupciones
       abiertas y solo al final pasa a tener la columna nueva */
    column_write(layer, slot, col);
    if (vis) vis_dirty[layer] = 1;     /* hay que volver a calcular que ranuras se ven */
    fill_busy = 0;
    return 1;
}

/* salto grande (cambio de seccion, recolocacion tras un choque, capa reiniciada): faltan muchas
   columnas de la ventana nueva. En vez de irlas rellenando de dos en dos durante varios cuadros
   (se veian huecos con el color de fondo), se escriben todas ya, en el bucle principal y antes de
   publicar el cuadro: asi el VBLANK siguiente muestra la escena entera, como el original (que
   tiene un solo cuadro de fondo). Las ranuras que se van a reescribir se ocultan antes */
static u8 vbl_seq, in_catchup;
static void layer_catchup(u16 layer, u16 sx)
{
    u16 x0 = (sx + CROPX - 16) & 1023, c0 = x0 >> 4;
    u16 k, n = 0, sr;
    u32 bits = 0;
    u8 cols[NSLOT];
    {   /* desplazamiento normal (una columna como mucho): las precargadas bastan, no hace falta mirar */
        s16 d = (s16)(((c0 - lay_c0[layer]) + 32) & 63) - 32;
        if (full_c0[layer] == c0) return;
        if (!lay_force[layer] && d >= -1 && d <= 1 && full_c0[layer] == lay_c0[layer]) return;
    }
    for (k = 0; k < NSLOT; k++) cols[k] = 0xFF;
    for (k = 0; k < NWIN; k++) {
        s16 o = k < NCOL + PREF ? (s16)k : (s16)(NCOL + PREF - 1) - (s16)k;
        u16 col = (c0 + o) & 63, slot = col & (NSLOT - 1);
        if (tcol[layer][slot] == col) continue;
        if (k < NCOL) n++;
        cols[k] = col; bits |= 1UL << slot;
    }
    if (n < 3) return;                  /* lo normal: los rellenos de siempre bastan */
    /* las ranuras a reescribir dejan de tener columna (se ocultan en el VBLANK siguiente, nunca
       a mitad de imagen) y se espera a ese VBLANK antes de escribirlas */
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    if (fill_busy) { __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory"); return; }
    fill_busy = 1;
    for (k = 0; k < NSLOT; k++)
        if (bits & (1UL << k)) { cb_set(layer, k, tcol[layer][k], 0xFF); tcol[layer][k] = 0xFF; }
    scan_c0[layer] = 0xFFFF;
    pr_vis[layer] &= ~bits;
    if (!layer) { pr_hivis = 0; hi_dirty = 1; hi_rows = 0xFFFFFFFFUL; }
    full_c0[layer] = 0xFFFF;
    vis_dirty[layer] = 1;
    k = vbl_seq;
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    in_catchup = 1;
    while (*(volatile u8 *)&vbl_seq == (u8)k) ;
    in_catchup = 0;
    for (k = 0; k < NSLOT; k++)
        if (cols[k] != 0xFF) column_write(layer, cols[k] & (NSLOT - 1), cols[k]);
    vis_dirty[layer] = 1;
    if (!layer) { hi_dirty = 1; hi_rows = 0xFFFFFFFFUL; }
    fill_busy = 0;
}

u16 pal_update_part(u16 part);
/* revision de paletas en el tiempo libre: una parte (8 paletas) por llamada; las 6 en cada cuadro.
   El juego rota colores en cada cuadro (degradado de la flecha) y tras un fundido no restaura todas
   las paletas en el mismo cuadro: revisando una parte por VBLANK se veian saltos y colores viejos.
   (Idempotente: si el VBLANK la interrumpe y revisa la misma parte, el resultado es el mismo) */
static volatile u8 pal_done;
/* el juego acaba de cambiar todas las paletas de golpe (fundido): se aplican ya, fuera del VBLANK */
volatile u8 pal_force;   /* el juego ha cargado paletas enteras en este cuadro (trampolines) */
volatile u8 pal_evt;     /* fundido en curso (desde 0xB01E hasta la carga de paletas): sin revisiones a medias */
static u8 pal_wait, pal_go, pal_staged, pal_post;
static u8 conv_age;    /* VBLANK desde el ultimo frame_convert (crece fuera de la carrera) */
static u16 fc_vr[4];   /* desplazamiento del ultimo frame_convert */
static u8 scr_dark, rv_wait;   /* pantalla en negro / cuadros esperando para encender la nueva */
void pal_copy_dirty(void);
u16 *pal_stg_addr(u16 p);
extern u16 pal_cache[];
void pal_mark(u16 p);
extern volatile u8 pal_cvp;
static volatile u8 pal_now;     /* copiar ya en el VBLANK siguiente (fundido del juego, 0xB026) */
static u8 pal_rs, pal_rt;               /* revision completa de una paleta por cuadro (se corrige sola) */
/* las paletas convertidas se preparan en RAM (fast.s: pal_stg) y el VBLANK copia las marcadas.
   Nada toca la RAM de paletas del Neo Geo con la imagen activa: en hardware real y en MiSTer cada
   acceso de la CPU pinta el pixel que se esta dibujando con el color de esa direccion */
static void pal_conv_one(u16 p)
{
    const u16 *g = (const u16 *)0x104000 + p * 16;
    u16 *c = pal_cache + p * 16, i;
    u16 *d;
    pal_cvp = p;
    d = pal_stg_addr(p);
    for (i = 0; i < 16; i++) { u16 v = g[i]; c[i] = v; d[i] = col_tab[v & 0xFFF]; }
    pal_mark(p);
    pal_cvp = 0xFF;
}
/* convierte las 48 paletas del juego (fuera del VBLANK: son ~50000 ciclos) */
static void pal_stage_build(void)
{
    u16 p;
    for (p = 0; p < 48; p++) pal_conv_one(p);
}
/* efectos y color de fondo: se calculan donde sea y se escriben en el VBLANK */
static u16 eff_col[8], bd_col;
static volatile u8 eff_d[8], bd_d;
static void pal_small_flush(void)
{
    u16 k;
    if (bd_d) { bd_d = 0; BACKDROP = bd_col; }
    for (k = 1; k < 8; k++) if (eff_d[k]) { eff_d[k] = 0; PALRAM[PAL_SH * 16 + k] = eff_col[k]; }
}
static void backdrop_calc(void);
static void effect_bank(u16 k, u16 smooth);
void pal_sync_now(void)
{
    u16 p;
    for (p = 0; p < 6; p++) pal_update_part(p);
    pal_now = 1;        /* (se aplica en el VBLANK siguiente, todas a la vez) */
    /* el color de fondo (asoma por los pixeles transparentes de las dos capas) y las paletas de
       efecto (sombras, faros) salen de estas paletas: ya, sin suavizar (si no, en un fundido a
       blanco se veian contornos del color anterior y la sombra del coche) */
    backdrop_calc();
    for (p = 1; p < 8; p++) effect_bank(p, 0);
}
u16 pal_idle_step(void)
{
    u16 p;
    if (pal_evt || pal_force || pal_now) return 0;
    for (p = 0; p < 6; p++)
        if (!(pal_done & (1 << p))) { pal_update_part(p); pal_done |= 1 << p; return 1; }
    /* y una paleta entera por cuadro aunque no haya cambiado: si algo dejara una paleta Neo Geo
       distinta de la del juego, se corrige sola en menos de un segundo */
#ifndef NORS
    if (!(pal_done & 0x40)) {
        pal_done |= 0x40;
        if (++pal_rt & 3) return 0;     /* (una de cada cuatro vueltas: 48 paletas en ~3 s) */
        if (++pal_rs >= 48) pal_rs = 0;
        pal_conv_one(pal_rs);
        return 1;
    }
#endif
    return 0;
}

void video_layer_reset(u16 layer, u16 cat1)
{
    u16 i;
    for (i = 0; i < NSLOT; i++) tcol[layer][i] = 0xFF;      /* se ocultan en el VBLANK y se rellenan */
    scan_c0[layer] = 0xFFFF;
    full_c0[layer] = 0xFFFF;
    for (i = 0; i < 64; i++) cb1[(layer << 6) + i] = cb2[(layer << 6) + i] = 0;
    lay_force[layer] = 1;
    vis_dirty[layer] = 1;
    if (!layer) {
        hi_dirty = 1; hi_rows = 0xFFFFFFFFUL;
        for (i = 0; i < 64; i++) { c1mask[i] = cat1 ? 0xFFFFFFFFUL : 0; l0mask[i] = TILE_EMPTY(GVRAM[0]) ? 0 : 0xFFFFFFFFUL; }
    }
}

void spr_begin(u16 *r0, u16 *r1);
void spr_add(const u16 *list, u32 n);
u32 spr_end(void);
void spr_back(u32 n, u32 slot, const u16 *rec);
void spr_show(u32 n, u32 slot, const u16 *rec);
void spr_hide(u32 n, u32 slot);
void spr_bbox(const u16 *rec, u32 n, s16 *out);
extern u16 rec0[400], rec1[828];
/* juego de buffers B en la RAM libre 0x108100-0x108A97 (la pila propia empieza en 0x108AC0) */
#define REC0B ((u16 *)0x108100)
#define REC1B ((u16 *)0x108420)
static u16 spr_n0, spr_n1, spr_h0, spr_h1, vis_h0, vis_h1;
static u16 *spr_hs0, *spr_hs1;             /* registros del cuadro publicado (pendiente de mostrar) */
volatile u8 spr_ready; static u8 pend_set, vis_set;
static u16 vis_n0, vis_n1;
static u16 pend_vr[4], cur_vr[4];
static u8 vr_age;
static s16 pend_bb[4];
static u16 *setrec0(u16 set) { return set ? REC0B : rec0; }
static u16 *setrec1(u16 set) { return set ? REC1B : rec1; }
#define WRAM(a) (*(vu16 *)(0x10C000 + ((a) - 0xFEC000)))
/* conversion de las listas de sprites del juego al juego de ranuras oculto (tiles y x) y
   publicacion junto con el desplazamiento de las capas de ese mismo cuadro; el VBLANK siguiente
   solo cambia las alturas. Si ya hay un cuadro publicado sin mostrar, este se salta (no se puede
   escribir el juego oculto que va a mostrarse) */
#define L1A ((const u16 *)0x10D600)
#define L2A ((const u16 *)0x10DDF8)
#define L3A ((const u16 *)0x10E5C8)
/* la tercera lista (marcador y textos, ~40 entradas) casi nunca cambia de un cuadro a otro: se
   guarda lo que produjo y, si llega igual, se copia en vez de volver a convertirla */
void spr_break(void);
extern u16 *sc_p0, *sc_p1, *sc_l0b, *sc_l1b;
extern u16 sc_n0, sc_n1;
#define HC_MAX 48
#define HC_IN ((u32 *)0x109BB0)         /* 48 entradas (384 bytes) */
#define HC_O0 ((u16 *)0x109D30)         /* 128 bytes */
#define HC_O1 ((u16 *)0x109DB0)         /* 512 bytes (hasta 0x109FB0) */
static u16 hc_n, hc_w0, hc_w1, hc_r0, hc_r1;
/* registros del marcador de este cuadro: van al final de cada juego de ranuras (siempre en las
   mismas) y, si son los mismos que ya se escribieron en ese juego, no se vuelven a escribir */
static u16 hud_r0, hud_r1, hud_gen, set_hgen[2];
static u16 *hud_s0, *hud_s1;
static u8 hc_ok;
u16 lcmp(const void *a, const void *b, u32 nl);      /* 1 si los nl longs son iguales */
static u8 hud_in;                      /* los registros del marcador estan en el buffer del cuadro */
extern u32 *sc_map;
static u16 hc_map[HC_MAX];              /* spr_add deja aqui donde puso el tile de cada entrada (parte baja de la
                                           direccion) y luego pasa a ser su posicion en HC_O0 / HC_O1 (bit 15:
                                           clase 1; 0xFFFF: ninguna) */
static u8 hc_mok;
/* el marcador suele cambiar solo en algun digito (el tile de 1-3 entradas, misma posicion): en vez
   de convertir las ~40 entradas otra vez se corrige ese tile en los registros guardados */
static u16 hud_patch(const u16 *l, u16 n)
{
    const u16 *o = (const u16 *)HC_IN;
    u16 i, k = 0;
    if (!hc_mok) return 0;
    for (i = 0; i < n; i++, l += 4, o += 4) {
        if (l[3] == o[3] && l[0] == o[0] && l[1] == o[1] && l[2] == o[2]) continue;
        if (l[0] != o[0] || l[1] != o[1] || l[2] != o[2]) return 0;
        if (((l[3] & 0x3FFF) >= 0x3700) != ((o[3] & 0x3FFF) >= 0x3700)) return 0;
        k++;
    }
    if (!k) return 0;
    for (l -= 4 * n, o -= 4 * n, i = 0; i < n; i++, l += 4, o += 4) {
        u16 m;
        if (l[3] == o[3]) continue;
        m = hc_map[i];
        if (m != 0xFFFF) {
            u16 *t = (m & 0x8000 ? HC_O1 : HC_O0) + (m & 0x7FFF);
            *t = *t - (o[3] & 0x3FFF) + (l[3] & 0x3FFF);
        }
        ((u16 *)o)[3] = l[3];
    }
    return 1;
}
static void hud_add(const u16 *l, u16 n)
{
    const u32 *a = (const u32 *)l;
    u16 i, *p0, *p1, r0, r1, w0, w1;
    spr_break();
    if (hc_ok && hc_n == n && sc_n0 + hc_r0 <= N0MAX && sc_n1 + hc_r1 <= N1MAX &&
        (lcmp(l, HC_IN, n * 2) || (hud_patch(l, n) && (hud_gen++, 1)))) {
        /* igual que el cuadro anterior (o corregido): se muestran directamente los registros guardados */
        hud_s0 = HC_O0; hud_s1 = HC_O1; hud_r0 = hc_r0; hud_r1 = hc_r1; hud_in = 0;
        return;
    }
    p0 = sc_p0; p1 = sc_p1; r0 = sc_n0; r1 = sc_n1;
    if (n <= HC_MAX) sc_map = (u32 *)hc_map;
    spr_add(l, n);
    sc_map = 0;
    spr_break();
    w0 = sc_p0 - p0; w1 = sc_p1 - p1; r0 = sc_n0 - r0; r1 = sc_n1 - r1;
    hud_s0 = p0; hud_s1 = p1; hud_r0 = r0; hud_r1 = r1; hud_in = 1; hud_gen++;
    hc_ok = 0;
    if (n <= HC_MAX && w0 <= 64 && w1 <= 256 && sc_n0 < N0MAX && sc_n1 < N1MAX &&
        sc_p0 < sc_l0b && sc_p1 < sc_l1b) {      /* (sin llegar a ningun limite: salida completa) */
        u32 *b = HC_IN; const u32 *s; u32 *d;
        for (i = n * 2; i; i--) *b++ = *a++;
        for (s = (const u32 *)p0, d = (u32 *)HC_O0, i = w0 >> 1; i; i--) *d++ = *s++;
        for (s = (const u32 *)p1, d = (u32 *)HC_O1, i = w1 >> 1; i; i--) *d++ = *s++;
        hc_n = n; hc_w0 = w0; hc_w1 = w1; hc_r0 = r0; hc_r1 = r1; hc_ok = 1;
        hc_mok = 1;
        for (i = 0; i < n; i++) {
            u16 *t = (u16 *)(0x100000UL | hc_map[i]);
            if (!hc_map[i]) hc_map[i] = 0xFFFF;
            else if (t >= p0 && t < p0 + w0) hc_map[i] = t - p0;
            else if (t >= p1 && t < p1 + w1) hc_map[i] = 0x8000 | (t - p1);
            else hc_mok = 0;
        }
    }
}
/* registros de la lista 1 ya construidos por l1_build (fast.s) en la IRQ del juego */
extern u8 st_ok, l1_need, l1_age;
extern u16 st_n0, st_n1, st_w0, st_w1;
#define ST_BUF0 ((const u16 *)0x109800)
#define ST_BUF1 ((const u16 *)0x107E00)   /* (ver fast.s) */
void lcopyl(void *d, const void *s, u32 nl);
extern s16 st_bb[4];
#define QH 8                            /* cabecera de la copia en cola: w0, n0, w1, n1, rectangulo */
static u16 stg_w0, stg_n0;              /* registros de la lista 1 copiados en este cuadro (al principio de b0) */
static s16 stg_bb[4];
/* stm: 0 = lista 1 del juego (l1, c1); 1 = registros de l1_build (se copian con las IRQ cortadas,
   la del juego los rehace); 2 = copia en cola: qs = [w0, n0, w1, n1, x0, x1, y0, y1, datos0, datos1] */
static void stg_add(u16 stm, const u16 *qs)
{
    u16 sr = 0, w0, w1, n0, n1;
    const u16 *s0, *s1;
    if (stm == 1) {
        __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
        w0 = st_w0; n0 = st_n0; w1 = st_w1; n1 = st_n1; s0 = ST_BUF0; s1 = ST_BUF1;
        stg_bb[0] = st_bb[0]; stg_bb[1] = st_bb[1]; stg_bb[2] = st_bb[2]; stg_bb[3] = st_bb[3];
    } else {
        w0 = qs[0]; n0 = qs[1]; w1 = qs[2]; n1 = qs[3]; s0 = qs + QH; s1 = s0 + w0;
        stg_bb[0] = qs[4]; stg_bb[1] = qs[5]; stg_bb[2] = qs[6]; stg_bb[3] = qs[7];
    }
    if (sc_n0 + n0 > N0MAX) w0 = n0 = 0;            /* (no deberia pasar) */
    if (sc_n1 + n1 > N1MAX) w1 = n1 = 0;
    lcopyl(sc_p0, s0, w0 >> 1); sc_p0 += w0; sc_n0 += n0;
    lcopyl(sc_p1, s1, w1 >> 1); sc_p1 += w1; sc_n1 += n1;
    if (stm == 1) __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    stg_w0 = w0; stg_n0 = n0;
}
static void convert_publish(const u16 *vr, u16 stm, const u16 *qs, const u16 *l1, u16 c1, const u16 *l2, u16 c2, const u16 *l3, u16 c3)
{
    u16 *b0, *b1, sr, set, n0, n1, h0, h1;
    u32 r;
    s16 bb[4];
    if (spr_ready) return;
    PM(16);
    set = vis_set ^ 1;
    b0 = setrec0(set); b1 = setrec1(set);
    spr_begin(b0, b1);
    hud_r0 = hud_r1 = 0; hud_in = 1; stg_w0 = stg_n0 = 0;
    if (!(dbg_mask & 4)) {
        if (stm) stg_add(stm, qs);
        else if (c1) spr_add(l1, c1);
        if (c2) spr_add(l2, c2);
        if (c3) hud_add(l3, c3);
    }
    r = spr_end();
    n0 = r >> 16; n1 = r;
    h0 = hud_r0; h1 = hud_r1;
    if (!hud_in) { n0 += h0; n1 += h1; }  /* (los guardados no estan en el buffer) */
    spr_back(n1 - h1, SPR_O1 + set * N1MAX, b1);
    spr_back(n0 - h0, SPR_O0 + set * N0MAX, b0);
    if (set_hgen[set] != hud_gen) {
        spr_back(h1, SPR_O1 + set * N1MAX + N1MAX - h1, hud_s1);
        spr_back(h0, SPR_O0 + set * N0MAX + N0MAX - h0, hud_s0);
        set_hgen[set] = hud_gen;
    }
    spr_bbox(b0 + stg_w0, n0 - h0 - stg_n0, bb);     /* (los de la lista 1 ya traen su rectangulo) */
    if (stg_n0) {
        if (stg_bb[0] < bb[0]) bb[0] = stg_bb[0];
        if (stg_bb[1] > bb[1]) bb[1] = stg_bb[1];
        if (stg_bb[2] < bb[2]) bb[2] = stg_bb[2];
        if (stg_bb[3] > bb[3]) bb[3] = stg_bb[3];
    }
    if (h0) {
        s16 hb[4];
        spr_bbox(hud_s0, h0, hb);
        if (hb[0] < bb[0]) bb[0] = hb[0];
        if (hb[1] > bb[1]) bb[1] = hb[1];
        if (hb[2] < bb[2]) bb[2] = hb[2];
        if (hb[3] > bb[3]) bb[3] = hb[3];
    }
    bb[1] += TW;
    /* capas del mismo cuadro: que ranuras se ven, ya calculado (en el VBLANK solo se aplica) */
    preparing = 1;
    layer_prepare(1, vr[3], vr[2], bb, n0);
    layer_prepare(0, vr[1] + 4, vr[0], bb, n0);
    vis_dirty[0] = vis_dirty[1] = 0;
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    pr_ok = 1;
    preparing = 0;
    pend_set = set;
    spr_n0 = n0; spr_n1 = n1; spr_h0 = h0; spr_h1 = h1; spr_hs0 = hud_s0; spr_hs1 = hud_s1;
    pend_vr[0] = vr[0]; pend_vr[1] = vr[1]; pend_vr[2] = vr[2]; pend_vr[3] = vr[3];
    pend_bb[0] = bb[0]; pend_bb[1] = bb[1]; pend_bb[2] = bb[2]; pend_bb[3] = bb[3];
    spr_ready = 1;
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    PM(11);
}
static u8 converting, conv_busy;   /* conv_busy: convert_publish en curso desde el bucle principal */     /* cuadros convertidos (o encolados) por el bucle principal sin confirmar por la IRQ */
/* cola de un cuadro: si el juego termina un cuadro cuando el anterior aun no se ha mostrado (va
   recuperando tras un cuadro largo), se copian sus listas de sprites y su desplazamiento y se
   convierten en la IRQ siguiente, justo despues de mostrar el anterior. Asi se ven todos los
   cuadros, uno por VBLANK, sin saltarse ninguno (eso era el temblor) y sin parar al juego.
   Buffer: la RAM de sprites original del juego (0x440000 -> 0x109000), que ya no se usa */
#define QBUF(i) ((u16 *)(0x109000 + (i) * 0x400))
#define QMAX 128                        /* entradas de 8 bytes por cuadro (2 x 1024 bytes: 0x109000-0x109800) */
#define QDEPTH 2
volatile u8 q_cnt; static volatile u8 q_head, q_tail;        /* cola de QDEPTH cuadros (maximo 2) */
u16 dbg_fid, dbg_shown; static u16 pend_fid, q_fid[2];
static u16 q_vr[2][4], q_c1[2], q_c2[2], q_c3[2], q_stm[2];
static u16 *q_l2[2];
static void lcopy(u16 *d, const u16 *s, u16 n)  /* n entradas de 4 palabras */
{
    const u32 *a = (const u32 *)s; u32 *b = (u32 *)d;
    while (n--) { b[0] = a[0]; b[1] = a[1]; a += 2; b += 2; }
}
/* llamada desde el bucle principal del juego al terminar cada cuadro (antes de esperar al
   VBLANK, 0x32B8): convierte ya los sprites y calcula el desplazamiento que la IRQ del juego
   pondra en los registros (rutinas 0x2F98, 0x43E2 y 0x4440). Asi el siguiente VBLANK muestra
   este cuadro sin retraso, a la vez que los tiles que el juego acaba de escribir */
void frame_convert(void)
{
    s16 c13e = WRAM(0xFEC13E), c140 = WRAM(0xFEC140), c142 = WRAM(0xFEC142), c144 = WRAM(0xFEC144);
    u16 vr[4], c1, c2, c3, sr, stm;
#ifdef PROFB_C
    *(vu16 *)0x10BFF4 = 1;              /* (marca para medir en MAME) */
#endif
#ifndef NOLAGFIX
    /* listas de sprites de ESTE cuadro: el juego las construye en su IRQ (0x457C, 0x46E4, 0x488E)
       a partir de los buffers de objetos que el bucle acaba de cerrar. Si se esperaba a la IRQ,
       las listas eran a veces de este cuadro y a veces del anterior (segun hubiera pasado o no
       una IRQ antes de llegar aqui), con el desplazamiento siempre de este: el coche y los
       objetos se adelantaban y retrasaban unos pixeles respecto a la carretera (temblor).
       Se construyen ya; los constructores marcan el buffer como gastado y la IRQ no los repite */
    {
        u16 s0;
        __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(s0) : : "memory");
        l1_need = 1; l1_age = 0;
        __asm__ volatile ("movem.l %%d0-%%d7/%%a0-%%a6,-(%%sp)\n\t"
                          "jsr l1_build\n\t"
                          "jsr 0x46E4\n\t"
                          "jsr 0x488E\n\t"
                          "movem.l (%%sp)+,%%d0-%%d7/%%a0-%%a6" : : : "memory", "cc");
        __asm__ volatile ("move.w %0,%%sr" : : "d"(s0) : "memory");
    }
#endif
    if (!(WRAM(0xFEF250) & 1)) {        /* btst #0,$FEF251 (byte bajo) */
        u16 x = WRAM(0xFEC244), y = WRAM(0xFEC252);
        if (WRAM(0xFEC1B4) & 0x40) x += x;
        x = (x & 1023) + 12; c142 = -(s16)x; c13e = -(s16)(x + 2);
        y = (y & 511) + 32; c140 = c144 = -(s16)y;
    }
    if (!WRAM(0xFEF824)) {
        vr[0] = -c140 - 13; vr[1] = -c13e + 66; vr[2] = -c144 - 13; vr[3] = -c142 + 72;
    } else {
        vr[0] = c140 + 13 - 262; vr[1] = c13e - 66 - 390; vr[2] = c144 + 13 - 262; vr[3] = c142 - 72 - 382;
    }
    fc_vr[0] = vr[0]; fc_vr[1] = vr[1]; fc_vr[2] = vr[2]; fc_vr[3] = vr[3];
    stm = st_ok ? 1 : 0;
    c1 = stm ? 0 : WRAM(0xFEC27E); c2 = WRAM(0xFEC280); c3 = WRAM(0xFEC282) + WRAM(0xFEC284);
    /* cola llena: se espera al VBLANK. Dos posiciones: en los cuadros ligeros el juego se adelanta
       (tick adelantado en frame_hook) y deja un cuadro de reserva; asi un cuadro que tarda mas de
       un VBLANK no deja la pantalla sin cuadro nuevo (eso, repetido, era el temblor) */
    /* (no antes de que el VBLANK haya aplicado el fundido del juego: si no, no se veria) */
    if (pal_force && !pal_staged && !pal_now) { pal_stage_build(); pal_staged = 1; }
    conv_age = 0;
    while (*(volatile u8 *)&q_cnt == QDEPTH) { layer_fill_one(1); layer_fill_one(0); }
    if (4 * (c1 + c2 + c3) + (stm ? QH + st_w0 + st_w1 + 32 : 0) > 4 * QMAX)  /* (no cabe en la cola: se espera a poder convertir) */
        while (*(volatile u8 *)&spr_ready || *(volatile u8 *)&q_cnt) { layer_fill_one(1); layer_fill_one(0); }
#ifdef NOLAGFIX
    l1_need = 1;
#endif
    l1_age = 0;                         /* (l1_build: hay un cuadro nuevo que construir) */
    dbg_fid++;
    layer_catchup(1, vr[3]);
    layer_catchup(0, vr[1] + 4);
    converting = 1;
    if (*(volatile u8 *)&spr_ready || q_cnt) {
        /* el anterior aun no se ha mostrado: a la cola (en orden) */
        u16 t = q_tail, queued = 0;
        u16 *qb = QBUF(t), *ql;
        if (stm) {                      /* registros de la lista 1 (con las IRQ cortadas: la del juego los rehace) */
            __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
            qb[0] = st_w0; qb[1] = st_n0; qb[2] = st_w1; qb[3] = st_n1;
            qb[4] = st_bb[0]; qb[5] = st_bb[1]; qb[6] = st_bb[2]; qb[7] = st_bb[3];
            {   /* la IRQ puede haber rehecho los registros despues de mirar si cabia: nunca se pasa
                   de la entrada de la cola (detras esta ST_BUF0) */
                s16 room = 4 * QMAX - QH - 2 - 4 * (c2 + c3);
                if ((s16)qb[0] > room) qb[0] = qb[1] = 0;
                if ((s16)(qb[0] + qb[2]) > room) qb[2] = qb[3] = 0;
            }
            lcopyl(qb + QH, ST_BUF0, qb[0] >> 1); lcopyl(qb + QH + qb[0], ST_BUF1, qb[2] >> 1);
            __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
            ql = qb + QH + qb[0] + qb[2];
            if ((u32)ql & 2) ql++;      /* (alinear a long para lcopy) */
        } else {
            lcopy(qb, L1A, c1); ql = qb + 4 * c1;
        }
        q_l2[t] = ql;
        lcopy(ql, L2A, c2); lcopy(ql + 4 * c2, L3A, c3);
        q_stm[t] = stm ? 2 : 0;
        q_vr[t][0] = vr[0]; q_vr[t][1] = vr[1]; q_vr[t][2] = vr[2]; q_vr[t][3] = vr[3];
        q_c1[t] = c1; q_c2[t] = c2; q_c3[t] = c3; q_fid[t] = dbg_fid;
        __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
        if (spr_ready || q_cnt) { q_tail = t ^ (QDEPTH - 1); q_cnt++; queued = 1; }
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
        if (!queued) {                  /* (se mostro mientras tanto) */
            pend_fid = dbg_fid;
            conv_busy = 1;
            convert_publish(vr, q_stm[t], qb, qb, c1, ql, c2, ql + 4 * c2, c3);
            conv_busy = 0;
        }
    } else {
        pend_fid = dbg_fid;
        conv_busy = 1;
        convert_publish(vr, stm, 0, L1A, c1, L2A, c2, L3A, c3);
        conv_busy = 0;
    }
    converting = 0;
    vr_age = 0;
}
/* cuadro en cola: se convierte en cuanto se ha mostrado el anterior (desde el VBLANK) */
void q_service(void)
{
    /* (mientras el bucle principal llena la cola se puede convertir la cabeza: es otra posicion) */
    static volatile u8 q_busy;
    u16 sr;
    __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
    if (conv_busy || q_busy || !q_cnt || spr_ready) { __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory"); return; }
    q_busy = 1;
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    {
        u16 h = q_head;
        u16 *qb = QBUF(h);
        pend_fid = q_fid[h];
        convert_publish(q_vr[h], q_stm[h], qb, qb, q_c1[h], q_l2[h], q_c2[h], q_l2[h] + 4 * q_c2[h], q_c3[h]);
        __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "memory");
        q_head = h ^ (QDEPTH - 1); q_cnt--;
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
    }
    q_busy = 0;
}
/* llamada en lugar de la copia de listas de sprites del juego (rutina 0x44E6, IRQ del juego) */
void spr_update_c(void)
{
    u16 c1 = WRAM(0xFEC27E), c2 = WRAM(0xFEC280), c3 = WRAM(0xFEC282) + WRAM(0xFEC284);
    vr_age = 0;
    WRAM(0xFEC27C) = c1 + c2 + c3;
    q_service();
    if (converting) return;            /* el bucle principal esta convirtiendo (le hemos interrumpido) */
    if (in_catchup) return;            /* (el bucle principal esta a punto de convertir este cuadro) */
    if (l1_age < 4) return;            /* el bucle principal convierte sus cuadros (frame_convert) */
    /* modos en los que el bucle no pasa por 0x32B8: conversion aqui, con los registros reales */
    if (WRAM(0xFEC1A6)) return;        /* el juego va retrasado: las listas no estan terminadas */
    {
        u16 vr[4];
        vr[0] = VREGS[0]; vr[1] = VREGS[1]; vr[2] = VREGS[2]; vr[3] = VREGS[3];
        if (st_ok) convert_publish(vr, 1, 0, L1A, 0, L2A, c2, L3A, c3);
        else convert_publish(vr, 0, 0, L1A, c1, L2A, c2, L3A, c3);
    }
}
/* VBLANK: se muestra el juego de ranuras recien escrito y se oculta el anterior (solo SCB3) */
static void sprites_flip(void)
{
    u16 old = vis_set, set = pend_set;
    u16 b1 = SPR_O1 + set * N1MAX, b0 = SPR_O0 + set * N0MAX;
    u16 o1 = SPR_O1 + old * N1MAX, o0 = SPR_O0 + old * N0MAX;
    if (set == old) {                   /* (no deberia pasar) mismo juego: primero ocultar */
        spr_hide(vis_n1 - vis_h1, o1); spr_hide(vis_h1, o1 + N1MAX - vis_h1);
        spr_hide(vis_n0 - vis_h0, o0); spr_hide(vis_h0, o0 + N0MAX - vis_h0);
    }
    spr_show(spr_n1 - spr_h1, b1, setrec1(set));
    spr_show(spr_h1, b1 + N1MAX - spr_h1, spr_hs1);
    spr_show(spr_n0 - spr_h0, b0, setrec0(set));
    spr_show(spr_h0, b0 + N0MAX - spr_h0, spr_hs0);
    if (set != old) {
        spr_hide(vis_n1 - vis_h1, o1); spr_hide(vis_h1, o1 + N1MAX - vis_h1);
        spr_hide(vis_n0 - vis_h0, o0); spr_hide(vis_h0, o0 + N0MAX - vis_h0);
    }
    vis_set = set; vis_n0 = spr_n0; vis_n1 = spr_n1; vis_h0 = spr_h0; vis_h1 = spr_h1;
    dbg_spr = spr_n0 + spr_n1;
}

void video_init(void)
{
    u16 i;
    for (i = 0; i < NSLOT; i++) { tcol[0][i] = 0xFF; tcol[1][i] = 0xFF; tfill[0][i] = tfill[1][i] = 0xFF; }
    for (i = 0; i < 128; i++) cb1[i] = cb2[i] = 0;
    for (i = 0; i < 64; i++) c1mask[i] = 0;
    vis_set = 0; vis_n0 = vis_n1 = vis_h0 = vis_h1 = 0; spr_ready = 0; hi_xb_old = 1;
    hud_gen = 1; set_hgen[0] = set_hgen[1] = 0;
    full_c0[0] = full_c0[1] = 0xFFFF; lay_win = 0xFFFFFFFFUL; scan_c0[0] = scan_c0[1] = 0xFFFF;
    {   /* cache de descifrado: todas las entradas validas para la clave 0:0 */
        extern u16 dcache[512];
        u16 v0 = dec_first[0], v1 = gdec2(0, 0);
        for (i = 0; i < 512; i += 4) { dcache[i] = 0; dcache[i + 1] = 0; dcache[i + 2] = v0; dcache[i + 3] = v1; }
    }
    lay_force[0] = lay_force[1] = 1; hi_dirty = 1; hi_rows = 0xFFFFFFFFUL;
    eff_tick = 0;
    pal_cvp = 0xFF;
    for (i = 0; i < 0x80; i += 2) *(u16 *)(0x107600 + i) = 0;   /* marcas y cola de paletas (fast.s) */
    REG_VRAMMOD = 1;
    REG_VRAMADDR = 0x8000;
    for (i = 0; i < 0x200; i++) VW(HSHRINK | 0xFF);
    /* el hardware dibuja el sprite 0 al final de cada lista de linea incompleta:
       lo dejamos fuera de pantalla y con un tile vacio */
    REG_VRAMADDR = 0; VW(BLANK_TILE); VW(0);
    for (i = 1; i < 32; i++) { VW(BLANK_TILE); VW(0); }
    vram_set(0x8200, 0);
    vram_set(0x8400, 320 << 7);
}

/* el color 0 de la capa 1 es opaco en el original pero transparente en Neo Geo:
   usamos como color de fondo el color 0 mas frecuente entre muestras de la capa 1 visible */
static void backdrop_calc(void)
{
    u16 sx = cur_vr[3] + CROPX, sy = cur_vr[2] + CROPY;
    u16 cand[12], cnt[12], nc = 0, i, j, best = 0;
    for (i = 0; i < 12; i++) {
        u16 px = sx + 24 + (i & 3) * 80, py = sy + 24 + (i >> 2) * 80;
        u16 idx = (((py >> 4) & 31) << 6) | ((px >> 4) & 63);
        u16 c0 = GPAL[(GVRAM[0x1000 + (idx << 1) + 1] & 0x1F) << 4] & 0xFFF;
        for (j = 0; j < nc; j++) if (cand[j] == c0) { cnt[j]++; break; }
        if (j == nc) { cand[nc] = c0; cnt[nc++] = 1; }
    }
    for (j = 1; j < nc; j++) if (cnt[j] > cnt[best]) best = j;
    bd_col = col_tab[cand[best]]; bd_d = 1;
}
static void backdrop_update(void)
{
    if (pal_force || pal_staged || rv_wait) return;    /* (paletas nuevas aun retenidas: el fondo con ellas, despues) */
    if ((eff_tick & 3) == 2) backdrop_calc();
}

/* sprites de efecto: el pen 8+k cambia el pixel de debajo al banco de paleta k (sombras, faros...).
   En el Neo Geo: variante del tile con el pen k en damero (paleta PAL_SH), es decir, la mitad de los
   pixeles con un color fijo y la otra mitad con el fondo. Lo que se ve es la media de los dos, asi
   que el color fijo se compensa: E = 2*luz - fondo (medias del banco k y del banco 0 en las mismas
   muestras). Con el color medio sin compensar los faros salian a mitad de intensidad. */

static s16 eff_r[8], eff_g[8], eff_b[8];
static u8 eff_init;
static volatile u8 eff_done;      /* bancos recalculados desde el ultimo cuadro nuevo (bits 1..7) */
static inline s16 div16(s32 a, s16 b)      /* divs.w (el cociente cabe en 16 bits) */
{
    __asm__ ("divs.w %1,%0" : "+d"(a) : "d"(b) : "cc");
    return (s16)a;
}
static void effect_bank(u16 k, u16 smooth)
{
    static const u8 ox[6] = { 30, 140, 250, 85, 195, 140 };
    static const u8 oy[6] = { 50, 50, 50, 140, 140, 200 };
    u16 sx = cur_vr[3] + CROPX, sy = cur_vr[2] + CROPY;
    u16 pals[6], i, j;
    s16 r, g, b;
    extern u16 eff_npl, eff_px[8], eff_py[8];
    u16 beam = eff_npl >= 6;        /* hay haz de faros: se mira el fondo debajo de el */
    for (i = 0; i < 6; i++) {
        u16 px = sx + ox[i], py = sy + oy[i];
        if (beam) {
            u16 q = (i * 3 + 1) & 7;                    /* repartidas por el haz */
            s16 nx = (s16)((eff_px[q] >> 7) & 0x1FF);   /* x Neo Geo (escala 13/16) */
            if (nx >= 448) nx -= 512;
            nx = div16((s32)(nx - 10) * 16, 13) + 8;
            px = sx + nx;
            py = sy + (u16)((496 - (eff_py[q] >> 7)) & 0x1FF) + 8;
        }
        u16 idx = (((py >> 4) & 31) << 6) | ((px >> 4) & 63);
        pals[i] = (GVRAM[0x1000 + (idx << 1) + 1] & 0x1F) << 4;
    }
    {
        s16 fr = 0, fg = 0, fb = 0, dr = 0, dg = 0, db = 0, nr = 0, ng = 0, nb = 0, yf, lr = 0, lg = 0, lb = 0;
        const u16 *bank = GPAL + k * 1024;
        /* luz = fondo + diferencia del banco k. Los bancos del original suben (o bajan) cada canal
           que no es 0: la luz toma el tono de cada pixel de fondo. Aqui hay un solo color (damero):
           el fondo entra solo como brillo (su tono -tierra rojiza a los lados de la carretera- teñia
           los faros de rosa) y el tono de la luz sale de la diferencia media por canal, contada solo
           en los pens donde ese canal no es 0 ni esta saturado (blanca en Sisteron, amarilla en las
           etapas con faros amarillos) */
        for (i = 0; i < 6; i++)
            for (j = 2; j < 16; j += 3) {
                u16 c = bank[pals[i] + j], f = GPAL[pals[i] + j];
                s16 cb = (c >> 8) & 15, cr = (c >> 4) & 15, cg = c & 15;
                s16 xb = (f >> 8) & 15, xr = (f >> 4) & 15, xg = f & 15;
                fb += xb; fr += xr; fg += xg; lb += cb; lr += cr; lg += cg;
                if (xr && (xr < 15 || cr < 15)) { dr += cr - xr; nr++; }
                if (xg && (xg < 15 || cg < 15)) { dg += cg - xg; ng++; }
                if (xb && (xb < 15 || cb < 15)) { db += cb - xb; nb++; }
            }
        /* 30 muestras, en 4.4 fijo: brillo medio del fondo (x16/30) + 2 x (luz - fondo) (damero):
           la cantidad de luz es la media de todas las muestras (como antes) y su tono, el de la
           diferencia por canal en los pens validos */
        yf = (s16)((fr * 5 + fg * 9 + fb * 2) >> 4);
        yf = (s16)(yf * 8) / 15;
        if (beam) {         /* debajo del haz: el tono del fondo cuenta (tierra amarilla, asfalto gris) */
            fr = (s16)(fr * 8) / 15; fg = (s16)(fg * 8) / 15; fb = (s16)(fb * 8) / 15;
        } else { fr = fg = fb = yf; }
        {   /* (todo en 16 bits: con divisiones de 32 bits esto no cabia en el VBLANK) */
            s16 ay = div16((((lr - fr) * 5 + (lg - fg) * 9 + (lb - fb) * 2) >> 4) * 16, 15);
            s16 tr = nr ? div16(dr * 16, nr) : 0, tg = ng ? div16(dg * 16, ng) : 0, tb = nb ? div16(db * 16, nb) : 0;
            s16 tx = tr > tg ? tr : tg;
            if (tb > tx) tx = tb;
            if (tx <= 0 || ay <= 0) {      /* sombra o nada: neutra */
                static const u8 dith_m[8] = { 8, 8, 4, 6, 8, 10, 12, 14 };
                r = g = b = yf + div16(ay * 8, dith_m[k & 7]);
            }
            else {
                /* tono normalizado (0-64 por canal) y cantidad de luz repartida segun su brillo */
                s16 hr = div16((tr < 0 ? 0 : tr) * 64, tx), hg = div16((tg < 0 ? 0 : tg) * 64, tx), hb = div16((tb < 0 ? 0 : tb) * 64, tx);
                s16 hy = (hr * 5 + hg * 9 + hb * 2) >> 4;
                if (hy < 16) hy = 16;
                if (ay > 480) ay = 480;
                /* tramado: el banco k ocupa dith_n[k]/16 de los pixeles (gfxconv.py); la luz de cada
                   pixel encendido se reparte entre esa densidad (ay ya es x2, para el 50%) */
                static const u8 dith_n[8] = { 8, 8, 4, 6, 8, 10, 12, 14 };
                static const u8 dith_b[8] = { 8, 1, 1, 2, 2, 3, 3, 4 };   /* haz (fase 4l) */
                s16 n = beam ? dith_b[k & 7] : dith_n[k & 7];
                (void)hy;
                /* luz de cada canal = la subida media del banco en los pens validos (tr/tg/tb, la
                   misma que el original aplica a cada pixel de fondo), repartida segun la densidad */
                if (beam) {     /* fase 4m: puntos sueltos con el doble de la subida del original, sin
                                   llegar al blanco puro (antes se saturaban: demasiado intensos) */
                    s16 mx;
                    r = fr + (tr < 0 ? 0 : tr) * 2; g = fg + (tg < 0 ? 0 : tg) * 2; b = fb + (tb < 0 ? 0 : tb) * 2;
                    mx = r > g ? r : g; if (b > mx) mx = b;
                    if (mx > 200) { r = div16(r * 25, mx >> 3); g = div16(g * 25, mx >> 3); b = div16(b * 25, mx >> 3); }
                } else {
                r = fr + div16((tr < 0 ? 0 : tr) * 16, n); g = fg + div16((tg < 0 ? 0 : tg) * 16, n); b = fb + div16((tb < 0 ? 0 : tb) * 16, n);
                }
            }
        }
        if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        /* suavizado 3:1 (las muestras cambian con el desplazamiento: sin el, la luz parpadea) */
        if (smooth && (eff_init & (1 << k))) {
            eff_r[k] = (eff_r[k] * 3 + r) >> 2; eff_g[k] = (eff_g[k] * 3 + g) >> 2; eff_b[k] = (eff_b[k] * 3 + b) >> 2;
        } else {
            eff_r[k] = r; eff_g[k] = g; eff_b[k] = b; eff_init |= 1 << k;
        }
        r = (eff_r[k] + 8) >> 4; g = (eff_g[k] + 8) >> 4; b = (eff_b[k] + 8) >> 4;
        if (r > 15) r = 15; if (g > 15) g = 15; if (b > 15) b = 15;
        eff_col[k] = col_tab[(b << 8) | (r << 4) | g]; eff_d[k] = 1;
    }
}
/* en el tiempo libre del bucle principal: los 7 bancos en cada cuadro (gratis para el juego) */
u16 eff_idle_step(void)
{
    u16 k;
    if (pal_evt || pal_force) return 0;
    for (k = 1; k < 8; k++)
        if (!(eff_done & (1 << k))) { effect_bank(k, 1); eff_done |= 1 << k; return 1; }
    return 0;
}
/* en el VBLANK de un cuadro nuevo: si el bucle principal no ha tenido tiempo, un banco */
static void effect_pal_update(void)
{
    u16 k = ((++eff_tick) % 7) + 1;
    if (pal_force || pal_staged) return;
    /* (solo 8 de cada 32 cuadros: en los cuadros pesados este tiempo le hace falta al juego) */
    if (!(eff_tick & 24) && eff_done != 0xFE && !(eff_done & (1 << k))) effect_bank(k, 1);
    eff_done = 0;
}

/* las 48 paletas del juego (capas y sprites) todas negras: pantalla fundida a negro */
static u16 pal_black_scan(void)
{
    /* (sobre la copia en RAM de lo convertido, pal_cache: la RAM de paletas del Neo Geo no se lee,
       cada acceso fuera del VBLANK pinta un punto en hardware real) */
    const u16 *p = pal_cache;
    u16 i;
    for (i = 0; i < 48 * 16; i++) if (p[i] & 0x0FFF) return 0;
    return 1;
}
/* las dos capas escritas, a la vista y en la posicion del desplazamiento actual del juego */
static u16 layers_ready(void)
{
    const u16 *wv = (conv_age > 3) ? (const u16 *)VREGS : fc_vr;
    u16 wc0 = ((wv[1] + 4 + CROPX - 16) & 1023) >> 4, wc1 = ((wv[3] + CROPX - 16) & 1023) >> 4;
    return full_c0[0] == lay_c0[0] && full_c0[1] == lay_c0[1] && !vis_dirty[0] && !vis_dirty[1]
           && lay_c0[0] == wc0 && lay_c0[1] == wc1;
}
extern volatile u8 pal_qh, pal_qt;
static void pal_vbl_copy(void)
{
    if (pal_go || pal_now) {
        pal_copy_dirty();
        if (pal_go) { pal_evt = 0; pal_staged = 0; pal_post = 1; }
        pal_go = 0; pal_now = 0;
        scr_dark = pal_black_scan();
    } else if (!pal_evt && !pal_force) {
        /* pantalla en negro (tras un fundido) y paletas nuevas: no se encienden hasta que la imagen
           nueva esta entera (o 30 cuadros). Si no, se veian piezas apareciendo a destiempo (fondo,
           marco, fotos) y restos de la pantalla anterior con los colores nuevos */
        if (scr_dark && pal_qh != pal_qt && rv_wait < 30 && !layers_ready()) rv_wait++;
        else if (pal_qh != pal_qt) { rv_wait = 0; pal_copy_dirty(); scr_dark = pal_black_scan(); }
    }
    pal_small_flush();
}

void video_frame(void)
{
    /* en el VBLANK solo alturas y posiciones (ya calculadas); las columnas nuevas se escriben
       despues, en ranuras ocultas */
    u16 prep = 0, fresh = spr_ready;   /* (cuadro nuevo en este VBLANK) */
    PM(12);
    if (hi_dirty && (hi_rows & lay_win)) vis_dirty[0] = 1;   /* cambiaron mascaras de filas en pantalla */
    if (spr_ready) {    /* nuevo cuadro del juego: sprites, desplazamiento y rectangulo a la vez */
        cur_vr[0] = pend_vr[0]; cur_vr[1] = pend_vr[1]; cur_vr[2] = pend_vr[2]; cur_vr[3] = pend_vr[3];
        o0_x0 = pend_bb[0]; o0_x1 = pend_bb[1]; o0_y0 = pend_bb[2]; o0_y1 = pend_bb[3];
        hi_n0 = spr_n0;
        vr_age = 0;
        PMB(11);
        sprites_flip();
        PMB(12);
        dbg_shown = pend_fid;
        spr_ready = 0;
        prep = vis_dirty[0] | (vis_dirty[1] << 1);     /* lo preparado quedo viejo */
    } else {
        if (++vr_age > 8) {
            /* el juego no llama a la copia de sprites (pantallas sin sprites): desplazamiento
               directo. Si llama pero va retrasado se mantiene el ultimo cuadro completo */
            vr_age = 9;
            if (cur_vr[0] != VREGS[0] || cur_vr[1] != VREGS[1] || cur_vr[2] != VREGS[2] || cur_vr[3] != VREGS[3]) prep = 3;
            cur_vr[0] = VREGS[0]; cur_vr[1] = VREGS[1]; cur_vr[2] = VREGS[2]; cur_vr[3] = VREGS[3];
        }
        prep |= (vis_dirty[0] | lay_force[0]) | ((vis_dirty[1] | lay_force[1]) << 1);
        /* el bucle principal esta preparando el cuadro siguiente (convert_publish): lo preparado
           de una capa puede ser ya de ese cuadro; no se toca nada hasta que se publique (antes se
           aplicaban las dos capas y una salia con la posicion del cuadro siguiente: saltos del
           fondo de 8-13 px durante uno o dos cuadros) */
        if (preparing) prep = 0;
        pr_ok = prep != 0;
    }
    /* paletas: lo primero tras las alturas de los sprites. Todo lo que toca la RAM de paletas tiene
       que acabar antes de que empiece la imagen (en los menus, el resto del VBLANK -recalcular las
       capas- llega a pasarse de el). Lo normal es una o dos paletas; en un cambio de escena, las 48
       (~10000 ciclos) */
    pal_vbl_copy();
    PM(10);
    PMB(13);
    if (prep) {
        s16 bb[4];
        bb[0] = o0_x0; bb[1] = o0_x1; bb[2] = o0_y0; bb[3] = o0_y1;
        if (prep & 2) { layer_prepare(1, cur_vr[3], cur_vr[2], bb, hi_n0); vis_dirty[1] = 0; }
        if (prep & 1) { layer_prepare(0, cur_vr[1] + 4, cur_vr[0], bb, hi_n0); vis_dirty[0] = 0; }
    }
    PMB(14);
    PMB(15);
    if (pr_ok) {
        /* con un cuadro nuevo, las dos (preparadas al publicarlo); si no, solo las recalculadas aqui */
        if (fresh || (prep & 2)) layer_apply(1);
        if (fresh || (prep & 1)) layer_apply(0);
        pr_ok = 0;
    }
    /* fuera de la carrera (titulo, menus, mapa) no hay frame_convert que prepare las paletas:
       ahi se vuelve a la revision por partes de siempre (antes pal_force se quedaba puesto, nada
       revisaba las paletas y los menus salian en negro) */
    if (conv_age < 255) conv_age++;
    /* fuera de la carrera (titulo, menus, mapa) no hay frame_convert: si el juego ha cargado
       paletas enteras (pantalla nueva), se preparan aqui y, como en la carrera, se aplican cuando
       las dos capas estan escritas y a la vista (o a los 10 cuadros). Antes se aplicaban al momento
       y se veian columnas de la pantalla anterior con los colores nuevos y piezas que aparecian
       a destiempo */
    if (conv_age > 3 && !pal_go) {
        pal_evt = 0;
        if (pal_force && !pal_staged) { pal_stage_build(); pal_staged = 1; pal_wait = 0; }
    }
    /* fundido del juego sin carga de paletas detras (pal_force): no se bloquea la revision para
       siempre (si no, las paletas se quedarian como en el fundido) */
    { static u8 evt_age;
      if (pal_evt && !pal_force && !pal_go && !pal_now) { if (++evt_age > 30) { pal_evt = 0; evt_age = 0; } } else evt_age = 0; }
    if (pal_force) {
        /* paletas cargadas tras el fundido: se aplican cuando la imagen nueva esta entera (las dos
           capas escritas y a la vista), como la recreativa, que no la ensena antes. Si no, se veian
           columnas sin escribir (color de fondo) y el marcador con colores a medio cargar */
        /* ademas, la ventana a la vista tiene que ser la del desplazamiento actual del juego: al
           empezar una carrera el juego carga las paletas con el mapa nuevo aun a medio escribir y
           la camara todavia en la posicion de la pantalla anterior (se veia esa zona a medias) */
        const u16 *wv = (conv_age > 3) ? (const u16 *)VREGS : fc_vr;
        u16 wc0 = ((wv[1] + 4 + CROPX - 16) & 1023) >> 4, wc1 = ((wv[3] + CROPX - 16) & 1023) >> 4;
        if (pal_staged && (++pal_wait > 30 || (full_c0[0] == lay_c0[0] && full_c0[1] == lay_c0[1] && !vis_dirty[0] && !vis_dirty[1]
                                               && lay_c0[0] == wc0 && lay_c0[1] == wc1))) {
            pal_force = 0; pal_wait = 0;
            pal_go = 1;     /* al principio del VBLANK siguiente (es largo: antes de que empiece la imagen) */
        }
    }
    PMB(16);
    PM(13);
    PMB(17);
    /* fuera ya de lo urgente: paletas (solo con un cuadro nuevo: en un VBLANK que repite el anterior
       porque el juego va retrasado no han cambiado, y ese tiempo le hace falta al juego) */
    /* fuera de la carrera (conv_age > 3) tambien en cada VBLANK: en los fundidos entre pantallas el
       juego no publica cuadros nuevos (su bucle de fundido no deja que la IRQ copie sprites) y aqui
       no se revisaban las paletas: no se veia el fundido y, en su lugar, como el juego borra y
       dibuja la pantalla siguiente con las paletas ya a negro, se veian los tiles borrandose a
       bloques y basura de la pantalla nueva con colores viejos */
    if (fresh || vr_age > 8 || conv_age > 3) {
        effect_pal_update();
        PM(14);
        {   /* las paletas se revisan enteras en el tiempo libre del bucle principal (pal_idle_step).
               Si un cuadro pesado no lo ha dejado, aqui solo una parte y, si en ella han cambiado
               varias (cambio general: fundido, tramo nuevo), todas las que falten */
            u16 p0 = eff_tick % 6, p;
            if (!pal_evt && !pal_force && pal_done != 0x3F && !(pal_done & (1 << p0))) {
                if (pal_update_part(p0) >= 2)
                    for (p = 0; p < 6; p++) if (p != p0 && !(pal_done & (1 << p))) pal_update_part(p);
            }
            pal_done = 0;
        }
        PM(18);
        backdrop_update();
    }
    PMB(18);
    PMB(19);
    /* unas pocas columnas (el resto, en el tiempo libre del bucle principal). (Dejar aqui solo las
       que ya hacen falta en pantalla resulto peor: sin tiempo libre las precargadas no llegan) */
    { u16 n = TAILN; while (n && (layer_fill_one(1) | layer_fill_one(0))) n--; }
    PMB(20);
    /* (tras un cambio de escena: fondo y efectos con las paletas nuevas ya; unas pocas palabras) */
    if (pal_post) { pal_post = 0; backdrop_calc(); { u16 k; for (k = 1; k < 8; k++) effect_bank(k, 0); } pal_small_flush(); }
    vbl_seq++;
    if (l1_age < 255) l1_age++;
    if (dbg_mask & 8) { u16 i; REG_VRAMMOD = 1; REG_VRAMADDR = 0x7000; for (i = 0; i < 0x500; i++) VW(0); }
    PM(20);
}

#ifdef DBGSPD
void dbg_lay(u16 *o)
{
    u16 L, k;
    for (L = 0; L < 2; L++) {
        u16 c0 = lay_c0[L], n = 0;
        for (k = 0; k < NCOL; k++) { u16 col = (c0 + k) & 63; if (tcol[L][col & (NSLOT - 1)] == col) n++; }
        o[L * 3] = c0; o[L * 3 + 1] = full_c0[L]; o[L * 3 + 2] = n;
    }
    o[6] = scr_dark; o[7] = rv_wait; o[8] = vis_dirty[0] | (vis_dirty[1] << 1);
}
#endif

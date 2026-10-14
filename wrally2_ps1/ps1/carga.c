/* El CD en los menus (H3): lector cd.c del WR1 por ruta (sondeo, sin interrupciones). Dos trabajos:
   - Emergencias: codigos que el dibujo no encuentra en la VRAM (video.c, carga_falta) van a una cola pequena; se lee
     de fondo su sector de TILESA.BIN (12 registros A por sector, codigo t en el sector t / 12) con cd_read_async y al
     llegar se suben los 12 (los vecinos suelen hacer falta tambien). Una lectura a la vez; tienen prioridad sobre
     el fondo (se corta y se reanuda).
   - MENUS.PAK: indice al principio (tipo 5: clave, sector, sectores, registros) y una seccion A por pantalla (clave
     modo << 16 | FEC146). De cada seccion se lleva el mapa de sectores que no estan en la VRAM (al principio todos;
     un sector se borra al subirlo y vuelve si video.c desaloja un tile suyo): cargar una seccion es leer del primer
     sector que falta al ultimo, asi una carga cortada se reanuda y una seccion desalojada a medias no se relee
     entera. carga_pantalla sube la seccion de la clave bloqueando (el juego esta congelado dentro de rc_vblank;
     CARGANDO si pasa de 0,5 s) y deja las demas secciones del mismo modo para el fondo (cd_stream_async con un
     anillo de sectores que se vacia en carga_atiende, con la GPU parada).
   Sondeo: carga_poll en cualquier sitio; carga_atiende solo donde la GPU esta parada (sube a la VRAM por DMA). */
#include "../../wrally_ps1/ps1/cd.h"
#include "video.h"
#include "carga.h"
#define T1 t1_get()
#define SEC 2048
#ifndef LIN_05
#define LIN_05 7875                             /* 0,5 s en lineas de video (-DLIN_05=n para probar CARGANDO) */
#endif
#define RW(a) (*(const u16 *)(rc_ram + ((a) & 0xFFFE)))

static int lba_ta = -1, lba_me = -1;            /* TILESA.BIN y MENUS.PAK */
typedef struct { u32 clave, sec, n; } sec_t;
#define NSEC 64
#define MAXS 256                                /* sectores por seccion que lleva el mapa (3 072 tiles > huecos) */
static sec_t idx[NSEC]; int carga_menus;
static u32 falta[NSEC][MAXS / 32];              /* sectores de la seccion que no estan en la VRAM */
static u8 ebuf[SEC] __attribute__((aligned(4)));           /* sector de emergencia (y el indice al arrancar) */
#define RING 4
static u8 ring[RING][SEC] __attribute__((aligned(4)));     /* anillo del fondo */
#define NQ 32
static u16 cola[NQ]; static int nq, qh;         /* emergencias pendientes (FIFO circular) */
static u8 pedido[32768 / 8];                    /* codigo pedido y aun no subido */
static int em_on, em_sec; static u16 em_t0;
static int bg_on, bg_k, bg_a, bg_cnt, bg_got, bg_lista[NSEC], bg_n, bg_i;   /* fondo: seccion bg_k, sectores bg_a.. (bg_cnt), bg_got subidos */
static int bloq_vis, bloq_fb, bloq_sec, bloq_a; static u16 bloq_t0;
u32 carga_sectores, carga_emerg, carga_emerg_lin, carga_emerg_max, carga_bloq, carga_bloq_lin, carga_bloq_max, carga_fondo, carga_err;

/* un sector de paquete de registros A (formatos.md 3) a la VRAM; borra los codigos de la cola y el sector del mapa */
static void procesa(const u8 *b, int sec, int sub)
{
    int r, n = b[5];
    if (b[0] != 'W' || b[1] != 'R' || b[2] != '2' || b[3] != 'T' || b[4] != 0 || n > 12) { carga_err++; return; }
    for (r = 0; r < n; r++) {
        const u8 *reg = b + 32 + r * 162; u32 c = reg[0] | (u32)reg[1] << 8;
        vid_sube_a(reg, sec, sub);
        if (c < 32768) pedido[c >> 3] &= (u8)~(1 << (c & 7));
    }
    if (sec < NSEC && sub < MAXS) falta[sec][sub >> 5] &= ~(1u << (sub & 31));
}
void carga_baja(int sec, int sub)
{
    if (sec < NSEC && sub < MAXS) falta[sec][sub >> 5] |= 1u << (sub & 31);
}
/* primer tramo de sectores que faltan de la seccion k (huecos de hasta GAP sectores se leen de paso: mas barato
   que otra busqueda); 0 si esta entera */
#define GAP 16
static int rango(int k, int *a, int *b)
{
    int i, p = -1, q = -1;
    for (i = 0; i < MAXS; i++) {
        if (!(falta[k][i >> 5] & (1u << (i & 31)))) { if (q >= 0 && i - q > GAP) break; continue; }
        if (p < 0) p = i;
        q = i;
    }
    if (p < 0) return 0;
    *a = p; *b = q;
    return 1;
}

/* ---- arranque: la unidad y el directorio; el indice de MENUS.PAK (tipo 5: entradas de 16 bytes hasta 0xFFFFFFFF) */
void carga_init(void)
{
    int sz, s, k, fin = 0;
    cd_init();
    if (cd_find("TILESA.BIN", &lba_ta, &sz)) lba_ta = -1;
    if (cd_find("MENUS.PAK", &lba_me, &sz)) { lba_me = -1; return; }
    for (s = 0; s < 8 && !fin && carga_menus < NSEC; s++) {
        const u32 *e = (const u32 *)(ebuf + 32);
        if (cd_read(lba_me + s, 1, ebuf, 1) || ebuf[0] != 'W' || ebuf[4] != 5) break;
        for (k = 0; k < 125 && carga_menus < NSEC; k++, e += 4) {
            int n = (int)e[2], i;
            if (e[0] == 0xFFFFFFFF) { fin = 1; break; }
            idx[carga_menus].clave = e[0]; idx[carga_menus].sec = e[1]; idx[carga_menus].n = e[2];
            if (n > MAXS) n = MAXS;
            for (i = 0; i < n; i++) falta[carga_menus][i >> 5] |= 1u << (i & 31);   /* falta entera */
            carga_menus++;
        }
    }
}
/* hueco maximo entre sondeos con lectura activa (lineas; puerta <= 4 ms = 63 lineas) */
u32 carga_hueco_max; static u16 t_poll; static int poll_ocupado;
void carga_poll(void)
{
    u16 t = T1; int b = cd_busy();
    if (b && poll_ocupado) { u32 d = (u16)(t - t_poll); if (d > carga_hueco_max) carga_hueco_max = d; }
    t_poll = t; poll_ocupado = b;
    cd_poll();
}

/* ---- emergencias */
void carga_falta(u32 c)
{
    if (lba_ta < 0 || c >= 32768 || RW(0xFEC298) == 0) return;   /* en el negro no se ve nada: lo trae la pantalla */
    if (pedido[c >> 3] & (1 << (c & 7))) return;
    if (nq >= NQ) return;
    pedido[c >> 3] |= (u8)(1 << (c & 7));
    cola[(qh + nq) % NQ] = (u16)c; nq++;
}
static int em_pendiente(void)
{
    while (nq && !(pedido[cola[qh] >> 3] & (1 << (cola[qh] & 7)))) { qh = (qh + 1) % NQ; nq--; }
    return nq > 0;
}
static void em_start(void)
{
    em_sec = cola[qh] / 12;
    if (cd_read_async(lba_ta + em_sec, 1, ebuf)) return;
    em_on = 1; em_t0 = T1; carga_emerg++;
}
/* ---- fondo */
static u8 *bg_dst(int i)
{
    if (i - bg_got >= RING) return 0;         /* el anillo esta lleno: se volvera a buscar */
    return ring[i % RING];
}
static void bg_drain(void)
{
    int pos = cd_read_pos();
    while (bg_got < pos) { procesa(ring[bg_got % RING], bg_k, bg_a + bg_got); bg_got++; }
}
static void bg_corta(void)
{
    if (!bg_on) return;
    cd_cancel(); bg_drain(); bg_on = 0; vid_carga_fin();
}
static void bg_start(void)
{
    int a, b, k = bg_lista[bg_i];
    if (!rango(k, &a, &b)) { bg_i++; return; }
    bg_k = k; bg_a = a; bg_cnt = b - a + 1; bg_got = 0;
    if (cd_stream_async(lba_me + (int)idx[k].sec + a, bg_cnt, bg_dst)) return;
    bg_on = 1; vid_carga_ini(k);
}
void carga_atiende(void)
{
    carga_poll();
    carga_sectores = (u32)cd_dbg[0];
    if (em_on) {
        int s = cd_read_state();
        if (s == 1) {
            u32 l = (u16)(T1 - em_t0);
            procesa(ebuf, 0xFF, 0); em_on = 0;
            carga_emerg_lin += l; if (l > carga_emerg_max) carga_emerg_max = l;
        } else if (s < 0) {                   /* error o cortada por una carga bloqueante: el codigo se pide otra vez */
            u32 c = cola[qh]; em_on = 0;
            if (s == -1) { carga_err++; pedido[c >> 3] &= (u8)~(1 << (c & 7)); }
        }
    }
    if (bg_on) {
        int s;
        bg_drain(); s = cd_read_state();
        if (s == 1) { bg_on = 0; vid_carga_fin(); carga_fondo++; }   /* el tramo siguiente, si queda, en bg_start */
        else if (s < 0) { bg_on = 0; vid_carga_fin(); if (s == -1) { carga_err++; bg_i++; } }   /* -2 cortada: se reanuda */
    }
    if (em_pendiente() && bg_on) bg_corta();   /* lo que se ve manda: el fondo se reanuda despues */
    if (!em_on && !bg_on) {
        if (em_pendiente()) em_start();
        else if (bg_i < bg_n) bg_start();
    }
}

/* ---- la seccion de una pantalla, bloqueando (el juego esta congelado dentro de rc_vblank) */
static void bloq_cb(int i, const unsigned char *d)
{
    procesa(d, bloq_sec, bloq_a + i);
    if (!bloq_vis && (u16)(T1 - bloq_t0) > LIN_05) { vid_cargando(bloq_fb); bloq_vis = 1; }
}
int carga_pantalla(u32 clave, int fb)
{
    int k, j, a, b;
    if (lba_me < 0) return -1;
    for (k = 0; k < carga_menus && idx[k].clave != clave; k++);
    if (k == carga_menus) return -1;
    bg_corta();                               /* la lista de fondo se rehace para este modo */
    {   /* las secciones de este modo se desalojan las ultimas */
        u32 m[2] = {0, 0};
        for (j = 0; j < carga_menus; j++) if ((idx[j].clave >> 16) == (clave >> 16)) m[j >> 5] |= 1u << (j & 31);
        vid_sec_prot(m[0], m[1]);
    }
    if (rango(k, &a, &b)) {
        u32 l; int n = 0;
        if (em_on) em_on = 0;                 /* cd_stream la cancela; el codigo sigue pedido */
        bloq_t0 = T1; bloq_vis = 0; bloq_fb = fb; bloq_sec = k;
        vid_carga_ini(k);
        do {                                  /* tramo a tramo (como mucho uno por sector: no se repite ninguno) */
            bloq_a = a;
            if (cd_stream(lba_me + (int)idx[k].sec + a, b - a + 1, bloq_cb, 1)) { carga_err++; break; }
        } while (++n < MAXS && rango(k, &a, &b));
        vid_carga_fin();
        l = (u16)(T1 - bloq_t0); carga_bloq++; carga_bloq_lin += l; if (l > carga_bloq_max) carga_bloq_max = l;
    }
    for (bg_n = 0, j = 1; j < carga_menus; j++) {
        int q = (k + j) % carga_menus;
        if ((idx[q].clave >> 16) == (clave >> 16) && rango(q, &a, &b)) bg_lista[bg_n++] = q;
    }
    bg_i = 0;
    return 0;
}

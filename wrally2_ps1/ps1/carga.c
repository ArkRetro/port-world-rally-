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
   - Demo (DEMOr.PAK, formatos.md 3 y 5; r = FEC3C6 + 1 en el titulo y en el negro de antes del mapa): en el titulo,
     de fondo, el mapa y el principio del VO a los 4 bloques B del titulo (b12-b15), la L2 y luego la ROM al area
     de tramo (solo lo que quepa entero; la ROM aun no se usa: la entera va en el EXE); en el negro antes del mapa,
     bloqueando, lo que falte del mapa; durante el mapa, el VO de fondo; en el negro antes de la demo, bloqueando,
     lo que falte del VO fijo (los primeros 3 936); durante la demo, el resto del VO a huecos rotativos. Las
     emergencias en B salen de TILES.BIN (14 registros B por sector; se suben solo los pedidos).
   - Area de tramo (diseno 2.2): la RAM libre detras de la BSS hasta la pila (menos AREA_RES, por si se quiere
     guardar sitio al codigo que falta): 10 bytes por hueco B (recetas), los sectores de la L2 tal cual y detras la
     ROM. En R0 (ROM entera en el EXE) son unos 180 KB: la L2 de DEMO0 (125 sectores) no cabe entera.
   Sondeo: carga_poll en cualquier sitio; carga_atiende solo donde la GPU esta parada (sube a la VRAM por DMA). */
#include "../../wrally_ps1/ps1/cd.h"
#include "video.h"
#include "carga.h"
#include "snd.h"
#define T1 t1_get()
#define SEC 2048
#ifndef LIN_05
#define LIN_05 7875                             /* 0,5 s en lineas de video (-DLIN_05=n para probar CARGANDO) */
#endif
#define RW(a) (*(const u16 *)(rc_ram + ((a) & 0xFFFE)))

#ifndef AREA_RES
#define AREA_RES 0
#endif
#define NFIJ 3936                               /* VO fijo: 4 352 huecos - 416 rotativos (diseno 2.3) */
#define HREC (4352 * 10)                        /* recetas de los huecos B al principio del area */
static int lba_ta = -1, lba_me = -1, lba_tb = -1;   /* TILESA.BIN, MENUS.PAK y TILES.BIN */
static int lba_dm[5] = {-1, -1, -1, -1, -1};   /* DEMO0-3.PAK y (4) el STkk.PAK del tramo */
static u32 dmi[5][4][3];                        /* paquete, clave - 1 (mapa, VO, L2, ROM): sector, sectores, registros */
static int st_k = -1;                           /* tramo cuyo indice esta en dmi[4] */
static int lba_g[3] = {-1, -1, -1}; static u32 gi[3][3];   /* GAMEN/L/R.PAK: los 1 024 fijos de carrera de cada coche (VO, clave 2) */
static int lba_gn = -1; static u32 *gn = gi[0];  /* el del coche de la partida (FE8124; si falta su paquete, N) */
static int k_tit = -1;                          /* seccion del titulo (3/16) en MENUS.PAK */
static u8 *area, *area_l2; static int l2_cap;   /* area de tramo; sectores que caben detras de las recetas */
static int dm_r = -1, dm_fase;                  /* demo del paquete; fase: 0 nada, 1 titulo, 2 mapa, 3 demo */
static int dm_map, dm_vo, dm_gn, dm_l2, dm_rom, dm_l2reg, dm_lleno;   /* registros en la VRAM, sectores en el area (dm_rom -1: no cabe) */
static int dm_rom_ok;                           /* R1: la ROM de la demo ya compactada y en rc_romtab */
static int rom_arr;                             /* R1: la ROM entera en el area (desde el arranque hasta la suma) */
#ifdef R1
static void bk_quita(void); static void rom_pon(u8 *b, int n);
#endif
u32 carga_emerg_sec, carga_dm_neg[2], carga_dm_neg_max[2], carga_dm_neg_n[2], carga_dm_fallo, carga_dm_nositio, carga_area_kb, carga_l2_cap;
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
static int em_on, em_sec, em_b, em_n; static u16 em_t0;
static int bg_on, bg_k, bg_a, bg_cnt, bg_got, bg_lista[NSEC], bg_n, bg_i;   /* fondo: seccion bg_k, sectores bg_a.. (bg_cnt), bg_got subidos */
static int bg_tipo, bg_para, *bg_cuenta; static u8 *bg_dir;   /* 0 MENUS, 1 demo a la VRAM (anillo), 2 demo al area (directo) */
static int bloq_vis, bloq_fb, bloq_sec, bloq_a, bloq_hecho, bloq_total; static u16 bloq_t0;
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
static u32 le32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static int cab(const u8 *b, int t) { return b[0] == 'W' && b[1] == 'R' && b[2] == '2' && b[3] == 'T' && b[4] == t && b[5] <= 14; }
/* emergencia en B: de los 14 registros del sector de TILES.BIN, solo los pedidos (los huecos rotativos son pocos) */
static void procesa_b(const u8 *b)
{   /* en la carrera tambien los vecinos del sector (14 codigos seguidos: las poses de un coche que no caben en su
       GAMEc van seguidas en TILES.BIN), a huecos rotativos que se desalojan por edad */
    int r, vec = RW(0xFEC298) == 1;
    if (!cab(b, 1)) { carga_err++; return; }
    for (r = 0; r < b[5]; r++) {
        const u8 *reg = b + 32 + r * 144; u32 c = reg[128] | (u32)reg[129] << 8;
        if (c >= 32768) continue;
        if (pedido[c >> 3] & (1 << (c & 7))) { vid_sube_b(reg, 0, 1); pedido[c >> 3] &= (u8)~(1 << (c & 7)); }
        else if (vec && vid_hueco(c) < 0) vid_sube_b(reg, 0, 1);   /* vecino: la siguiente pose del coche */
    }
    dma_wait();                               /* los texels salen del sector por DMA: que acabe antes de reusarlo */
}
/* un sector del mapa (tipo 3) o del VO (tipo 4) a la VRAM desde el registro que toca; los primeros NFIJ del VO son
   fijos y los demas, en la demo, pueden desalojar. para: si no hay sitio se corta ahi (-1) y el contador se queda */
static int dm_sector(const u8 *b, int para)
{
    int r, t = b[4], s, *cnt;
    if ((!cab(b, 3) && !cab(b, 4))) { carga_err++; return 0; }
    s = (int)le32(b + 8); cnt = t == 3 ? &dm_map : b[7] == 4 ? &dm_gn : &dm_vo;   /* paquete 0x04xx: GAMEc */
    for (r = 0; r < b[5]; r++) {
        int j = s * 14 + r; u32 ex = t == 3 ? 0x80 : (j < NFIJ || cnt == &dm_gn) ? 0x10 : 0x01;   /* mapa, VO fijo, VO rotativo nuevo */
        if (j < *cnt) continue;
        if (vid_sube_b(b + 32 + r * 144, ex, dm_fase == 3 && t == 4 && j >= NFIJ) == -1) {
            if (para) { dm_lleno = 1; dma_wait(); return -1; }
            carga_dm_nositio++;
        }
        *cnt = j + 1;
    }
    dma_wait();
    return 0;
}
/* los registros de la L2 ya en el area, a la tabla de codigos (solo en el reparto B) */
static void dm_l2_apunta(void)
{
    if (vid_lay() != 2) return;
    for (; dm_l2reg < dm_l2; dm_l2reg++) {
        const u8 *b = area_l2 + dm_l2reg * SEC; int r;
        if (!cab(b, 1)) { carga_err++; continue; }
        for (r = 0; r < b[5]; r++) vid_pon_l2(b + 32 + r * 144, dm_l2reg * 14 + r);
    }
}
/* la VRAM de los bloques se perdio (reparto B): todas las secciones de MENUS faltan y las peticiones A no valen */
static void menus_perdidos(void)
{
    int k, i;
    for (k = 0; k < carga_menus; k++) {
        int n = (int)idx[k].n > MAXS ? MAXS : (int)idx[k].n;
        for (i = 0; i < n; i++) falta[k][i >> 5] |= 1u << (i & 31);
    }
}
static void pedidos_fuera(void)
{
    int i;
    for (i = 0; i < 32768 / 8; i++) pedido[i] = 0;
    nq = 0;
}
/* primer tramo de sectores que faltan de la seccion k (huecos de hasta GAP sectores se leen de paso: mas barato
   que otra busqueda); 0 si esta entera */
#define GAP 16
#define BLOQ_MAX 40                             /* carga parcial (cambio de seccion con la pantalla a la vista, p. ej. la
                                                   animacion del titulo con la musica): sectores que se cargan bloqueando
                                                   (480 tiles, ~0,3 s a 2x); lo demas de fondo mientras se ve. En el negro
                                                   la seccion entera */
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
extern char __bss_end[];
static void demo_init(void)
{   /* indices de DEMO0-3.PAK (un sector cada uno) y el area de tramo */
    static char nom[] = "DEMO0.PAK";
    int r, k, sz; s32 t;
    if (cd_find("TILES.BIN", &lba_tb, &sz)) lba_tb = -1;
    for (r = 0; r < 4; r++) {
        nom[4] = (char)('0' + r);
        if (cd_find(nom, &lba_dm[r], &sz) || cd_read(lba_dm[r], 1, ebuf, 1) || !cab(ebuf, 5)) { lba_dm[r] = -1; continue; }
        for (k = 0; k < ebuf[5] && k < 8; k++) {
            const u8 *e = ebuf + 32 + k * 16; u32 c = le32(e);
            if (c >= 1 && c <= 4) { dmi[r][c - 1][0] = le32(e + 4); dmi[r][c - 1][1] = le32(e + 8); dmi[r][c - 1][2] = le32(e + 12); }
        }
    }
    for (r = 0; r < 3; r++) {
        static char gnom[] = "GAMEN.PAK";
        gnom[4] = "NLR"[r];
        if (!cd_find(gnom, &lba_g[r], &sz) && !cd_read(lba_g[r], 1, ebuf, 1) && cab(ebuf, 5)) {
            for (k = 0; k < ebuf[5] && k < 8; k++) {
                const u8 *e = ebuf + 32 + k * 16;
                if (le32(e) == 2) { gi[r][0] = le32(e + 4); gi[r][1] = le32(e + 8); gi[r][2] = le32(e + 12); }
            }
        } else lba_g[r] = -1;
    }
    lba_gn = lba_g[0];
    area = (u8 *)(((u32)__bss_end + 3) & ~3u);
    t = (s32)(0x801FFFF0u - 16384u - AREA_RES - (u32)area);   /* la pila empieza en 801FFFF0 (crt0) */
    if (t < HREC + SEC) { area = 0; return; }
    carga_area_kb = (u32)t >> 10; area_l2 = area + HREC; l2_cap = (t - HREC) / SEC; carga_l2_cap = (u32)l2_cap;
    vid_area(area, area_l2);
}
void carga_init(void)
{
    int sz, s, k, fin = 0;
    cd_init();
    if (cd_find("TILESA.BIN", &lba_ta, &sz)) lba_ta = -1;
    demo_init();
    if (cd_find("MENUS.PAK", &lba_me, &sz)) { lba_me = -1; return; }
    for (s = 0; s < 8 && !fin && carga_menus < NSEC; s++) {
        const u32 *e = (const u32 *)(ebuf + 32);
        if (cd_read(lba_me + s, 1, ebuf, 1) || ebuf[0] != 'W' || ebuf[4] != 5) break;
        for (k = 0; k < 125 && carga_menus < NSEC; k++, e += 4) {
            int n = (int)e[2], i;
            if (e[0] == 0xFFFFFFFF) { fin = 1; break; }
            idx[carga_menus].clave = e[0]; idx[carga_menus].sec = e[1]; idx[carga_menus].n = e[2];
            if (e[0] == 0x30016) k_tit = carga_menus;
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

/* ---- emergencias (TILESA.BIN en A, TILES.BIN en B) */
void carga_falta(u32 c)
{
    if ((vid_lay() == 2 ? lba_tb : lba_ta) < 0 || c >= 32768 || RW(0xFEC298) == 0) return;   /* en el negro no se ve nada: lo trae la pantalla */
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
/* en B los pedidos de los 3 sectores siguientes de TILES.BIN vienen en la misma lectura (al anillo, libre: el fondo
   se corta antes): seguida cuesta casi nada frente a otra busqueda, y los tiles de un decorado van seguidos */
static void em_start(void)
{
    int k, n = 1;
    em_b = vid_lay() == 2;
    em_sec = em_b ? cola[qh] / 14 : cola[qh] / 12;
    if (em_b)
        for (k = 1; k < nq; k++) {
            u16 c = cola[(qh + k) % NQ]; int d = c / 14 - em_sec;
            if ((pedido[c >> 3] & (1 << (c & 7))) && d >= n && d < RING) n = d + 1;
        }
    if (cd_read_async((em_b ? lba_tb : lba_ta) + em_sec, n, em_b ? ring[0] : ebuf)) return;
    em_on = 1; em_n = n; em_t0 = T1; carga_emerg++; carga_emerg_sec += (u32)n;
}
static void em_corta(void) { if (em_on) { cd_cancel(); em_on = 0; } }
/* ---- fondo: secciones de MENUS (anillo), y de la demo: mapa y VO a la VRAM (anillo) o L2 y ROM al area (directo) */
static u8 *bg_dst(int i)
{
    if (bg_tipo == 2) return bg_dir + i * SEC;
    if (i - bg_got >= RING) return 0;         /* el anillo esta lleno: se volvera a buscar */
    return ring[i % RING];
}
static void bg_drain(void)
{
    int pos = cd_read_pos();
    if (bg_tipo == 2) { bg_got = pos; *bg_cuenta = bg_a + pos; return; }
    while (bg_got < pos && !bg_para) {
        const u8 *b = ring[bg_got % RING];
        if (!bg_tipo) procesa(b, bg_k, bg_a + bg_got);
        else if (dm_sector(b, 1) < 0) bg_para = 1;
        bg_got++;
    }
}
static void bg_corta(void)
{
    if (!bg_on) return;
    cd_cancel(); bg_drain(); bg_on = 0; bg_para = 0;
    if (!bg_tipo) vid_carga_fin();
}
static void bg_start(void)
{
    int a, b, k = bg_lista[bg_i];
    if (!rango(k, &a, &b)) { bg_i++; return; }
    bg_tipo = 0; bg_k = k; bg_a = a; bg_cnt = b - a + 1; bg_got = 0;
    if (cd_stream_async(lba_me + (int)idx[k].sec + a, bg_cnt, bg_dst)) return;
    bg_on = 1; vid_carga_ini(k, k == k_tit);
}
static void dm_stream_de(int lba, const u32 *x, int a, int n)
{
    bg_got = 0; bg_a = a;
    if (n > 0 && !cd_stream_async(lba + (int)x[0] + a, n, bg_dst)) bg_on = 1;
}
static void dm_stream(const u32 *x, int a, int n) { dm_stream_de(lba_dm[dm_r], x, a, n); }
/* la seccion ROM al area detras de la L2 que cabe (n sectores; en R1 la ROM va entera y la L2 se recorta por la cola);
   1 si se pidio */
static int dm_rom_trae(int n, const u32 *o)
{
    if ((u32)dm_rom >= o[1]) return 0;
    if (n + (int)o[1] > l2_cap) { dm_rom = -1; return 0; }
    bg_cuenta = &dm_rom; bg_dir = area_l2 + (n + dm_rom) * SEC; dm_stream(o, dm_rom, (int)o[1] - dm_rom);
    return 1;
}
/* sectores de la L2 del paquete r que van al area: lo que quepa; en R1 detras de ella va la ROM entera, que manda
   (la cola de la L2 son los codigos menos necesarios: los nunca visibles; lo que falte sale por emergencia) */
static int l2_n(int r)
{
    int n = (int)dmi[r][2][1] < l2_cap ? (int)dmi[r][2][1] : l2_cap;
#ifdef R1
    if (dmi[r][3][1] && n + (int)dmi[r][3][1] > l2_cap) n = l2_cap - (int)dmi[r][3][1];
    if (n < 0) n = 0;
#endif
    return n;
}
/* lo siguiente de la demo: mapa, VO, L2 y ROM, en ese orden (VRAM mientras haya sitio; area mientras quepa) */
static void dm_start(void)
{
    const u32 *m, *v, *l, *o; int n;
    if (!dm_fase || dm_r < 0 || !area) return;
    m = dmi[dm_r][0]; v = dmi[dm_r][1]; l = dmi[dm_r][2]; o = dmi[dm_r][3];
    bg_tipo = 1;
    if (!dm_lleno && (u32)dm_map < m[2]) { dm_stream(m, dm_map / 14, (int)m[1] - dm_map / 14); return; }
    if (dm_r == 4 && !dm_lleno && lba_gn >= 0 && (u32)dm_gn < gn[2]) { dm_stream_de(lba_gn, gn, dm_gn / 14, (int)gn[1] - dm_gn / 14); return; }
    if (!dm_lleno && (u32)dm_vo < v[2]) { dm_stream(v, dm_vo / 14, (int)v[1] - dm_vo / 14); return; }
    bg_tipo = 2;
    n = l2_n(dm_r);
#ifdef R1
    if (rom_arr) return;
    if (dm_rom >= 0 && !dm_rom_ok && (u32)dm_rom >= o[1] && o[1]) { bk_quita(); rom_pon(area_l2 + n * SEC, (int)o[1]); dm_rom_ok = 1; }
    if (!dm_rom_ok && dm_rom >= 0 && dm_rom_trae(n, o)) return;   /* en R1 la ROM antes que la L2: la demo la lee desde el principio */
#endif
    if (dm_l2 < n) { bg_cuenta = &dm_l2; bg_dir = area_l2 + dm_l2 * SEC; dm_stream(l, dm_l2, n - dm_l2); return; }
#ifndef R1
    if (dm_rom >= 0 && (u32)dm_rom < o[1]) dm_rom_trae(n, o);
#endif
}
void carga_atiende(void)
{
    int k;
    carga_poll();
    carga_sectores = (u32)cd_dbg[0];
    if (em_on) {
        int s = cd_read_state();
        if (s == 1) {
            u32 l = (u16)(T1 - em_t0);
            if (em_b) { for (k = 0; k < em_n; k++) procesa_b(ring[k]); } else procesa(ebuf, 0xFF, 0);
            em_on = 0;
            carga_emerg_lin += l; if (l > carga_emerg_max) carga_emerg_max = l;
        } else if (s < 0) {                   /* error o cortada por una carga bloqueante: el codigo se pide otra vez */
            u32 c = cola[qh]; em_on = 0;
            if (s == -1) { carga_err++; pedido[c >> 3] &= (u8)~(1 << (c & 7)); }
        }
    }
    if (bg_on) {
        int s;
        bg_drain();
        if (bg_para) { cd_cancel(); bg_on = 0; bg_para = 0; }   /* sin sitio en la VRAM: lo demas, otro dia */
        else if ((s = cd_read_state()) == 1) { bg_on = 0; if (!bg_tipo) { vid_carga_fin(); carga_fondo++; } }   /* el tramo siguiente, si queda, en bg_start */
        else if (s < 0) { bg_on = 0; if (!bg_tipo) { vid_carga_fin(); if (s == -1) { carga_err++; bg_i++; } } else if (s == -1) carga_err++; }   /* -2 cortada: se reanuda */
    }
    if (dm_l2reg < dm_l2) dm_l2_apunta();
    if (em_pendiente() && bg_on) bg_corta();   /* lo que se ve manda: el fondo se reanuda despues */
    if (!em_on && !bg_on) {
        if (em_pendiente()) em_start();
        else if (bg_i < bg_n) bg_start();
        else dm_start();
    }
}

#ifdef R1
/* ---- R1: la ROM del 68000 fuera del EXE (diseno 3). Al arrancar, ROM.BIN entera al area (3 s a 2x): los tramos
   residentes a rc_romres y todos los bloques a la tabla; al acabar la suma del arranque (PC 0x2616) se quita todo menos
   lo residente y el area queda libre. Bancos: la seccion ROM (clave 4) de DEMOr.PAK (de fondo, con el resto de la
   demo) o de STkk.PAK (bloqueando, al entrar en la carrera): sectores de tipo 2 que se compactan en su sitio (fuera las
   cabeceras), se cambian de orden (la ROM viene como en la placa) y se apuntan por tramos. Trampa: un bloque que no
   esta se lee de ROM.BIN (su sector y el principio del siguiente, por la cola) a una de 2 entradas por turno */
#define ROMFIN 0xDD800
static int lba_rom = -1;
u32 carga_rom_trampas, carga_rom_lin, carga_rom_max, carga_rom_kb;
static u8 rtmp[2 * SEC] __attribute__((aligned(4)));
#define NREMB 2                                 /* entradas de la trampa (2: la puerta G-RAM R1 con el residente de 97 KB; en psxprof2 no salta ninguna) */
static u8 remb[NREMB][SEC + 256] __attribute__((aligned(4)));
static int remb_s[NREMB] = {-1, -1}, remb_t;
#define NTR 192
static struct { u32 a0, a1; u8 *p; } btr[NTR]; static int btr_n;   /* tramos del banco apuntados desde el area */
static void rom_swap(u8 *p, u32 n)
{
    u32 *w = (u32 *)p;
    for (n >>= 2; n; n--, w++) { u32 x = *w; *w = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu); }
}
static void bk_quita(void)
{
    int i;
    for (i = 0; i < btr_n; i++) rc_rom_mapa(btr[i].a0, btr[i].a1, 0);
    if (btr_n) rc_rom_res_mapa();               /* por si un margen del banco tocaba un bloque residente */
    btr_n = 0;
}
static void rom_pon(u8 *b, int n)
{
    u8 *w = b; u32 fin = 0xFFFFFFFFu; int i;
    btr_n = 0;
    for (i = 0; i < n; i++) {
        u8 *s = b + i * SEC; u32 a, m, j;
        if (!cab(s, 2)) { carga_err++; break; }
        a = le32(s + 16); m = le32(s + 20);
        if (m > SEC - 32 || (m & 3)) { carga_err++; break; }
        if (a != fin) { if (btr_n == NTR) { carga_err++; break; } btr[btr_n].a0 = a; btr[btr_n].p = w; btr_n++; }
        for (j = 0; j < m; j += 4) *(u32 *)(w + j) = *(const u32 *)(s + 32 + j);   /* hacia delante: w <= s */
        w += m; fin = a + m; btr[btr_n - 1].a1 = fin - 4;
    }
    rom_swap(b, (u32)(w - b));
    for (i = 0; i < btr_n; i++) rc_rom_mapa(btr[i].a0, btr[i].a1, btr[i].p);
    carga_rom_kb = (u32)(w - b) >> 10;
}
static void rom_trampa(u32 a)
{
    u32 s = a >> 11, b; int k = remb_t; u8 *e = remb[k]; u16 t0 = T1, l;
    if (lba_rom < 0 || a >= ROMFIN) rc_bad(0xA00000 | a);
    remb_t = k + 1 < NREMB ? k + 1 : 0;
    if (remb_s[k] >= 0)                         /* la entrada se reusa: fuera sus bloques */
        for (b = (u32)remb_s[k] * 8; b < (u32)remb_s[k] * 8 + 8; b++) if (rc_romtab[b] == e - ((u32)remb_s[k] << 11)) rc_romtab[b] = 0;
    bg_corta(); em_corta(); snd_pausa(1);
    if (cd_read(lba_rom + (int)s, (s + 1) * SEC < ROMFIN ? 2 : 1, rtmp, 1)) rc_bad(0xA00000 | a);
    for (b = 0; b < SEC + 256; b += 4) *(u32 *)(e + b) = *(const u32 *)(rtmp + b);
    rom_swap(e, SEC + 256);
    for (b = s * 8; b < s * 8 + 8; b++) if (!rc_romtab[b]) rc_romtab[b] = e - (s << 11);
    remb_s[k] = (int)s; snd_pausa(0);
    l = (u16)(T1 - t0); carga_rom_trampas++; carga_rom_lin += l; if (l > carga_rom_max) carga_rom_max = l;
}
int carga_rom_arranque(void)
{
    int sz;
    if (!area || (u32)carga_area_kb << 10 < ROMFIN || cd_find("ROM.BIN", &lba_rom, &sz) || (u32)sz < ROMFIN) { lba_rom = -1; return -1; }
    if (cd_read(lba_rom, ROMFIN / SEC, area, 1)) return -1;
    rom_swap(area, ROMFIN);
    rc_rom_mapa(0, ROMFIN, area); rc_rom_residente(area);
    rc_romf_gancho = rom_trampa; rom_arr = 1;
    return 0;
}
void carga_rom_suma(void)
{
    if (!rom_arr) return;
    rc_rom_mapa(0, 0x100000, 0); rc_rom_res_mapa(); rom_arr = 0;
}
static void dm_nuevo(int r);
#endif

/* ---- el CD para otro (la SPU): se corta el fondo y la emergencia (se reanudan solos en carga_atiende) */
void carga_para(void) { bg_corta(); em_corta(); }

/* ---- la seccion de una pantalla, bloqueando (el juego esta congelado dentro de rc_vblank) */
static void bloq_cb(int i, const unsigned char *d)
{
    procesa(d, bloq_sec, bloq_a + i);
    bloq_hecho++;
    if (!bloq_vis && (u16)(T1 - bloq_t0) > LIN_05) { vid_cargando(bloq_fb); bloq_vis = 1; }
    if (bloq_vis && !(bloq_hecho & 3)) vid_cargando_barra(bloq_fb, bloq_hecho, bloq_total);
}
int carga_pantalla(u32 clave, int fb, int parcial)
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
        u32 l; int n = 0, hechos = 0;
        if (em_on) em_on = 0;                 /* cd_stream la cancela; el codigo sigue pedido */
        bloq_t0 = T1; bloq_vis = 0; bloq_fb = fb; bloq_sec = k; bloq_hecho = 0; bloq_total = (int)idx[k].n;
        vid_carga_ini(k, k == k_tit);
        do {                                  /* tramo a tramo (como mucho uno por sector: no se repite ninguno) */
            if (parcial && a < BLOQ_MAX && b >= BLOQ_MAX) b = BLOQ_MAX - 1;
            if (parcial && a >= BLOQ_MAX) break;   /* lo que sale despues (la seccion va por primer cuadro): de fondo */
            bloq_a = a;
            if (cd_stream(lba_me + (int)idx[k].sec + a, b - a + 1, bloq_cb, 1)) { carga_err++; break; }
            hechos += b - a + 1;
        } while (++n < MAXS && rango(k, &a, &b));
        vid_carga_fin();
        l = (u16)(T1 - bloq_t0); carga_bloq++; carga_bloq_lin += l; if (l > carga_bloq_max) carga_bloq_max = l;
    }
    bg_n = 0;
    if (rango(k, &a, &b)) bg_lista[bg_n++] = k;   /* lo que quedo de esta (seccion grande), lo primero de fondo */
    for (j = 1; j < carga_menus; j++) {
        int q = (k + j) % carga_menus;
        if ((idx[q].clave >> 16) == (clave >> 16) && rango(q, &a, &b)) bg_lista[bg_n++] = q;
    }
    bg_i = 0;
    return 0;
}

/* ---- la demo */
static void dm_nuevo(int r)
{   /* paquete de otra demo: todo por cargar */
#ifdef R1
    if (dm_rom_ok) bk_quita();
#endif
    dm_r = r; dm_map = dm_vo = dm_gn = dm_l2 = dm_rom = dm_l2reg = 0; dm_lleno = 0; dm_rom_ok = 0;
}
void carga_demo_titulo(int r)
{   /* titulo: prelectura de la demo r en b12-b15 y en el area; el fondo de MENUS se deja (la VRAM libre es para esto) */
    if (lba_dm[r] < 0 || !area) return;
    bg_corta(); bg_n = 0;
    if (r != dm_r) dm_nuevo(r);
    dm_map = dm_vo = 0; dm_lleno = 0;         /* lo de la VRAM de antes ya no esta */
    vid_lay_titulo(); dm_fase = 1;
}
void carga_demo_fin(void)
{   /* a los menus: con el reparto B, la VRAM de A se perdio; el area (L2 y ROM) se queda por si vuelve la misma demo */
    int l = vid_lay();
    if (!l && !dm_fase) return;
    if (bg_on && bg_tipo) bg_corta();
    if (l == 2) { em_corta(); pedidos_fuera(); menus_perdidos(); }
    vid_lay_a(); dm_fase = 0; dm_lleno = 0;
}
static void dm_bloq_cb(int i, const unsigned char *d)
{
    (void)i; dm_sector(d, 0);
    bloq_hecho++;
    if (!bloq_vis && (u16)(T1 - bloq_t0) > LIN_05) { vid_cargando(bloq_fb); bloq_vis = 1; }
    if (bloq_vis && !(bloq_hecho & 3)) vid_cargando_barra(bloq_fb, bloq_hecho, bloq_total);
}
/* registros [hechos, hasta) de la seccion x del fichero lba, bloqueando */
static void dm_bloq_de(int lba, const u32 *x, int hechos, int hasta)
{
    int a = hechos / 14, b = (hasta + 13) / 14;
    if (hechos >= hasta || lba < 0) return;
    bloq_hecho = 0; bloq_total = b - a;
    if (cd_stream(lba + (int)x[0] + a, b - a, dm_bloq_cb, 1)) carga_err++;
}
static void dm_bloq(const u32 *x, int hechos, int hasta) { if (dm_r >= 0) dm_bloq_de(lba_dm[dm_r], x, hechos, hasta); }
/* el paquete del tramo de la clave (FEC3C6, FEC3C8) en dmi[4] (formatos.md 8.3: STkk.PAK); 4, o -1 si no esta.
   Si es otro tramo, el area y lo cargado de antes no valen (dm_nuevo) */
static int st_pack(void)
{
    static const u8 tk[12] = {4, 8, 5, 2, 9, 7, 10, 6, 0, 11, 1, 3};
    static char stn[] = "ST00.PAK";
    int c6 = RW(0xFEC3C6), c8 = RW(0xFEC3C8), k, sz, i;
    if (!area || c6 > 3 || c8 > 2) return -1;
    k = tk[c6 * 3 + c8];
    {   /* los fijos del coche de la partida: FE8126, fijo desde elegir coche (0 L, 1 N, 2 R; FE8124 solo toma el valor
           del coche al empezar la carrera y en el mapa aun es el de antes) */
        u16 co = RW(0xFE8126); int g = co == 0 ? 1 : co == 2 ? 2 : 0;
        if (lba_g[g] < 0) g = 0;
        if (lba_g[g] != lba_gn) { lba_gn = lba_g[g]; gn = gi[g]; st_k = -1; }   /* otro coche: todo de nuevo */
    }
    if (k != st_k) {
        st_k = k; stn[2] = (char)('0' + k / 10); stn[3] = (char)('0' + k % 10);
        if (dm_r == 4 || dm_r < 0) dm_nuevo(-1);
        bg_corta(); em_corta();
        for (i = 0; i < 4; i++) dmi[4][i][0] = dmi[4][i][1] = dmi[4][i][2] = 0;
        if (cd_find(stn, &lba_dm[4], &sz) || cd_read(lba_dm[4], 1, ebuf, 1) || !cab(ebuf, 5)) lba_dm[4] = -1;
        else for (i = 0; i < ebuf[5] && i < 8; i++) {
            const u8 *e = ebuf + 32 + i * 16; u32 c = le32(e);
            if (c >= 1 && c <= 4) { dmi[4][c - 1][0] = le32(e + 4); dmi[4][c - 1][1] = le32(e + 8); dmi[4][c - 1][2] = le32(e + 12); }
        }
    }
    return lba_dm[4] >= 0 ? 4 : -1;
}
/* al reparto B (sin la prelectura del titulo o si la demo no era la prevista) */
static void dm_a_b(int r, int fase)
{
    int g = dm_fase == 1 && r == dm_r && vid_lay() == 1;
    em_corta(); pedidos_fuera();
    if (vid_lay() != 2) menus_perdidos();
    if (r != dm_r) dm_nuevo(r);
    if (!g) { dm_map = dm_vo = dm_gn = 0; }
    vid_lay_b(g); dm_fase = fase; dm_lleno = 0; dm_l2reg = 0;
    if (dm_r >= 0) dm_l2_apunta();
}
/* negro antes del mapa (ped 9; en la partida, el mapa del tramo 0xB) o de la demo (ped 5; carrera 1): bloqueando, lo
   que falte del mapa o del VO fijo (en carrera, GAMEN y el VO del tramo) */
void carga_demo_negro(int ped, int fb)
{
    int k = ped == 5 || ped == 1, carrera = ped == 0xB || ped == 1, r; u32 l;
    bloq_t0 = T1; bloq_vis = 0; bloq_fb = fb;
    bg_corta(); em_corta();                   /* cd_stream cortaria la emergencia: se vuelve a pedir despues */
    r = carrera ? st_pack() : (RW(0xFEC3C6) + 1) & 3;
    if (r >= 0 && (lba_dm[r] < 0 || !area)) r = -1;
    if (!k) {
        dm_a_b(r, 2);
        if (r >= 0) dm_bloq(dmi[r][0], dm_map, (int)dmi[r][0][2]);
    } else {
        if (vid_lay() != 2) dm_a_b(dm_r, 3);
        vid_quita_mapa(); dm_fase = 3; dm_lleno = 0;
        if (dm_r == 4) dm_bloq_de(lba_gn, gn, dm_gn, (int)gn[2]);
        if (dm_r >= 0) dm_bloq(dmi[dm_r][1], dm_vo, (int)dmi[dm_r][1][2] < NFIJ ? (int)dmi[dm_r][1][2] : NFIJ);
#ifdef R1
        if (dm_r >= 0 && !dm_rom_ok && dm_rom >= 0 && !rom_arr) {   /* lo que falte de la ROM de la demo */
            const u32 *o = dmi[dm_r][3]; int n = l2_n(dm_r);
            if (o[1] && n + (int)o[1] <= l2_cap) {
                if ((u32)dm_rom < o[1] && cd_read(lba_dm[dm_r] + (int)o[0] + dm_rom, (int)o[1] - dm_rom, area_l2 + (n + dm_rom) * SEC, 1)) carga_err++;
                else { dm_rom = (int)o[1]; bk_quita(); rom_pon(area_l2 + n * SEC, (int)o[1]); dm_rom_ok = 1; }
            }
        }
#endif
    }
    l = (u16)(T1 - bloq_t0); carga_dm_neg[k] = l; carga_dm_neg_n[k]++; if (l > carga_dm_neg_max[k]) carga_dm_neg_max[k] = l;
}
/* cada cuadro del mapa o de la demo (o del mapa del tramo y la carrera): si no hubo negro o el paquete no es el
   previsto (FEC3C6; en carrera la clave del tramo), todo de nuevo */
void carga_demo_modo(int modo, int fb)
{
    int carrera = modo == 0xB || modo == 1, f = (modo == 9 || modo == 0xB) ? 2 : 3;
    int r = carrera ? st_pack() : RW(0xFEC3C6) & 3;
    if (r >= 0 && (lba_dm[r] < 0 || !area)) r = -1;
    if (vid_lay() == 2 && dm_fase >= 2 && r == dm_r) { if (dm_fase < f) dm_fase = f; return; }
    carga_dm_fallo++;
    snd_pausa(1);
    bg_corta(); em_corta();
    dm_a_b(r, f);
    if (r >= 0 && f == 2) {                   /* el mapa del tramo (la clave cambia al entrar en el): bloqueando */
        bloq_t0 = T1; bloq_vis = 0; bloq_fb = fb;
        dm_bloq(dmi[r][0], dm_map, (int)dmi[r][0][2]);
    }
    snd_pausa(0);
}

/* Sonda cdfondo (diseno.md 6.2 y H2): lectura del CD a doble velocidad de fondo durante DURACION s (60) por variante
   del driver del WR1 (copia del FIFO por CPU y -DCD_DMA; las dos en este EXE: cd.c compilado dos veces, la de DMA con
   los simbolos renombrados d_* por objcopy en build.sh) mientras la CPU hace un cuadro como el del port: bloque
   "68000" del 35 % del cuadro que sondea cd_poll cada ~0,35 ms (16384 ciclos del 68000), bloque "dibujo" del 40 % con
   sondeos entre fases (cada ~1,5 ms) y espera del vblank sondeando en bucle como el WR1 (sin sondear en las 32 lineas
   antes del vblank previsto). El trabajo es de verdad (lecturas seguidas y al azar y escrituras a la RAM) para que
   el bus se parezca. DATOS.BIN (gen.py) se lee seguido con cd_stream_async y al acabar se vuelve al principio (una
   busqueda); cada sector lleva su indice y un xorshift32 sembrado con el. Por variante: sectores buenos, perdidos
   (indice mayor del esperado), repetidos (menor), malos (contenido), contadores del driver (cd_dbg), hueco maximo
   entre entradas a cd_poll con lectura activa (us; puerta <= 4 ms y 0 perdidos), ciclos de cd_poll por sector y por
   sondeo vacio, caudal. Al acabar las dos queda el resumen fijo con OK o FALLO por variante. Nada de la ROM. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef int s32;
#include "../../../wrally_ps1/ps1/cd.h"       /* driver del WR1 por ruta (t1_get viene de aqui) */
#ifndef DURACION
#define DURACION 60
#endif
/* la variante -DCD_DMA del mismo cd.c (simbolos d_*; no tiene externos, asi que el prefijo es limpio) */
int d_cd_init(void); void d_cd_poll(void); int d_cd_stream_async(int lba, int n, unsigned char *(*dst)(int i));
int d_cd_read_pos(void); int d_cd_read_state(void); void d_cd_cancel(void); void d_cd_sync(void); int d_cd_busy(void);
extern int d_cd_dbg[8];

#define REG(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)((a) | 0xA0000000))
#define GP1 REG(0x1F801814)
#define I_STAT REG(0x1F801070)
#define I_MASK REG(0x1F801074)
#define D2_MADR REG(0x1F8010A0)
#define D2_BCR REG(0x1F8010A4)
#define D2_CHCR REG(0x1F8010A8)
#define DPCR REG(0x1F8010F0)
#define T1 t1_get()

/* ------------------------------------------------------------ relojes */
/* fino: temporizador 2 a sysclk/8 (4233,6 cuentas por ms, 8 ciclos de CPU por cuenta), acumulado en 32 bits mientras se
   lea cada < 15 ms; grueso: temporizador 1 en lineas (15734 por s), que aguanta 4 s sin leerlo (cd_init y cd_sync no
   vuelven hasta acabar y en ellos no se lee ninguno: los segundos salen del grueso) */
static u32 tk, tl; static u16 t2p, t1p;
static u32 now(void)
{
    u16 a = R16(0x1F801120), b = T1;
    tk += (u16)(a - t2p); t2p = a; tl += (u16)(b - t1p); t1p = b;
    return tk;
}
#define CUADRO 70752u                       /* cuentas de T2 por cuadro NTSC */
#define BLK68 (CUADRO * 35 / 100)
#define BLKDIB (CUADRO * 40 / 100)
#define POLL68 1482u                        /* 0,35 ms */
#define POLLDIB 6350u                       /* 1,5 ms */
#define MS4 16934u                          /* 4 ms: la puerta */
#define US(c) ((c) * 100u / 423u)
#define LPS 15734u                          /* lineas por segundo */

/* ------------------------------------------------------------ GPU (como ps1/main.c): nodos de 16 palabras como mucho */
#define W 368
#define H 240
static void gpu_wait(void) { while (!(GP1 & (1 << 26))); }
static void dma_wait(void) { while (D2_CHCR & (1 << 24)); }
#define LSTN 16384
static u32 lst[LSTN];
static u32 *lp, *node;
static void l_begin(void) { node = lst; lp = lst + 1; }
static void l_close(u32 *next) { node[0] = ((u32)(lp - node - 1) << 24) | ((u32)next & 0xFFFFFF); }
static void l_room(int n)
{
    if (lp + n - node - 1 > 16 || lp + n + 2 >= lst + LSTN) {
        if (lp + n + 2 >= lst + LSTN) return;
        l_close(lp); node = lp; lp = node + 1;
    }
}
static int l_full(int n) { return lp + n + 2 >= lst + LSTN; }
static void l_send(void)
{
    l_close((u32 *)0xFFFFFF);
    dma_wait(); gpu_wait(); D2_MADR = (u32)lst; D2_BCR = 0; D2_CHCR = 0x01000401;
}
static void rect(int x, int y, int w, int h, u32 rgb)
{
    if (l_full(3)) return;
    l_room(3); lp[0] = 0x60000000 | rgb; lp[1] = ((u32)(y & 0xFFFF) << 16) | (x & 0xFFFF); lp[2] = ((u32)h << 16) | w; lp += 3;
}
#define FONDO 0x201000
static void l_marco(int k, int limpia)
{
    l_room(6);
    lp[0] = 0xE3000000 | ((k ? 256u : 0u) << 10); lp[1] = 0xE4000000 | ((((k ? 256u : 0u) + H - 1) << 10)) | (W - 1);
    lp[2] = 0xE5000000 | ((k ? 256u : 0u) << 11); lp += 3;
    if (limpia) { lp[0] = 0x02000000 | FONDO; lp[1] = (k ? 256u : 0u) << 16; lp[2] = (H << 16) | W; lp += 3; }
}

/* ------------------------------------------------------------ texto: cifras y letras de 3 x 5 a escala s */
static const u16 font[40] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF,
    0x2BED, 0x6BAE, 0x7927, 0x6B6E, 0x79E7, 0x79E4, 0x796F, 0x5BED, 0x7497, 0x126F, 0x5BAD, 0x4927, 0x5FED,
    0x6B6D, 0x7B6F, 0x7BE4, 0x7B79, 0x7BAD, 0x79CF, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
    0x0000, 0x01C0, 0x0002, 0x0410};
static int gl(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + c - 'A';
    return c == '-' ? 37 : c == '.' ? 38 : c == ':' ? 39 : 36;
}
static void glifo(int x, int y, int s, int g, u32 rgb)
{
    int r, c;
    for (r = 0; r < 5; r++) {
        int b = (font[g] >> (12 - r * 3)) & 7, c0 = -1;
        for (c = 0; c <= 3; c++) {
            int on = c < 3 && (b & (4 >> c));
            if (on && c0 < 0) c0 = c;
            if (!on && c0 >= 0) { rect(x + c0 * s, y + r * s, (c - c0) * s, s, rgb); c0 = -1; }
        }
    }
}
#define CX(col, s) (4 + (col) * 4 * (s))
static void txt(int x, int y, int s, const char *p, u32 rgb) { for (; *p; p++, x += 4 * s) if (*p != ' ') glifo(x, y, s, gl(*p), rgb); }
static void borra(int x, int y, int s, int n) { rect(x - 1, y - 1, n * 4 * s, 5 * s + 2, FONDO); }
/* numero a la derecha de n cifras sin ceros delante (si no cabe se pierden las altas), sobre su celda borrada */
static void numr(int x, int y, int s, u32 v, int n, u32 rgb)
{
    char b[12]; int i;
    borra(x, y, s, n);
    for (i = n - 1; i >= 0; i--) { b[i] = v || i == n - 1 ? '0' + v % 10 : ' '; v /= 10; }
    b[n] = 0; txt(x, y, s, b, rgb);
}
static void txtb(int x, int y, int s, int n, const char *p, u32 rgb) { borra(x, y, s, n); txt(x, y, s, p, rgb); }
#define GRIS 0x909090
#define BLANCO 0xFFFFFF
#define ROJO 0x3030FF
#define VERDE 0x30D030
#define AMARILLO 0x30D0D0
#define MAL(v) ((v) ? ROJO : BLANCO)

/* ------------------------------------------------------------ las dos variantes del driver, por tabla */
typedef struct {
    int (*init)(void); void (*poll)(void); int (*stream)(int, int, u8 *(*)(int)); int (*pos)(void); int (*state)(void);
    void (*cancel)(void); void (*sync)(void); int (*busy)(void); int *dbg;
} drv_t;
static const drv_t drv_cpu = {cd_init, cd_poll, cd_stream_async, cd_read_pos, cd_read_state, cd_cancel, cd_sync, cd_busy, cd_dbg};
static const drv_t drv_dma = {d_cd_init, d_cd_poll, d_cd_stream_async, d_cd_read_pos, d_cd_read_state, d_cd_cancel, d_cd_sync, d_cd_busy, d_cd_dbg};
typedef struct {
    u32 buenos, perdidos, repetidos, malos;   /* sectores entregados a la sonda, por su indice y contenido */
    u32 hueco_max, hueco4; char hueco_src;    /* entre entradas a cd_poll con lectura activa (cuentas de T2); los > 4 ms;
                                                 donde se dio el maximo: 6 bloque 68000, D dibujo, V espera del vblank */
    u32 cs_sum, cs_n, cs_max, ce_sum, ce_n;   /* cuentas dentro de cd_poll: con sector nuevo / sin nada */
    u32 vueltas, tarde, cuadros, seg, t_ini, espera; int init, fin, ok;
} est_t;
static est_t est[2]; static est_t *E = est; static const drv_t *D = &drv_cpu;
static int fase;                              /* 0 copia por CPU, 1 CD_DMA, 2 fin, 3 sin DATOS.BIN */
static int lba, nsect, verif; static u32 t_sond, cuadros; static char src;
static u8 ring[8][2048] __attribute__((aligned(4)));
static u8 *dst(int i) { return ring[i & 7]; }
static u32 semilla(u32 i) { u32 s = i * 0x9E3779B1u ^ 0xA5A5A5A5u; return s ? s : 1; }
/* el sector que debia ser el esp (gen.py): indice y flujo */
static void comprueba(const u32 *p, u32 esp)
{
    u32 i = p[0], s; int k;
    if (i >= (u32)nsect) { E->malos++; return; }
    if (i != esp) { if (i > esp) E->perdidos++; else E->repetidos++; return; }
    s = semilla(i);
    for (k = 1; k < 512; k++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; if (p[k] != s) { E->malos++; return; } }
    E->buenos++;
}
/* un sondeo con sus medidas; luego comprueba lo que haya llegado (el anillo tiene 8 sectores: llega uno cada 6,67 ms
   y se sondea cada < 4 ms; el driver los da en orden) */
static void sondeo(void)
{
    u32 t0 = now(), dt; int s0 = D->dbg[0];
    if (D->state() == 0) { u32 g = t0 - t_sond; if (g > E->hueco_max) { E->hueco_max = g; E->hueco_src = src; } if (g > MS4) E->hueco4++; }
    t_sond = t0;
    D->poll();
    dt = now() - t0;
    if (D->dbg[0] != s0) { E->cs_sum += dt; E->cs_n++; if (dt > E->cs_max) E->cs_max = dt; } else { E->ce_sum += dt; E->ce_n++; }
    while (verif < D->pos()) { comprueba((const u32 *)ring[verif & 7], verif); verif++; }
}
static void empieza(void) { verif = 0; D->stream(lba, nsect, dst); t_sond = now(); }
static void arranca(void) { now(); E->t_ini = tl; E->seg = 0; empieza(); }
static u32 entregados(void) { return E->buenos + E->perdidos + E->repetidos + E->malos; }

/* ------------------------------------------------------------ carga de CPU */
#define WRK_W 16384
static u32 wa[WRK_W], wb[WRK_W]; static u32 wpos, wacc;
/* ~0,1 ms de trabajo de verdad: lectura seguida, lectura al azar (como la tabla de ROM) y escritura, todo a la RAM
   (la PS1 no tiene cache de datos), como el 68000 recompilado con su RAM */
static void trabajo(void)
{
    u32 *a = wa + wpos, *b = wb + wpos; int k;
    for (k = 0; k < 128; k++) { u32 v = a[k]; wacc += v + wa[(wacc >> 5) & (WRK_W - 1)]; b[k] = v ^ wacc; }
    wpos = (wpos + 128) & (WRK_W - 1);
}
/* trabajo hasta gastar dur cuentas desde t0, sondeando cada cada cuentas */
static void ocupa(u32 t0, u32 dur, u32 cada)
{
    u32 tp = now();
    while (tk - t0 < dur) { trabajo(); now(); if (tk - tp >= cada) { sondeo(); tp = tk; } }
}
static void bloque68(void) { src = '6'; ocupa(now(), BLK68, POLL68); }

/* ------------------------------------------------------------ pantalla */
static int fb;
#define FY(r) (34 + (r) * 12)
static const char *const filas[15] = {"SEGUNDOS", "SECTORES OK", "PERDIDOS", "REPETIDOS", "MALOS", "DRV SECTORES",
    "DRV PERDIDOS", "DRV ERR VIG COD", "HUECO MAX US", "HUECOS 4 MS", "CICLOS SECTOR", "CICLOS MAX", "CICLOS VACIO",
    "KB/S", "VUELTAS TARDE"};
static void rotulos(void)
{
    int r;
    txt(CX(0, 3), 4, 3, "CDFONDO", GRIS); txt(CX(8, 3), 4, 3, "FASE", GRIS); txt(CX(20, 3), 4, 3, "T", GRIS);
    txt(CX(20, 2), 22, 2, "COPIA CPU", AMARILLO); txt(CX(38, 2), 22, 2, "CD DMA", AMARILLO);
    for (r = 0; r < 15; r++) txt(CX(0, 2), FY(r), 2, filas[r], GRIS);
    txt(CX(0, 3), 222, 3, "CPU", GRIS); txt(CX(12, 3), 222, 3, "DMA", GRIS);
}
static void hex2(int x, int y, int s, u32 v, u32 rgb)
{
    char b[3]; b[0] = "0123456789ABCDEF"[(v >> 4) & 15]; b[1] = "0123456789ABCDEF"[v & 15]; b[2] = 0;
    txt(x, y, s, b, rgb);
}
/* columna de una variante (x: columna de la primera cifra) */
static void columna(int c, int x)
{
    const est_t *e = &est[c]; const int *g = c ? d_cd_dbg : cd_dbg;
    u32 us = US(e->hueco_max), kbs = e->seg ? (e->buenos + e->perdidos + e->repetidos + e->malos) * 2 / e->seg : 0;
    numr(x, FY(0), 2, e->seg, 7, BLANCO);
    numr(x, FY(1), 2, e->buenos, 7, BLANCO);
    numr(x, FY(2), 2, e->perdidos, 7, MAL(e->perdidos));
    numr(x, FY(3), 2, e->repetidos, 7, MAL(e->repetidos));
    numr(x, FY(4), 2, e->malos, 7, MAL(e->malos));
    numr(x, FY(5), 2, g[0], 7, BLANCO);
    numr(x, FY(6), 2, g[1], 7, MAL(g[1]));
    numr(x, FY(7), 2, g[2], 3, MAL(g[2])); numr(x + 24, FY(7), 2, g[3], 3, MAL(g[3])); borra(x + 56, FY(7), 2, 2); hex2(x + 56, FY(7), 2, g[5], g[2] ? ROJO : GRIS);
    numr(x, FY(8), 2, us, 6, us > 4000 ? ROJO : BLANCO);
    borra(x + 48, FY(8), 2, 1); if (e->hueco_src) glifo(x + 48, FY(8), 2, gl(e->hueco_src), GRIS);
    numr(x, FY(9), 2, e->hueco4, 7, MAL(e->hueco4));
    numr(x, FY(10), 2, e->cs_n ? e->cs_sum * 8 / e->cs_n : 0, 7, BLANCO);
    numr(x, FY(11), 2, e->cs_max * 8, 7, BLANCO);
    numr(x, FY(12), 2, e->ce_n ? e->ce_sum * 8 / e->ce_n : 0, 7, BLANCO);
    numr(x, FY(13), 2, kbs, 7, e->seg >= 5 && kbs < 250 ? ROJO : BLANCO);
    numr(x, FY(14), 2, e->vueltas, 3, BLANCO); numr(x + 24, FY(14), 2, e->tarde, 4, BLANCO);
}
static void veredicto(int c, int x)
{
    const est_t *e = &est[c];
    txtb(x, 222, 3, 6, e->fin ? (e->ok ? "OK" : "FALLO") : e->init ? "..." : "", e->fin ? (e->ok ? VERDE : ROJO) : GRIS);
}
/* bloque de dibujo: las cifras en dos fases con sondeos entre ellas, la lista a la GPU y trabajo hasta el 40 % */
static void bloque_dibujo(void)
{
    u32 t0 = now();
    src = 'D';
    l_begin(); l_marco(fb, 0);
    txtb(CX(13, 3), 4, 3, 6, fase == 0 ? "CPU" : fase == 1 ? "DMA" : fase == 2 ? "FIN" : "NODATA", fase == 3 ? ROJO : fase == 2 ? BLANCO : VERDE);
    numr(CX(22, 3), 4, 3, tl / LPS, 4, BLANCO);
    columna(0, CX(22, 2)); sondeo();
    columna(1, CX(37, 2)); sondeo();
    veredicto(0, CX(4, 3)); veredicto(1, CX(16, 3));
    l_send(); sondeo();
    ocupa(t0, BLKDIB, POLLDIB);
    sondeo();                                 /* donde el port entra en bg_service antes de esperar el vblank */
}
/* espera del vblank como el WR1: sondeando, salvo de 32 lineas antes a 2 despues de donde toca (leer un sector tarda
   hasta ~11 lineas y retrasaria el cambio de bufer) */
static u16 t_vbl;
static void vsync(void)
{
    u16 tp = T1, ph; int late = 0;
    src = 'V';
    if (I_STAT & 1) E->tarde++;               /* el trabajo del cuadro se paso del vblank */
    I_STAT = ~1u;
    while (!(I_STAT & 1)) {
        tp = T1; ph = (u16)(tp - t_vbl) % 263; late = 0;
        if (ph >= 2 && ph < 263 - 32) { sondeo(); late = (u16)(T1 - tp) >= 2; }
    }
    if (!late) t_vbl = tp;
    I_STAT = ~1u;
}
/* una vez por cuadro (psxprof cuenta las llamadas): mostrar lo dibujado y llevar las fases */
void __attribute__((noinline)) rc_vblank(void)
{
    GP1 = 0x05000000 | ((fb ? 256 : 0) << 10); fb ^= 1;
    cuadros++;
    if (fase > 1) return;
    E->cuadros++; now(); E->seg = (tl - E->t_ini) / LPS;
    if (E->seg >= DURACION) {
        D->cancel(); D->sync(); E->fin = 1;
        E->ok = E->buenos && !E->perdidos && !E->repetidos && !E->malos && !D->dbg[1] && US(E->hueco_max) <= 4000;
        if (++fase == 1) { D = &drv_dma; E = &est[1]; D->init(); E->init = 1; arranca(); }
    } else if (D->state() != 0) {             /* se acabo el fichero: con la unidad ya parada (como entre dos cargas del
                                                 port; si no, cd.c cuenta como perdidos los sectores de detras del fin
                                                 que llegan antes de la busqueda), otra vez desde el principio */
        if (++E->espera >= 10 && !D->busy()) { E->espera = 0; E->vueltas++; empieza(); }
    }
}

int main(void)
{
    int i, size; u32 s = 12345;
    __asm__ volatile ("mtc0 $0, $12");
    *(volatile u32 *)0xFFFE0130 = 0x0001E988;     /* cache de instrucciones activada (como la BIOS) */
    I_MASK = 0;
    DPCR |= 0x800;
    GP1 = 0x00000000;
    GP1 = 0x04000002;
    GP1 = 0x08000041;                             /* 368 x 240 NTSC */
    GP1 = 0x06C70260;
    GP1 = 0x07040010;
    *(volatile u16 *)0x1F801114 = 0x0100;         /* temporizador 1: cuenta lineas */
    *(volatile u16 *)0x1F801124 = 0x0200;         /* temporizador 2: sysclk / 8, libre */
    for (i = 0; i < WRK_W; i++) { s = s * 1103515245u + 12345u; wa[i] = s; }
    for (i = 0; i < 2; i++) { l_begin(); l_marco(i, 1); rotulos(); l_send(); }
    dma_wait(); gpu_wait(); GP1 = 0x05000000; GP1 = 0x03000000;
    t2p = R16(0x1F801120); t1p = T1; now();
    D->init(); E->init = 1;
    if (cd_find("DATOS.BIN", &lba, &size) || size < 2048) fase = 3;
    else { nsect = size / 2048; arranca(); }
    for (;;) { bloque68(); bloque_dibujo(); vsync(); rc_vblank(); }
}

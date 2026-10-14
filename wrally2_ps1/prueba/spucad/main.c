/* Sonda spucad (diseno.md 5.2 y H2): encadenado de trozos de sonido en la SPU por la direccion de repeticion con las
   reglas del juego (PS-ADPCM, banderas 3 al final, KON y luego repeticion = silencio, encolar solo cuando la voz ya
   toca el anterior con guarda de 1 cuadro y >= 111 ms antes del final, parar = volumen 0, tono 0x2DF) en las voces
   0, 4, 5 y 6 durante DURACION s. Cada paso de trozo se detecta por ENDX y se compara con el calendario de la voz:
   el reloj es el temporizador 2 (sysclk/8: 96 cuentas por muestra de la SPU, el mismo cristal) y los trozos duran
   distinto, asi que el tiempo entre dos ENDX dice que trozo acaba de sonar: el esperado (visto), el anterior
   (repetido), otro (saltado) o el silencio. Si ENDX no se borra al escribirlo (MiSTer) solo se ve el primer paso de
   cada KON y el resto va por calendario, como en el juego. El IRQ de la SPU vigila ademas una voz cada vez (direccion
   del trozo encolado: salta cuando la voz entra en el). Se comprueba que la direccion de repeticion sigue siendo lo
   escrito y que el ADSR no se apaga. Pantalla de cifras; al acabar queda fija con OK o FALLO. ROMPE=1: encola el
   siguiente sin esperar a que la voz toque el anterior (saltados); ROMPE=2: no pone silencio al entrar y encola
   tarde (repetidos). Nada de la ROM. Uso personal. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef int s32;
#include "trozos.h"
#ifndef DURACION
#define DURACION 240
#endif
#ifndef ROMPE
#define ROMPE 0
#endif
#define REG(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)((a) | 0xA0000000))   /* por KSEG1: sin la cola de escritura */
#define R32(a) (*(volatile u32 *)((a) | 0xA0000000))
#define GP0 REG(0x1F801810)
#define GP1 REG(0x1F801814)
#define I_STAT REG(0x1F801070)
#define I_MASK REG(0x1F801074)
#define D2_MADR REG(0x1F8010A0)
#define D2_BCR REG(0x1F8010A4)
#define D2_CHCR REG(0x1F8010A8)
#define DPCR REG(0x1F8010F0)
#define SPUCNT R16(0x1F801DAA)
#define SPUSTAT R16(0x1F801DAE)
#define SPUADDR R16(0x1F801DA6)
#define SPUIRQA R16(0x1F801DA4)
#define ENDX R16(0x1F801D9C)
#define VOICE(v, r) R16(0x1F801C00 + (v) * 16 + (r))
#define PITCH 0x2DF
#define SIL8 (0x1000 >> 3)                  /* bloque de silencio (se repite a si mismo), en unidades del registro */
#define VOL 0x2000
#define NV 4
extern const u8 tz_datos[];

/* ------------------------------------------------------------ reloj: temporizador 2 a sysclk/8 */
/* 96 cuentas por muestra de la SPU, 4233,6 por ms, 70752 por cuadro NTSC; se suma en 32 bits (vuelta a los 1014 s) */
static u32 tk; static u16 t2p;
static u32 now(void) { u16 a = R16(0x1F801120); tk += (u16)(a - t2p); t2p = a; return tk; }
#define MS(x) ((u32)(x) * 21168u / 5u)
#define SEG 4233600u
#define CUADRO 70752u
#define TOL MS(10)                           /* paso visto a +-10 ms del calendario (los trozos se llevan >= 50 ms) */
#define TARDE_MAX (3 * CUADRO)               /* sin ENDX 3 cuadros despues del fin previsto: paso perdido */
#define MARGEN MS(111)
#define SILDUR 14980u                        /* una vuelta del bloque de silencio: 28 muestras x 4096 x 96 / 0x2DF */
#define SILK NT
static u32 dur[NT];                          /* duracion de cada trozo en cuentas */
static int antes(u32 a, u32 b) { return (s32)(a - b) < 0; }
static inline u16 t1_get(void)
{
    volatile u16 *r = (volatile u16 *)0xBF801110; u16 a = *r, b;
    while ((b = *r) != a) a = b;
    return a;
}
static void lineas(int n) { u16 a = t1_get(); while ((u16)(t1_get() - a) < (u16)n); }

/* ------------------------------------------------------------ GPU (como ps1/main.c) */
#define W 368
#define H 240
static void gpu_wait(void) { while (!(GP1 & (1 << 26))); }
static void dma_wait(void) { while (D2_CHCR & (1 << 24)); }
/* lista de la GPU: nodos de hasta 16 palabras (con nodos mas largos Mednafen pierde palabras y la GPU se cuelga) */
#define LSTN 16384
static u32 lst[LSTN];
static u32 *lp, *node;
static void l_begin(void) { node = lst; lp = lst + 1; }
static void l_close(u32 *next)
{
    u32 n = (u32)(lp - node - 1);
    node[0] = (n << 24) | ((u32)next & 0xFFFFFF);
}
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
    for (r = 0; r < 5; r++) {                /* cada fila en tramos seguidos: un rectangulo por tramo */
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
static void num(int x, int y, int s, u32 v, int n, u32 rgb)
{
    char b[12]; int i;
    for (i = n - 1; i >= 0; i--) { b[i] = '0' + v % 10; v /= 10; }
    b[n] = 0; txt(x, y, s, b, rgb);
}
static void hex4(int x, int y, int s, u32 v, u32 rgb)
{
    char b[5]; int i;
    for (i = 3; i >= 0; i--) { b[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    b[4] = 0; txt(x, y, s, b, rgb);
}
#define GRIS 0x909090
#define BLANCO 0xFFFFFF
#define ROJO 0x3030FF
#define VERDE 0x30D030
#define MAL(v) ((v) ? ROJO : BLANCO)

/* ------------------------------------------------------------ SPU */
static void spu_wait_mode(u16 m) { int n; for (n = 0; n < 100000 && (SPUSTAT & 0x3F) != (m & 0x3F); n++); }
static void spu_dma_write(u32 addr, const void *src, int bytes)
{
    int n;
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
    SPUADDR = addr >> 3;
    SPUCNT = (SPUCNT & ~0x30) | 0x20; spu_wait_mode(SPUCNT);
    R32(0x1F8010C0) = (u32)src;
    R32(0x1F8010C4) = ((bytes / 64) << 16) | 16;
    R32(0x1F8010C8) = 0x01000201;
    for (n = 0; n < 1000000 && (R32(0x1F8010C8) & 0x01000000); n++);
    for (n = 0; n < 100000 && (SPUSTAT & 0x400); n++);
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
}
static void spu_init(void)
{
    int v;
    R32(0x1F8010F0) |= 0x80000;                           /* DMA 4 (SPU) */
    SPUCNT = 0; spu_wait_mode(0);
    R16(0x1F801D80) = 0x3FFF; R16(0x1F801D82) = 0x3FFF;   /* volumen general */
    R16(0x1F801D84) = 0; R16(0x1F801D86) = 0;             /* reverberacion */
    R16(0x1F801D8C) = 0xFFFF; R16(0x1F801D8E) = 0x00FF;   /* apagar voces */
    R16(0x1F801D90) = 0; R16(0x1F801D92) = 0; R16(0x1F801D94) = 0; R16(0x1F801D96) = 0;
    R16(0x1F801D98) = 0; R16(0x1F801D9A) = 0;
    R16(0x1F801DB0) = 0; R16(0x1F801DB2) = 0;             /* sin audio del CD ni entrada externa */
    R16(0x1F801DB4) = 0; R16(0x1F801DB6) = 0;
    R16(0x1F801DAC) = 0x0004;
    SPUIRQA = 0x7FFF0 >> 3;                               /* direccion del IRQ fuera de todo mientras no vigila */
    R16(0x1F801DA2) = 0xFFFE;                             /* la reverberacion, aun apagada, lee su area de trabajo (Mednafen:
                                                             desde la base barre los 512 KB cada 12 s) y eso tambien dispara
                                                             el IRQ: area en los ultimos 16 bytes, lejos de los trozos */
    SPUCNT = 0xC000;                                      /* SPU, sin silencio, sin IRQ */
    for (v = 0; v < 24; v++) { VOICE(v, 0) = 0; VOICE(v, 2) = 0; VOICE(v, 14) = SIL8; }
    lineas(4);
}
static u16 t_key;                                         /* KON: al menos 2 lineas entre escrituras (snd.c del WR1) */
static void spu_kon(int ch)
{
    while ((u16)(t1_get() - t_key) < 2);
    R16(0x1F801D88) = 1 << ch; t_key = t1_get();
}

/* ------------------------------------------------------------ voces y cuentas */
typedef struct {
    int idx, ch, modo;          /* voz de la SPU; encolado: 0 en la guarda, 1 a 111 ms del final, 2 al azar entre ambos */
    int cur, prev, nxt, plan;   /* trozo sonando (-1 silencio), anterior, encolado sin consumir, siguiente previsto */
    int on, endx_ok, cola, mudo, guarda, corte_kon;
    u32 t_in, t_end, t_plan, t_kon, t_corte, t_esc, rep, seed, pasos;
} voz_t;
static voz_t vz[NV];
static u32 irq_ok, irq_fallo, irq_canc, irq_dev_max, endx_dev_max, adsr0, rep_dif;
static int endx_borrable, endx_fijo, irq_v = -1, irq_k; static u32 irq_t, irq_t0;
static int dbg_v, dbg_k, dbg_ant, dbg_cur; static u32 dbg_arm;   /* el peor disparo del IRQ: para entender un desvio */
static const u8 canal[NV] = {0, 4, 5, 6}, cls_lo[NV] = {0, 4, 8, 0}, cls_hi[NV] = {3, 7, 11, 11};
static const u8 cad_lo[NV] = {3, 0, 0, 2}, cad_hi[NV] = {8, 0, 0, 6};   /* trozos por cadena (0: sin fin) */
static u32 esperados, vistos, saltados, repetidos, tarde, perdidos, otros, sil_bucles, kons, encolados, paradas;
static int fin, final, fallo; static u32 t_fin, cuadros;
static u32 rnd(voz_t *v) { v->seed = v->seed * 1103515245u + 12345u; return (v->seed >> 16) & 0x7FFF; }
/* trozo al azar de la clase de la voz, distinto del actual y del anterior (asi la duracion entre dos ENDX dice cual
   sono); si se puede, tambien distinto de lo que tocan o tienen encolado las otras voces y del que vigila el IRQ
   (dos voces en el mismo trozo dispararian el IRQ antes de tiempo y se mezclarian al oido) */
static int elige(voz_t *v)
{
    int k, i, j, n = cls_hi[v->idx] - cls_lo[v->idx] + 1;
    for (j = 0; j < 40; j++) {
        int ocupado = 0;
        k = cls_lo[v->idx] + rnd(v) % n;
        if (k == v->cur || k == v->prev || (irq_v >= 0 && k == irq_k)) continue;
        if (j >= 32) break;                                   /* sin candidato libre: vale uno que toque otra voz */
        for (i = 0; i < NV; i++) if (i != v->idx && (k == vz[i].cur || k == vz[i].nxt || k == vz[i].plan)) ocupado = 1;
        if (!ocupado) break;
    }
    return k;
}
static void escribe_rep(voz_t *v, u32 a8) { VOICE(v->ch, 14) = a8; v->rep = a8; v->t_esc = now(); }
static int trozo_de(u32 a8) { int k; for (k = 0; k < NT; k++) if (tz_addr[k] == a8) return k; return -1; }
static int match(u32 d)
{
    int k;
    if (d < 2 * SILDUR + TOL) return SILK;
    for (k = 0; k < NT; k++) if (d + TOL >= dur[k] && d <= dur[k] + TOL) return k;
    return -1;
}
static void irq_off(void) { SPUCNT = SPUCNT & ~0x40; I_STAT = ~0x200u; irq_v = -1; }
static void irq_arma(voz_t *v, int k, u32 t)
{
    int i;
    if (irq_v >= 0) return;
    for (i = 0; i < NV; i++) if (i != v->idx && (k == vz[i].cur || k == vz[i].nxt || k == vz[i].plan)) return;   /* otra voz lo toca o lo tocara: ambiguo */
    irq_v = v->idx; irq_k = k; irq_t = v->cur >= 0 ? v->t_end : t + SILDUR; irq_t0 = t;
    I_STAT = ~0x200u; SPUIRQA = tz_addr[k]; SPUCNT = SPUCNT | 0x40;
}
/* al entrar en un trozo: cuando encolar el siguiente (ventana: de la guarda a 111 ms del final) y el fin de cadena */
static void planificar(voz_t *v)
{
    u32 a = v->t_in + CUADRO, b = v->t_end - MARGEN;
    v->plan = -1;
    if (fin) { v->cola = 0; return; }
    if (v->cola > 0) {
        v->plan = elige(v); if (cad_hi[v->idx]) v->cola--;
        if (ROMPE == 2) v->t_plan = v->t_end + CUADRO;             /* tarde a proposito */
        else if (v->modo == 0) v->t_plan = a;
        else if (v->modo == 1) v->t_plan = b;
        else v->t_plan = a + rnd(v) % (b - a + 1);
    } else if (v->idx == 0) v->t_corte = v->t_in + 1 + rnd(v) % dur[v->cur];   /* parar (volumen 0) en medio */
    else if (v->idx == 3 && (rnd(v) & 1)) v->corte_kon = 1, v->t_kon = v->t_in + CUADRO + rnd(v) % (dur[v->cur] - CUADRO);
}
static void entra(voz_t *v, int k, u32 t)
{
    v->prev = v->cur; v->cur = k; v->t_in = t; v->nxt = -1; v->guarda = 0;
    if (k >= 0) { v->t_end = t + dur[k]; v->guarda = ROMPE != 2; planificar(v); }
    else {
        v->t_end = t + SILDUR;
        if (!fin && cad_hi[v->idx] && !v->corte_kon) v->t_kon = t + MS(300) + rnd(v) % MS(1200);
    }
}
static void kon(voz_t *v, int k)
{
    u32 t;
    if (v->on && v->cur >= 0) esperados--;                /* trozo cortado: su paso ya no llega */
    if (v->on && v->nxt >= 0) esperados--;
    if (irq_v == v->idx) { irq_off(); irq_canc++; }
    VOICE(v->ch, 0) = VOL; VOICE(v->ch, 2) = VOL; v->mudo = 0;
    VOICE(v->ch, 4) = PITCH; VOICE(v->ch, 6) = tz_addr[k]; VOICE(v->ch, 8) = 0x00FF; VOICE(v->ch, 10) = 0;
    spu_kon(v->ch); t = now();
    lineas(2);                                            /* que la SPU vea el KON antes de tocar la repeticion */
    escribe_rep(v, SIL8);
    v->on = 1; v->endx_ok = 1; v->prev = -1; v->cur = k; v->t_in = t; v->t_end = t + dur[k];
    v->nxt = -1; v->guarda = 0; v->corte_kon = 0; v->t_kon = 0; v->t_corte = 0;
    v->cola = cad_hi[v->idx] ? cad_lo[v->idx] + rnd(v) % (cad_hi[v->idx] - cad_lo[v->idx] + 1) : 1;
    planificar(v); kons++; esperados++;
}
static void encola(voz_t *v, u32 t)
{
    int k = v->plan;
    if (v->cur < 0 || !antes(t, v->t_end)) tarde++;       /* la voz ya acabo el anterior (o esta en silencio) */
    escribe_rep(v, tz_addr[k]);
    if (v->nxt < 0) v->nxt = k;                            /* lo que el juego cree que sonara despues */
    irq_arma(v, k, t);
    encolados++; esperados++; v->plan = -1;
    if (ROMPE == 1 && v->cola > 0) { v->plan = elige(v); if (cad_hi[v->idx]) v->cola--; v->t_plan = t + CUADRO; }
}
/* la voz acaba de pasar al bloque de repeticion: que trozo sono (por su duracion) y adonde ha ido */
static void paso_endx(voz_t *v, u32 t)
{
    u32 d = t - v->t_in; int k = match(d), dest;
    u16 env = VOICE(v->ch, 12);
    if (v->cur >= 0) {
        if (k == v->cur) {
            u32 dv = d > dur[k] ? d - dur[k] : dur[k] - d;
            if (dv > endx_dev_max) endx_dev_max = dv;
            vistos++; v->pasos++; dest = v->nxt;
        } else if (k == v->prev && k >= 0) { repetidos++; dest = trozo_de(v->rep); }
        else if (k >= 0 && k < NT) { saltados++; dest = trozo_de(v->rep); }
        else { otros++; dest = trozo_de(v->rep); }
        if (!v->mudo && env == 0) adsr0++;
    } else {                                              /* en silencio segun el modelo */
        u32 de = t - v->t_esc;                            /* desde la ultima escritura de repeticion */
        if (v->nxt >= 0 && de < 2 * SILDUR + TOL) { vistos++; v->pasos++; dest = v->nxt; }   /* sale al trozo escrito tarde */
        else if (v->nxt < 0 && k == SILK) { sil_bucles++; v->t_in = t; return; }
        else if (k == v->prev && k >= 0) { repetidos++; dest = trozo_de(v->rep); }   /* seguia sonando el anterior */
        else { otros++; dest = trozo_de(v->rep); }
    }
    entra(v, dest, t);
}
static void sondeo(u32 t)
{
    u16 e = ENDX; int i;
    for (i = 0; i < NV; i++) {
        voz_t *v = &vz[i]; u16 b = 1 << v->ch;
        if (!v->on || !v->endx_ok || !(e & b)) continue;
        e &= ~b; ENDX = e;                                /* borrar solo este bit (Mednafen carga el registro entero) */
        /* se juzga si ENDX se borra solo en pasos desde un trozo: en los bucles de silencio se vuelve a poner cada
           3,5 ms y la relectura puede caer justo detras */
        if (v->cur >= 0) { if (ENDX & b) { endx_fijo = 1; v->endx_ok = 0; } else endx_borrable = 1; }
        paso_endx(v, t);
    }
    if (irq_v >= 0) {
        if (I_STAT & 0x200) {
            u32 dv = antes(t, irq_t) ? irq_t - t : t - irq_t;
            if (dv > irq_dev_max) { irq_dev_max = dv; dbg_v = irq_v; dbg_k = irq_k; dbg_ant = antes(t, irq_t); dbg_arm = t - irq_t0; dbg_cur = vz[irq_v].cur; }
            irq_ok++; irq_off();
        } else if (!antes(t, irq_t + TARDE_MAX)) { irq_fallo++; irq_off(); }
    }
    for (i = 0; i < NV; i++) {
        voz_t *v = &vz[i];
        if (v->t_kon && !antes(t, v->t_kon) && !fin) { v->t_kon = 0; kon(v, elige(v)); continue; }
        if (!v->on) continue;
        if (VOICE(v->ch, 14) != v->rep) rep_dif++;
        if (v->t_corte && !antes(t, v->t_corte)) { VOICE(v->ch, 0) = 0; VOICE(v->ch, 2) = 0; v->mudo = 1; v->t_corte = 0; paradas++; }
        if (v->guarda && !antes(t, v->t_in + CUADRO)) { v->guarda = 0; if (v->nxt < 0) escribe_rep(v, SIL8); }
        if (v->plan >= 0 && !antes(t, v->t_plan) && (v->nxt < 0 || ROMPE == 1) && !v->guarda) encola(v, t);
        if (v->cur >= 0) {
            if (v->endx_ok) { if (!antes(t, v->t_end + TARDE_MAX)) { perdidos++; entra(v, v->nxt, v->t_end); } }
            else if (!antes(t, v->t_end)) entra(v, v->nxt, v->t_end);      /* ENDX fijo: solo calendario */
        } else if (!v->endx_ok && v->nxt >= 0 && !antes(t, v->t_esc + SILDUR)) entra(v, v->nxt, v->t_esc + SILDUR);
    }
}

/* ------------------------------------------------------------ pantalla */
/* los rotulos se dibujan una vez en cada bufer; por cuadro solo las cifras (borrando su celda), para que el dibujo
   no deje de sondear ENDX mas de ~1 ms (con todo el texto por cuadro eran ~5 ms de retraso en la deteccion) */
#define FONDO 0x201000
static int fb;
static void borra(int x, int y, int s, int n) { rect(x - 1, y - 1, n * 4 * s, 5 * s + 2, FONDO); }
static void numb(int x, int y, int s, u32 v, int n, u32 rgb) { borra(x, y, s, n); num(x, y, s, v, n, rgb); }
static void hexb(int x, int y, int s, u32 v) { borra(x, y, s, 4); hex4(x, y, s, v, BLANCO); }
static void l_marco(int k, int limpia)
{
    l_room(6);
    lp[0] = 0xE3000000 | ((k ? 256u : 0u) << 10); lp[1] = 0xE4000000 | ((((k ? 256u : 0u) + H - 1) << 10)) | (W - 1);
    lp[2] = 0xE5000000 | ((k ? 256u : 0u) << 11); lp += 3;
    if (limpia) { lp[0] = 0x02000000 | FONDO; lp[1] = (k ? 256u : 0u) << 16; lp[2] = (H << 16) | W; lp += 3; }
}
static void rotulos(void)
{
    int i, y;
    txt(CX(0, 3), 6, 3, "SPUCAD T", GRIS); txt(CX(14, 3), 6, 3, "S", GRIS);
    if (ROMPE) { txt(CX(26, 3), 6, 3, "R", ROJO); num(CX(27, 3), 6, 3, ROMPE, 1, ROJO); }
    txt(CX(0, 3), 28, 3, "ESPERADOS", GRIS); txt(CX(17, 3), 28, 3, "VISTOS", GRIS);
    txt(CX(0, 3), 48, 3, "SALTADOS", GRIS); txt(CX(15, 3), 48, 3, "REPETIDOS", GRIS);
    txt(CX(0, 3), 68, 3, "TARDE", GRIS); txt(CX(11, 3), 68, 3, "PERDIDOS", GRIS);
    txt(CX(0, 2), 90, 2, "OTROS", GRIS); txt(CX(11, 2), 90, 2, "IRQ OK", GRIS); txt(CX(24, 2), 90, 2, "FALLO", GRIS); txt(CX(35, 2), 90, 2, "CANC", GRIS);
    txt(CX(0, 2), 104, 2, "ENDX BORRABLE", GRIS); txt(CX(16, 2), 104, 2, "SIL BUCLES", GRIS); txt(CX(34, 2), 104, 2, "DESVIO", GRIS); txt(CX(44, 2), 104, 2, "MS", GRIS);
    txt(CX(0, 2), 118, 2, "ADSR0", GRIS); txt(CX(10, 2), 118, 2, "REPDIF", GRIS); txt(CX(21, 2), 118, 2, "KON", GRIS); txt(CX(30, 2), 118, 2, "IRQ DESV", GRIS); txt(CX(42, 2), 118, 2, "MS", GRIS);
    for (i = 0, y = 134; i < NV; i++, y += 14) {
        txt(CX(0, 2), y, 2, "V", GRIS); num(CX(1, 2), y, 2, canal[i], 1, GRIS);
        txt(CX(3, 2), y, 2, "TROZO", GRIS); txt(CX(12, 2), y, 2, "PASOS", GRIS); txt(CX(24, 2), y, 2, "ADSR", GRIS);
        txt(CX(34, 2), y, 2, "REP", GRIS); txt(CX(43, 2), y, 2, "C", GRIS);
    }
    txt(CX(0, 4), 197, 4, "ENCOLADOS", GRIS);
}
void rc_vblank(void)
{
    int i, y; u32 seg = tk / SEG;
    cuadros++;
    GP1 = 0x05000000 | ((fb ? 256 : 0) << 10); fb ^= 1;
    l_begin(); l_marco(fb, 0);
    numb(CX(9, 3), 6, 3, seg, 4, BLANCO);
    borra(CX(16, 3), 6, 3, 9); txt(CX(16, 3), 6, 3, final ? "FIN" : "CORRIENDO", final ? BLANCO : VERDE);
    numb(CX(10, 3), 28, 3, esperados, 5, BLANCO); numb(CX(24, 3), 28, 3, vistos, 5, BLANCO);
    numb(CX(9, 3), 48, 3, saltados, 4, MAL(saltados)); numb(CX(25, 3), 48, 3, repetidos, 4, MAL(repetidos));
    numb(CX(6, 3), 68, 3, tarde, 4, MAL(tarde)); numb(CX(20, 3), 68, 3, perdidos, 4, MAL(perdidos));
    numb(CX(6, 2), 90, 2, otros, 4, MAL(otros)); numb(CX(18, 2), 90, 2, irq_ok, 5, BLANCO);
    numb(CX(30, 2), 90, 2, irq_fallo, 4, MAL(irq_fallo)); numb(CX(40, 2), 90, 2, irq_canc, 3, BLANCO);
    numb(CX(14, 2), 104, 2, endx_borrable && !endx_fijo, 1, endx_fijo ? ROJO : BLANCO);
    numb(CX(27, 2), 104, 2, sil_bucles % 1000000, 6, BLANCO); numb(CX(41, 2), 104, 2, endx_dev_max * 5 / 21168, 2, BLANCO);
    numb(CX(6, 2), 118, 2, adsr0, 3, MAL(adsr0)); numb(CX(17, 2), 118, 2, rep_dif, 3, MAL(rep_dif));
    numb(CX(25, 2), 118, 2, kons, 4, BLANCO); numb(CX(39, 2), 118, 2, irq_dev_max * 5 / 21168, 2, BLANCO);
    for (i = 0, y = 134; i < NV; i++, y += 14) {
        voz_t *v = &vz[i];
        if (v->cur >= 0) numb(CX(9, 2), y, 2, v->cur, 2, BLANCO); else { borra(CX(9, 2), y, 2, 2); txt(CX(9, 2), y, 2, "--", BLANCO); }
        numb(CX(18, 2), y, 2, v->pasos, 5, BLANCO);
        hexb(CX(29, 2), y, 2, VOICE(v->ch, 12)); hexb(CX(38, 2), y, 2, VOICE(v->ch, 14));
        numb(CX(45, 2), y, 2, v->cola > 9 ? 9 : v->cola, 1, BLANCO);
    }
    if (final) {
        rect(0, 194, W, 26, fallo ? 0x000080 : 0x004000);
        txt(CX(0, 4), 197, 4, fallo ? "RESULTADO: FALLO" : "RESULTADO: OK", fallo ? ROJO : VERDE);
    } else numb(CX(10, 4), 197, 4, encolados, 5, BLANCO);
    txt(CX(22, 2), 224, 2, "IRQMAX V", GRIS); numb(CX(30, 2), 224, 2, dbg_v, 1, BLANCO); txt(CX(32, 2), 224, 2, "K", GRIS); numb(CX(33, 2), 224, 2, dbg_k, 2, BLANCO);
    txt(CX(36, 2), 224, 2, "A", GRIS); numb(CX(37, 2), 224, 2, dbg_ant, 1, BLANCO); numb(CX(39, 2), 224, 2, dbg_arm * 5 / 21168, 4, BLANCO); txt(CX(44, 2), 224, 2, "C", GRIS); numb(CX(45, 2), 224, 2, dbg_cur, 1, BLANCO);
    l_send();
}

int main(void)
{
    int i; u32 t;
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
    for (i = 0; i < 2; i++) { l_begin(); l_marco(i, 1); rotulos(); l_send(); }
    dma_wait(); gpu_wait(); GP1 = 0x05000000; GP1 = 0x03000000;
    spu_init();
    spu_dma_write(0x1000, tz_datos, TZ_BYTES);
    for (i = 0; i < NT; i++) dur[i] = tz_blk[i] * 14979u + tz_blk[i] * 483u / 735u;   /* bloques x 11010048 / 0x2DF */
    for (i = 0; i < NV; i++) {
        voz_t *v = &vz[i];
        v->idx = i; v->ch = canal[i]; v->modo = i == 3 ? 2 : i; v->seed = 0x1234567 * (i + 1); v->cur = v->prev = v->nxt = v->plan = -1;
    }
    t2p = R16(0x1F801120); now();
    for (i = 0; i < NV; i++) vz[i].t_kon = tk + MS(500 + 200 * i);   /* arranque escalonado */
    for (;;) {
        t = now();
        if (!final) sondeo(t);
        if (!fin && t / SEG >= DURACION) { fin = 1; t_fin = t; for (i = 0; i < NV; i++) vz[i].plan = -1, vz[i].cola = 0, vz[i].t_kon = 0; }
        if (fin && !final && !antes(t, t_fin + 7 * SEG)) {   /* en vuelo: el trozo en curso y el encolado (<= 6 s) */
            final = 1;
            for (i = 0; i < NV; i++) { VOICE(vz[i].ch, 0) = 0; VOICE(vz[i].ch, 2) = 0; }
            if (irq_v >= 0) irq_off();
            fallo = saltados || repetidos || tarde || perdidos || otros || irq_fallo || adsr0 || rep_dif
                 || (endx_borrable && !endx_fijo && vistos != esperados) || !irq_ok;
        }
        if (I_STAT & 1) { I_STAT = ~1u; rc_vblank(); }
    }
}

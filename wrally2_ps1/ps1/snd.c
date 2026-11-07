/* Sonido del World Rally 2 en la PS1 (diseno 5.2): cada trozo del GAE1 es un sample PS-ADPCM en la RAM de la SPU
   (tools/sndwr2.py: SPU.BIN con SPUFIJO en 0x1000 y los de la zona variable en 0x5D5E0, tabla work/snd_tab.c por clave banco/final).
   El modelo del GAE1 (rc/gae1.c, con el tiempo del 68000) manda sucesos: empieza el trozo t (KON con su direccion y,
   un cuadro despues, repeticion = silencio), el juego encola el otro trozo (repeticion = ese trozo, pero solo cuando
   la voz ya toca el anterior: calendario por voz con un cuadro de guarda), pasa al otro trozo (la SPU lo hizo sola
   por la direccion de repeticion: solo se apunta) o se calla (volumen 0). Tono = 4096 x 7812,5 x (fps / 59,1) / 44100
   con los fps medidos al arrancar (0x2DF en NTSC). Canales 0, 4, 5 y 6 del GAE1 en las voces 0, 4, 5 y 6.
   Sin interrupciones: la SPU se escribe por KSEG1 y el KON lleva 2 lineas de separacion (snd.c del WR1). */
#include "../rc/rc_rt.h"
#include "../rc/gae1.h"
#include "snd.h"
#include "../../wrally_ps1/ps1/cd.h"
#include "../work/snd_tab.c"
#define R16(a) (*(volatile u16 *)((a) | 0xA0000000))
#define R32(a) (*(volatile u32 *)((a) | 0xA0000000))
#define SPUCNT R16(0x1F801DAA)
#define SPUSTAT R16(0x1F801DAE)
#define SPUADDR R16(0x1F801DA6)
#define VOICE(v, r) R16(0x1F801C00 + (v) * 16 + (r))
#define SIL 0x1000                              /* bloque de silencio en bucle */
#define VAR0 0x5D5E0
extern int fps_mil;
unsigned snd_kon, snd_encolados, snd_sin_trozo, snd_tarde, snd_cargas, snd_carga_lin;
int snd_pide, snd_modo_tit, snd_juego;
static u16 tono = 0x2DF, t_key;
/* ficheros de la SPU (el numero es snd_val >> 24, tools/sndwr2.py) y lo que ocupan en la RAM de la SPU */
#define NF 11
/* 0 SPUFIJO, 1 SPUTIT, 2-5 SPUDEMO0-3, 6 SPUMENU (S7C), 7 SPUS7E, 8-10 SPUCAR0-2: seguidos en SPU.BIN (snd_fsec) */
static int lba_spu = -1;
static u32 lo[NF], hi[NF];
static u16 cargado;                             /* bit f: el fichero f esta en la SPU */
static int ult_coche, no_pide;
static u32 ult_carga = 0xFFFF0000u;
/* por canal del GAE1: direccion que toca, repeticion pendiente, y trozos que no estaban (registro + 1) */
static struct { u32 dir, pend; u16 vol; u8 act, cuadro_kon, falta, falta_enc; } voz[8];
static u32 cuadro;
static int pausa_n;                             /* pausas anidadas abiertas (snd_pausa) */

static void lineas(int n) { u16 a = t1_get(); while ((u16)(t1_get() - a) < (u16)n); }
static void spu_wait_mode(u16 m) { int n; for (n = 0; n < 100000 && (SPUSTAT & 0x3F) != (m & 0x3F); n++); }
/* trozo del canal ch, registro t (0/1): direccion en la SPU o 0 si no esta cargado; entonces pide su fichero (si
   esta en dos: el del titulo en las pantallas del titulo, si no el ultimo coche) */
static u32 trozo_dir(int ch, int t)
{
    const u16 *r = gae1_reg + ch * 8 + t * 4;
    u32 k = ((u32)(r[1] & 3) << 16) | (u16)(r[2] - 0x1000), a = 0, b = SND_N, j; int f, el = -1;   /* el registro lleva el final + 0x1000 */
    while (a < b) { u32 m = (a + b) >> 1; if (snd_key[m] < k) a = m + 1; else b = m; }
    if (a >= SND_N || snd_key[a] != k) { snd_sin_trozo++; return 0; }
    for (j = a; j < SND_N && snd_key[j] == k; j++) {
        f = (int)(snd_val[j] >> 24);
        if (cargado & (1u << f)) return snd_val[j] & 0xFFFFF;
        if (el < 0 || (f == 1 && snd_modo_tit) || (f == ult_coche && !(el == 1 && snd_modo_tit))) el = f;
    }
    if (!no_pide && !snd_pide) snd_pide = el;
    return 0;
}
static u16 volumen(int ch, int t)
{
    u16 r = gae1_reg[ch * 8 + t * 4 + 1], a = (r >> 12) & 15, b = (r >> 8) & 15;
    return (u16)(0x3FFF * (a > b ? a : b) / 15);
}
static void kon(int ch, u32 dir, u16 vol)
{
    VOICE(ch, 0) = vol; VOICE(ch, 2) = vol; VOICE(ch, 4) = pausa_n ? 0 : tono; VOICE(ch, 6) = (u16)(dir >> 3);
    VOICE(ch, 8) = 0x000F; VOICE(ch, 10) = 0x0006;   /* ADSR: ataque inmediato, sostenido maximo, sin caida; al KOFF,
                                                        bajada lineal de ~3 ms (desplazamiento 6: -256 por muestra) */
    while ((u16)(t1_get() - t_key) < 2);
    R16(0x1F801D88) = 1u << ch; t_key = t1_get(); snd_kon++;
    voz[ch].dir = dir; voz[ch].pend = SIL; voz[ch].vol = vol; voz[ch].act = 1; voz[ch].cuadro_kon = (u8)cuadro;
}
void snd_suceso(int ch, int t)
{
    if (ch > 6) return;
    if (t < 0) {                                         /* se calla: KOFF (la envolvente baja en ~3 ms) en vez del volumen
                                                            a 0 de golpe, que a media onda sonaba como un golpe */
        while ((u16)(t1_get() - t_key) < 2);
        R16(0x1F801D8C) = 1u << ch; t_key = t1_get();
        VOICE(ch, 14) = SIL >> 3; voz[ch].act = 0; voz[ch].pend = 0; voz[ch].falta = voz[ch].falta_enc = 0; return;
    }
    if (t >= 2) {                                        /* encolado: repeticion = trozo nuevo (cuando toque) */
        u32 d = trozo_dir(ch, t - 2);
        voz[ch].pend = d ? d : SIL; voz[ch].falta_enc = d ? 0 : (u8)(t - 1); snd_encolados++; return;
    }
    if (!voz[ch].act) {                                  /* empieza */
        u32 d = trozo_dir(ch, t);
        if (!d) { voz[ch].act = 1; voz[ch].pend = 0; voz[ch].dir = 0; voz[ch].falta = (u8)(t + 1); return; }   /* sin cargar: silencio */
        kon(ch, d, volumen(ch, t)); voz[ch].falta = 0;
    } else {                                             /* el modelo paso al otro trozo: la SPU ya lo hizo sola */
        voz[ch].dir = voz[ch].pend; voz[ch].pend = SIL; voz[ch].cuadro_kon = (u8)(cuadro + 1);   /* la SPU puede ir un cuadro
                                                       detras: la repeticion nueva, un cuadro entero despues */
        voz[ch].falta = voz[ch].falta_enc; voz[ch].falta_enc = 0;
    }
}
void snd_vblank(void)
{
    int ch;
    cuadro++;
    for (ch = 0; ch < 7; ch++) if (voz[ch].act && voz[ch].pend && (s8)(cuadro - voz[ch].cuadro_kon) >= 1) {
        VOICE(ch, 14) = (u16)(voz[ch].pend >> 3);        /* la voz ya toca el trozo anterior: repeticion al dia */
        if (voz[ch].pend == SIL) voz[ch].pend = 0;
    }
}
/* anidada (una carga bloqueante puede llamar a otra); las lineas en pausa se apuntan en snd_lin_pausa (main.c las
   descuenta del tiempo real del modelo del GAE1) */
u32 snd_lin_pausa; static u16 pausa_t;
void snd_pausa(int on)
{
    int ch;
    if (on ? pausa_n++ : --pausa_n) { if (pausa_n < 0) pausa_n = 0; return; }
    for (ch = 0; ch < 7; ch++) VOICE(ch, 4) = on ? 0 : tono;
    if (on) pausa_t = t1_get(); else snd_lin_pausa += (u16)(t1_get() - pausa_t);
}
static void spu_dma_write(u32 addr, const void *src, int bytes)
{
    int n;
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
    SPUADDR = (u16)(addr >> 3);
    SPUCNT = (SPUCNT & ~0x30) | 0x20; spu_wait_mode(SPUCNT);
    R32(0x1F8010C0) = (u32)src;
    R32(0x1F8010C4) = ((bytes / 64) << 16) | 16;
    R32(0x1F8010C8) = 0x01000201;
    for (n = 0; n < 1000000 && (R32(0x1F8010C8) & 0x01000000); n++);
    for (n = 0; n < 100000 && (SPUSTAT & 0x400); n++);
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
}
/* ---- carga comprobada (hardware real con CD grabado: en MiSTer y con PSIO/XStation todo llega bien). Cada sector de
   SPU.BIN se suma (snd_ck, tools/sndwr2.py) antes de ir a la SPU: el que llega mal no se escribe y al acabar se lee
   otra vez solo (a 1x desde el segundo intento: los CD-R se leen peor a 2x). Lo escrito por DMA se lee de vuelta de
   la RAM de la SPU por trozos de 256 bytes; si no es igual se escribe otra vez, y al tercer fallo a mano por el puerto
   de datos (1F801DA8). Cifras: snd_ck_mal (sectores que llegaron mal), snd_ck_perdidos (que no llegaron bien en 4
   intentos), snd_spu_mal (escrituras a la SPU que no salieron) */
unsigned snd_ck_mal, snd_ck_perdidos, snd_spu_mal;
static u32 suma(const unsigned char *d)
{
    const u32 *w = (const u32 *)d; u32 c = 0; int k;
    for (k = 0; k < 512; k++) c = ((c << 1) | (c >> 31)) + w[k];
    return c;
}
static void spu_dma_read(u32 addr, void *dst, int bytes)
{
    int n;
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
    SPUADDR = (u16)(addr >> 3);
    SPUCNT = (SPUCNT & ~0x30) | 0x30; spu_wait_mode(SPUCNT);
    R32(0x1F8010C0) = (u32)dst;
    R32(0x1F8010C4) = ((bytes / 64) << 16) | 16;
    R32(0x1F8010C8) = 0x01000200;
    for (n = 0; n < 1000000 && (R32(0x1F8010C8) & 0x01000000); n++);
    for (n = 0; n < 100000 && (SPUSTAT & 0x400); n++);
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
}
static void spu_mano_write(u32 addr, const void *src, int bytes)
{   /* escritura manual: 32 medias palabras al FIFO y modo 1 para soltarlas */
    const u16 *h = (const u16 *)src; int i, k, n;
    SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
    SPUADDR = (u16)(addr >> 3);
    for (i = 0; i < bytes / 2; i += 32) {
        for (k = 0; k < 32 && i + k < bytes / 2; k++) R16(0x1F801DA8) = h[i + k];
        SPUCNT = (SPUCNT & ~0x30) | 0x10; spu_wait_mode(SPUCNT);
        for (n = 0; n < 100000 && (SPUSTAT & 0x400); n++);
        SPUCNT = SPUCNT & ~0x30; spu_wait_mode(SPUCNT);
    }
}
static int spu_igual(u32 addr, const unsigned char *src, int bytes)
{
    static u32 t[64]; int o, k;           /* fuera de la pila: la pila esta en el scratchpad y la DMA no llega alli */
    for (o = 0; o < bytes; o += 256) {
        int b = bytes - o < 256 ? bytes - o : 256;
        spu_dma_read(addr + o, t, b);
        for (k = 0; k < b / 4; k++) if (t[k] != ((const u32 *)(src + o))[k]) return 0;
    }
    return 1;
}
static void spu_write_ver(u32 addr, const unsigned char *src, int bytes)
{
    int k;
    for (k = 0; k < 4; k++) {
        if (k < 3) spu_dma_write(addr, src, bytes); else spu_mano_write(addr, src, bytes);
        if (spu_igual(addr, src, bytes)) return;
    }
    snd_spu_mal++;
}
static u32 carga_dir, carga_n, carga_s0, mal[(185 + 31) / 32];
static void sector_spu(int i, const unsigned char *d)
{   /* el ultimo sector solo hasta el final del fichero (redondeado a 64: bloques del DMA) */
    u32 r = carga_n - (u32)i * 2048; if (r > 2048) r = 2048;
    if (suma(d) != snd_ck[carga_s0 + i]) { mal[i >> 5] |= 1u << (i & 31); snd_ck_mal++; return; }
    mal[i >> 5] &= ~(1u << (i & 31));
    spu_write_ver(carga_dir + i * 2048, d, (int)((r + 63) & ~63u));
}
static int ret_i;
static void sector_ret(int i, const unsigned char *d) { (void)i; sector_spu(ret_i, d); }
static int carga_fich(int f, unsigned addr)
{   /* el fichero f de SPU.BIN a la RAM de la SPU, bloqueando; luego los sectores que llegaron mal, uno a uno */
    int sz, n, i, k, r;
    if (lba_spu < 0 && cd_find("SPU.BIN", &lba_spu, &sz)) { lba_spu = -1; return 0; }
    carga_dir = addr; carga_n = snd_flen[f]; carga_s0 = snd_fsec[f]; n = (int)(snd_flen[f] + 2047) / 2048;
    for (i = 0; i < (int)(sizeof mal / sizeof mal[0]); i++) mal[i] = 0;
    for (i = 0; i < n; i++) mal[i >> 5] |= 1u << (i & 31);   /* lo que no llegue (lectura cortada) tambien */
    cd_stream(lba_spu + (int)carga_s0, n, sector_spu, 1);
    for (i = 0; i < n; i++)
        for (k = 0; k < 4 && (mal[i >> 5] & (1u << (i & 31))); k++) { ret_i = i; cd_stream(lba_spu + (int)carga_s0 + i, 1, sector_ret, k == 0); }
    for (r = 0, i = 0; i < n; i++) if (mal[i >> 5] & (1u << (i & 31))) { snd_ck_perdidos++; r = -1; }
    return !r;
}
/* el fichero pedido por trozo_dir, bloqueando (el juego congelado; las voces a tono 0 lo pone quien llama). Como mucho
   una carga asi cada 60 cuadros (dos ficheros que se pisan pedidos a la vez no se turnan sin fin); snd_precarga (main.c,
   en los negros) no tiene ese limite. carga_f quita los ficheros que pisa y vuelve a empezar las voces que no tenian su
   trozo o tocaban donde se escribe */
static void carga_f(int f);
void snd_atiende(void)
{
    int f = snd_pide;
    snd_pide = 0;
    if (f <= 0 || f >= NF || (cargado & (1u << f))) return;
    if (cuadro - ult_carga < 60) { snd_tarde++; return; }
    carga_f(f);
}
void snd_precarga(int f) { if (f > 0 && f < NF && !(cargado & (1u << f))) carga_f(f); }
static void carga_f(int f)
{
    int g, ch; u16 t0;
    for (g = 1; g < NF; g++) if (g != f && lo[g] < hi[f] && lo[f] < hi[g]) cargado &= (u16)~(1u << g);
    for (ch = 0; ch < 7; ch++) if (voz[ch].act && voz[ch].dir >= lo[f] && voz[ch].dir < hi[f]) { VOICE(ch, 0) = 0; VOICE(ch, 2) = 0; }
    t0 = t1_get();
    if (!carga_fich(f, lo[f])) return;
    snd_carga_lin += (u16)(t1_get() - t0); snd_cargas++;
    cargado |= (u16)(1u << f); ult_carga = cuadro;
    if (f >= 8) ult_coche = f;
    no_pide = 1;
    for (ch = 0; ch < 7; ch++) {
        u32 d; int t;
        if (!voz[ch].act || !(voz[ch].falta || (voz[ch].dir >= lo[f] && voz[ch].dir < hi[f]))) continue;
        t = voz[ch].falta ? voz[ch].falta - 1 : gae1_c[ch].trozo;
        d = trozo_dir(ch, t); voz[ch].falta = 0;
        if (d) kon(ch, d, volumen(ch, t)); else { voz[ch].dir = 0; VOICE(ch, 0) = 0; VOICE(ch, 2) = 0; }
        if (voz[ch].falta_enc && (d = trozo_dir(ch, voz[ch].falta_enc - 1)) != 0) { voz[ch].pend = d; voz[ch].falta_enc = 0; }
    }
    no_pide = 0;
}
void snd_init(void)
{
    int v;
    R32(0x1F8010F0) |= 0x80000;                          /* DMA 4 (SPU) */
    SPUCNT = 0; spu_wait_mode(0);
    R16(0x1F801D80) = 0x3FFF; R16(0x1F801D82) = 0x3FFF;  /* volumen general */
    R16(0x1F801D84) = 0; R16(0x1F801D86) = 0;            /* reverberacion */
    R16(0x1F801D8C) = 0xFFFF; R16(0x1F801D8E) = 0x00FF;  /* apagar voces */
    R16(0x1F801D90) = 0; R16(0x1F801D92) = 0; R16(0x1F801D94) = 0; R16(0x1F801D96) = 0;
    R16(0x1F801D98) = 0; R16(0x1F801D9A) = 0;
    R16(0x1F801DB0) = 0; R16(0x1F801DB2) = 0;            /* sin audio del CD ni entrada externa */
    R16(0x1F801DB4) = 0; R16(0x1F801DB6) = 0;
    R16(0x1F801DAC) = 0x0004;
    R16(0x1F801DA4) = 0x7FFF0 >> 3;
    R16(0x1F801DA2) = 0xFFFE;                            /* area de la reverberacion (apagada) lejos de los trozos */
    SPUCNT = 0xC000;                                     /* SPU, sin silencio, sin IRQ */
    for (v = 0; v < 24; v++) { VOICE(v, 0) = 0; VOICE(v, 2) = 0; VOICE(v, 14) = SIL >> 3; }
    lineas(4);
    tono = snd_juego ? (u16)((u32)fps_mil * 12278u / 1000000u) : 0x2D6;   /* juego: 725,62 x fps / 59,1; si no, 7 812,5 Hz */
    t_key = t1_get();
    for (v = 0; v < NF; v++) { lo[v] = 0xFFFFF; hi[v] = 0; }
    for (v = 0; v < SND_N; v++) {
        u32 f = snd_val[v] >> 24, a = snd_val[v] & 0xFFFFF, e = a + snd_nblk[v] * 16u;
        if (a < lo[f]) lo[f] = a;
        if (e > hi[f]) hi[f] = e;
    }
    lo[0] = SIL;                                         /* SPUFIJO lleva el silencio delante */
    cargado = carga_fich(0, SIL) ? 1 : 0;
    if (carga_fich(1, lo[1])) cargado |= 2;         /* el titulo: lo primero que suena */
}

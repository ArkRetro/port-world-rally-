/* Sonido del World Rally 2 en la PS1 (diseno 5.2): cada trozo del GAE1 es un sample PS-ADPCM en la RAM de la SPU
   (tools/sndwr2.py: SPUFIJO.BIN en 0x1000, la zona variable en 0x5D5E0, tabla work/snd_tab.c por clave banco/final).
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
unsigned snd_kon, snd_encolados, snd_sin_trozo, snd_tarde;
static u16 tono = 0x2DF, t_key;
static int zona;                                /* fichero de la zona variable cargado (snd_val >> 24; 0 = fijo) */
static struct { u32 dir, pend; u16 vol; u8 act, cuadro_kon; } voz[8];   /* por canal del GAE1 */
static u32 cuadro;

static void lineas(int n) { u16 a = t1_get(); while ((u16)(t1_get() - a) < (u16)n); }
static void spu_wait_mode(u16 m) { int n; for (n = 0; n < 100000 && (SPUSTAT & 0x3F) != (m & 0x3F); n++); }
/* trozo del canal ch, registro t (0/1): direccion en la SPU o 0 si no esta cargado (zona variable de otra fase) */
static u32 trozo_dir(int ch, int t)
{
    const u16 *r = gae1_reg + ch * 8 + t * 4;
    u32 k = ((u32)(r[1] & 3) << 16) | r[2], a = 0, b = SND_N;
    while (a < b) { u32 m = (a + b) >> 1; if (snd_key[m] < k) a = m + 1; else b = m; }
    if (a >= SND_N || snd_key[a] != k) { snd_sin_trozo++; return 0; }
    if ((snd_val[a] >> 24) && (int)(snd_val[a] >> 24) != zona) { snd_sin_trozo++; return 0; }
    return snd_val[a] & 0xFFFFF;
}
static u16 volumen(int ch, int t)
{
    u16 r = gae1_reg[ch * 8 + t * 4 + 1], a = (r >> 12) & 15, b = (r >> 8) & 15;
    return (u16)(0x3FFF * (a > b ? a : b) / 15);
}
static void kon(int ch, u32 dir, u16 vol)
{
    VOICE(ch, 0) = vol; VOICE(ch, 2) = vol; VOICE(ch, 4) = tono; VOICE(ch, 6) = (u16)(dir >> 3);
    VOICE(ch, 8) = 0x000F; VOICE(ch, 10) = 0x0000;   /* ADSR: ataque inmediato, sostenido maximo, sin caida */
    while ((u16)(t1_get() - t_key) < 2);
    R16(0x1F801D88) = 1u << ch; t_key = t1_get(); snd_kon++;
    voz[ch].dir = dir; voz[ch].pend = SIL; voz[ch].vol = vol; voz[ch].act = 1; voz[ch].cuadro_kon = (u8)cuadro;
}
void snd_suceso(int ch, int t)
{
    if (ch > 6) return;
    if (t < 0) { VOICE(ch, 0) = 0; VOICE(ch, 2) = 0; voz[ch].act = 0; voz[ch].pend = 0; return; }   /* callar */
    if (t >= 2) {                                        /* encolado: repeticion = trozo nuevo (cuando toque) */
        u32 d = trozo_dir(ch, t - 2);
        voz[ch].pend = d ? d : SIL; snd_encolados++; return;
    }
    if (!voz[ch].act) {                                  /* empieza */
        u32 d = trozo_dir(ch, t);
        if (!d) { voz[ch].act = 1; voz[ch].pend = 0; return; }   /* trozo sin cargar: suena como silencio */
        kon(ch, d, volumen(ch, t));
    } else {                                             /* el modelo paso al otro trozo: la SPU ya lo hizo sola */
        voz[ch].dir = voz[ch].pend; voz[ch].pend = SIL; voz[ch].cuadro_kon = (u8)cuadro;
    }
}
void snd_vblank(void)
{
    int ch;
    cuadro++;
    for (ch = 0; ch < 7; ch++) if (voz[ch].act && voz[ch].pend && (u8)(cuadro - voz[ch].cuadro_kon) >= 1) {
        VOICE(ch, 14) = (u16)(voz[ch].pend >> 3);        /* la voz ya toca el trozo anterior: repeticion al dia */
        if (voz[ch].pend == SIL) voz[ch].pend = 0;
    }
}
void snd_pausa(int on)
{
    int ch;
    for (ch = 0; ch < 7; ch++) VOICE(ch, 4) = on ? 0 : tono;
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
static u32 carga_dir;
static void sector_spu(int i, const unsigned char *d) { spu_dma_write(carga_dir + i * 2048, d, 2048); }
int snd_cargar(const char *nombre, unsigned addr)
{
    int lba, n;
    if (!cd_find(nombre, &lba, &n)) return 0;
    carga_dir = addr;
    cd_stream(lba, (n + 2047) / 2048, sector_spu, 1);
    return 1;
}
void snd_zona(int fichero) { zona = fichero; }
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
    tono = (u16)((u32)fps_mil * 12278u / 1000000u);      /* 725,62 x fps / 59,1 */
    t_key = t1_get();
}

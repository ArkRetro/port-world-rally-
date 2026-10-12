/* Sonido: el OKI M6295 de la recreativa sobre la SPU (frases cortas y efectos, en la RAM de la SPU) y el audio
   del CD (frases largas, pistas CD-DA). El canal de una frase de la SPU queda ocupado hasta que su voz llega al
   ultimo bloque (ENDX), como el OKI, que avisa al acabar la muestra; el de una frase de CD hasta que la pista acaba
   de verdad (pausa automatica, INT4), o como mucho su duracion mas 2 s contando desde que empieza a sonar. Para
   leer del CD la frase se corta (apuntando por donde iba) y luego se sigue desde ahi. Uso personal. */
#include "cd.h"
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#define SPUCNT R16(0x1F801DAA)
#define SPUSTAT R16(0x1F801DAE)
#define SPUADDR R16(0x1F801DA6)
#define VOICE(v, r) R16(0x1F801C00 + (v) * 16 + (r))
#define T1 R16(0x1F801110)                  /* temporizador 1: lineas de video */
extern const u16 snd_addr[128];
extern const u8 snd_track[128];
extern const u16 oki_len[16][128];
void rc_oki_hold(int c, int on), rc_oki_end(int c);
static const u8 okivol[16] = {32, 22, 16, 11, 8, 6, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1};
static int trk_lba[32], trk_end[32];        /* pistas: principio exacto y final (TRACKS.BIN) */
/* frase larga: canal, pista, desde donde suena, 1/60 s sonando y lo que dura desde ahi, 1/60 s esperando a que
   suene, posicion si esta cortada */
static int cd_ch = -1, cd_trk, cd_from, cd_t, cd_len, cd_wait, cd_susp = -1, cut_st, snd_ok;
/* canales que suenan en la SPU: 1/60 s desde que empezaron y el maximo (por si la voz no avisara) */
static int spu_on, spu_t[4], spu_max[4];
/* teclas: la SPU mira KON y KOFF una vez por muestra (44,1 kHz) y escribir uno de los dos sustituye los bits que
   aun no ha visto (MiSTer, DuckStation): entre dos escrituras al mismo registro, al menos 2 lineas (~128 us) */
static u16 t_key[2];
static void spu_key(int off, int m)
{
    while ((u16)(T1 - t_key[off]) < 2);
    R16(0x1F801D88 + off * 4) = m; t_key[off] = T1;
}
static int snd_fr, cut_f;                   /* cuadros (snd_frame) y en el que se pidio la posicion para cortar */

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
}
void snd_init(void)
{
    int v;
    R32(0x1F8010F0) |= 0x80000;                 /* DMA 4 (SPU) */
    SPUCNT = 0; spu_wait_mode(0);
    R16(0x1F801D80) = 0x3FFF; R16(0x1F801D82) = 0x3FFF;   /* volumen general */
    R16(0x1F801D84) = 0; R16(0x1F801D86) = 0;             /* reverberacion */
    R16(0x1F801D8C) = 0xFFFF; R16(0x1F801D8E) = 0x00FF;   /* apagar voces */
    R16(0x1F801D90) = 0; R16(0x1F801D92) = 0; R16(0x1F801D94) = 0; R16(0x1F801D96) = 0;
    R16(0x1F801D98) = 0; R16(0x1F801D9A) = 0;
    R16(0x1F801DB0) = 0x7FFF; R16(0x1F801DB2) = 0x7FFF;   /* audio del CD */
    R16(0x1F801DAC) = 0x0004;
    SPUCNT = 0xC001;                                       /* SPU, sin silencio, audio del CD */
    for (v = 0; v < 24; v++) { VOICE(v, 0) = 0; VOICE(v, 2) = 0; }
    cd_volume(0x80);
    snd_ok = 1;
}
/* sector i de SNDFX.BIN (efectos en PS-ADPCM) a la RAM de la SPU */
void snd_fx_sector(int i, const unsigned char *d) { spu_dma_write(0x1010 + i * 2048, d, 2048); }
/* tabla de pistas (TRACKS.BIN: principio y final de cada una, en sectores) */
void snd_tracks(const unsigned char *d)
{
    const u32 *p = (const u32 *)d; int t;
    for (t = 0; t < 32; t++) { trk_lba[t] = p[t * 2]; trk_end[t] = p[t * 2 + 1]; }
}
void snd_play(int ch, int p, int bank, int att)
{
    if (!snd_ok) return;
    if (snd_track[p]) {
        int t = snd_track[p], vol = okivol[att] * 0x80 / 16;
        if (t >= 32 || trk_lba[t] <= 0 || trk_end[t] <= trk_lba[t]) return;
        if (cd_ch >= 0 && cd_ch != ch) rc_oki_hold(cd_ch, 0);   /* la anterior: ocupada solo por su duracion */
        cd_volume(vol > 0x80 ? 0x80 : vol);
        cd_play(trk_lba[t]);
        cd_ch = ch; cd_trk = t; cd_from = trk_lba[t]; cd_t = cd_wait = 0; cd_susp = -1; cut_st = 0;
        cd_len = (trk_end[t] - trk_lba[t]) * 60 / 75;
        rc_oki_hold(ch, 1);
    } else if (snd_addr[p]) {
        u32 vol = okivol[att] * 0x3FFF / 16;
        if (vol > 0x3FFF) vol = 0x3FFF;
        /* sin KOFF: KON vuelve a empezar la voz aunque este sonando (y un KOFF justo antes podria ganarle) */
        VOICE(ch, 0) = vol; VOICE(ch, 2) = vol;
        VOICE(ch, 4) = 704;                         /* 7576 Hz */
        VOICE(ch, 6) = snd_addr[p];
        VOICE(ch, 8) = 0x00FF; VOICE(ch, 10) = 0x0000;
        spu_key(0, 1 << ch);
        spu_on |= 1 << ch; spu_t[ch] = 0; spu_max[ch] = oki_len[bank & 15][p] + 30;
        rc_oki_hold(ch, 1);
    }
}
void snd_stop(int mask)
{
    if (!snd_ok) return;
    if (mask & 15) spu_key(1, mask & 15);
    spu_on &= ~mask;
    /* cortada ya no suena: sin Pause, que pararia la lectura en curso */
    if (cd_ch >= 0 && (mask & (1 << cd_ch))) { if (cd_susp < 0) cd_stop(); cd_ch = -1; cd_susp = -1; cut_st = 0; }
}
/* voces que llegaron al ultimo bloque (ENDX): canal libre. Al empezar cada cuadro, con los 1/60 s pasados, y otra
   vez justo antes de volver al juego (n = 0), que mira los canales nada mas empezar el cuadro */
void snd_spu(int n)
{
    int e, c;
    if (!spu_on) return;
    e = R16(0x1F801D9C);
    for (c = 0; c < 4; c++)
        if ((spu_on & (1 << c)) && ((e & (1 << c)) || (spu_t[c] += n) > spu_max[c])) { spu_on &= ~(1 << c); rc_oki_end(c); }
}
static void cd_done(void) { cd_ended = 0; rc_oki_hold(cd_ch, 0); cd_ch = -1; cd_susp = -1; cut_st = 0; }
/* cada cuadro, con los 1/60 s pasados: fin de las frases de la SPU y de la larga (el tiempo de esta cuenta mientras
   suena: la busqueda, la pausa o el cambio de velocidad de antes no; si no llega a sonar en 10 s se da por acabada) */
void snd_frame(int n)
{
    snd_spu(n);
    snd_fr++;
    if (cd_ch < 0 || cd_susp >= 0) return;
    if (cd_playing()) cd_t += n; else cd_wait += n;
    if ((cd_ended && !cd_audio()) || cd_t > cd_len + 120 || cd_wait > 600) {
        if (!cd_ended) cd_stop();               /* sin pausa automatica: pararla a mano */
        cd_done();
    }
}
/* la frase ya ha acabado (o casi): no vale la pena cortarla ni seguirla */
static int cd_over(int margin) { return cd_ended || cd_t + margin >= cd_len; }
/* posicion para seguir la frase: la de GetlocP si es de la pista, si no por tiempo */
static int cd_pos(int pos)
{
    if (pos < cd_from || pos >= trk_end[cd_trk]) {
        pos = cd_from + (cd_t > 20 ? cd_t - 20 : 0) * 75 / 60;
        if (pos >= trk_end[cd_trk]) pos = trk_end[cd_trk] - 1;
    }
    return pos;
}
/* antes de una carga: parar la frase larga y apuntar por donde iba; despues, seguirla desde ahi */
void snd_suspend(void)
{
    int pos;
    if (cd_ch < 0 || cd_susp >= 0) { cut_st = 0; return; }
    if (cd_over(0)) { cd_stop(); cd_sync(); cd_done(); return; }
    pos = cd_pos(cd_where());
    cd_stop(); cd_sync();
    cd_ended = 0; cd_susp = pos; cut_st = 0;
}
/* lo mismo sin esperar (para leer tiles mientras suena): devuelve 1 cuando el audio ya esta cortado o se esta
   parando (la lectura que se pida espera a la pausa), 0 si aun hay que esperar a la posicion. No corta si a la
   frase le queda poco (devuelve 0). La posicion pedida vale para el cuadro siguiente: si no se vuelve a llamar
   enseguida, se pide otra (la vieja haria repetir un trozo) */
int snd_cut(void)
{
    int pos = -1;
    if (cd_ch < 0 || cd_susp >= 0) return !cd_audio() || cd_susp >= 0;
    if (cut_st && snd_fr - cut_f > 2) cut_st = 0;
    if (cd_over(15)) return 0;
    if (cd_playing() || cut_st) {               /* si aun no suena se seguira desde donde iba a empezar */
        if (!cut_st) { cd_loc_start(); cut_st = 1; cut_f = snd_fr; return 0; }
        if ((pos = cd_loc_get()) == -2) return 0;
    }
    cd_susp = cd_pos(pos);
    cd_stop();
    cd_ended = 0; cut_st = 0;
    return 1;
}
int snd_cut_on(void) { return cd_ch >= 0 && cd_susp >= 0; }
/* pausa: todo en silencio (las voces de la SPU siguen hasta su final, el juego las vera libres al volver) y la frase
   larga parada y apuntada; al volver, se sigue desde ahi si la paro la pausa (si estaba cortada para leer, la sigue
   cache_load como siempre) */
static int pause_cd;
void snd_resume(void);
void snd_pause(int on)
{
    if (!snd_ok) return;
    if (on) {
        R16(0x1F801D80) = 0; R16(0x1F801D82) = 0;
        pause_cd = cd_ch >= 0 && cd_susp < 0;
        if (pause_cd) snd_suspend();
    } else {
        if (pause_cd && cd_ch >= 0 && cd_susp >= 0) snd_resume();
        pause_cd = 0;
        R16(0x1F801D80) = 0x3FFF; R16(0x1F801D82) = 0x3FFF;
    }
}
void snd_resume(void)
{
    if (cd_ch < 0 || cd_susp < 0) return;
    cd_play(cd_susp);
    cd_from = cd_susp; cd_t = cd_wait = 0; cd_len = (trk_end[cd_trk] - cd_susp) * 60 / 75;
    cd_susp = -1;
}

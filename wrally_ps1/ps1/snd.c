/* Sonido: el OKI M6295 de la recreativa sobre la SPU (frases cortas, en la RAM de la SPU) y el audio del CD
   (frases largas, pistas CD-DA). Uso personal. */
#include "cd.h"
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#define SPUCNT R16(0x1F801DAA)
#define SPUSTAT R16(0x1F801DAE)
#define SPUADDR R16(0x1F801DA6)
#define VOICE(v, r) R16(0x1F801C00 + (v) * 16 + (r))
extern const u16 snd_addr[128];
extern const u8 snd_track[128];
extern const unsigned short oki_len[16][128];
static const u8 okivol[16] = {32, 22, 16, 11, 8, 6, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1};
static int trk_lba[32], cd_ch = -1, cd_left, cd_trk, cd_elapsed, snd_ok;
int snd_cd_busy;            /* el CD esta sonando (las lecturas de datos lo paran) */

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
void snd_init(void *buf, int bufsize, int lba, int size)
{
    int v, off;
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
    /* efectos del CD a la RAM de la SPU */
    if (lba >= 0)
        for (off = 0; off < size; off += bufsize) {
            int n = size - off < bufsize ? size - off : bufsize;
            if (cd_read(lba + off / 2048, (n + 2047) / 2048, buf)) break;
            spu_dma_write(0x1010 + off, buf, (n + 63) & ~63);
        }
    for (v = 2; v < 32; v++) trk_lba[v] = cd_track_lba(v);
    cd_volume(0x80);
    snd_ok = 1;
}
void snd_play(int ch, int p, int bank, int att)
{
    (void)bank;
    if (!snd_ok) return;
    if (snd_track[p]) {
        int t = snd_track[p], vol = okivol[att] * 0x80 / 16;
        if (trk_lba[t] < 0) return;
        cd_volume(vol > 0x80 ? 0x80 : vol);
        cd_play_at(trk_lba[t]);
        cd_ch = ch; cd_trk = t; cd_left = oki_len[0][p]; cd_elapsed = 0; snd_cd_busy = 1;
    } else if (snd_addr[p]) {
        u32 vol = okivol[att] * 0x3FFF / 16;
        if (vol > 0x3FFF) vol = 0x3FFF;
        R16(0x1F801D8C) = 1 << ch;
        VOICE(ch, 0) = vol; VOICE(ch, 2) = vol;
        VOICE(ch, 4) = 704;                         /* 7576 Hz */
        VOICE(ch, 6) = snd_addr[p];
        VOICE(ch, 8) = 0x00FF; VOICE(ch, 10) = 0x0000;
        R16(0x1F801D88) = 1 << ch;
    }
}
void snd_stop(int mask)
{
    if (!snd_ok) return;
    R16(0x1F801D8C) = mask & 15;
    if (cd_ch >= 0 && (mask & (1 << cd_ch))) { cd_stop_audio(); cd_ch = -1; snd_cd_busy = 0; }
}
/* una vez por cuadro: fin de la frase larga */
void snd_frame(void)
{
    if (cd_ch >= 0) {
        cd_elapsed++;
        if (--cd_left <= 0) { cd_stop_audio(); cd_ch = -1; snd_cd_busy = 0; }
    }
}
/* tras leer datos del CD: seguir la frase larga donde iba */
void snd_resume(void)
{
    if (cd_ch >= 0 && trk_lba[cd_trk] >= 0) cd_play_at(trk_lba[cd_trk] + cd_elapsed * 75 / 60);
}

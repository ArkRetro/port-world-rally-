/* Mapa de memoria del World Rally para el codigo recompilado (comun al PC y a la PS1) */
#include "rc_rt.h"
u8 *rc_rpg[4096], *rc_wpg[4096];
#ifndef RC_ROM_EXTERN
u8 rc_rom[0x100000 + 4] __attribute__((aligned(4)));
#endif
u8 rc_ram[0x4000 + 4] __attribute__((aligned(4))), rc_pal[0x4000 + 4] __attribute__((aligned(4)));
u8 rc_spr[0x1000 + 4] __attribute__((aligned(4))), rc_vram[0x4000 + 4] __attribute__((aligned(4)));
u8 rc_vregs[0x1000 + 4] __attribute__((aligned(4)));
u8 rc_iopg[0x1000 + 4] __attribute__((aligned(4))), rc_ffpg[0x1000 + 4] __attribute__((aligned(4)));
rc_regs_t rc_r;
u16 gdecrypt(u16 ep, u16 dp, u16 e);

void rc_set_io(u16 dsw, u16 in)
{
    *(u16 *)(rc_iopg + 0x0) = dsw;
    *(u16 *)(rc_iopg + 0x2) = in;
    *(u16 *)(rc_iopg + 0x4) = 0xFF00;
    *(u16 *)(rc_iopg + 0x8) = 0xFFF3;
    *(u16 *)(rc_iopg + 0xE) = 0x0000;      /* OKI: nunca ocupado */
}
void rc_mem_init(void)
{
    int i;
    for (i = 0; i < 0x1004; i++) rc_ffpg[i] = rc_iopg[i] = 0xFF;
    for (i = 0; i < 4096; i++) { rc_rpg[i] = rc_ffpg; rc_wpg[i] = 0; }
    for (i = 0; i < 256; i++) rc_rpg[i] = rc_rom + i * 0x1000;
    for (i = 0; i < 4; i++) {
        rc_rpg[0x100 + i] = rc_vram + i * 0x1000;           /* VRAM: lectura directa, escritura cifrada */
        rc_rpg[0x200 + i] = rc_wpg[0x200 + i] = rc_pal + i * 0x1000;
        rc_rpg[0xFEC + i] = rc_wpg[0xFEC + i] = rc_ram + i * 0x1000;
    }
    rc_rpg[0x108] = rc_wpg[0x108] = rc_vregs;
    rc_rpg[0x440] = rc_wpg[0x440] = rc_spr;
    rc_rpg[0x700] = rc_iopg;
    rc_set_io(0xFFDF, 0xFFEF);
}
u32 rc_rd16(u32 a) { return RD16(a); }
u32 rc_rd8(u32 a) { return RD8(a); }
u32 rc_vram_writes;
static u32 lastpc = 0xFFFFFFFF; static int lastoff = -2; static u16 lastenc, lastdec;
void rc_wr16(u32 a, u32 v, u32 pc)
{
    a &= 0xFFFFFF;
    if (a >= 0x100000 && a < 0x104000) {
        rc_vram_writes++;
        int off = (a & 0x3FFF) >> 1;
        if (lastpc == pc && off == lastoff + 1) { lastpc = 0xFFFFFFFF; v = gdecrypt(lastenc, lastdec, v); }
        else { lastenc = v; lastpc = pc; lastoff = off; v = lastdec = gdecrypt(0, 0, v); }
        *(u16 *)(rc_vram + (a & 0x3FFE)) = v;
    }
}
/* ---- OKI M6295: orden de frases, canales ocupados (estado en 0x70000F) y banco (0x70000D) */
extern const unsigned short oki_len[16][128];
static u8 oki_wait, oki_phr, oki_bank;
static u16 oki_cnt[4];
u8 oki_busy;
void snd_play(int ch, int phrase, int bank, int att);   /* los pone cada plataforma */
void snd_stop(int mask);
static void oki_status(void) { *(u16 *)(rc_iopg + 0xE) = oki_busy; }
void rc_oki_frame(void)
{
    int c;
    for (c = 0; c < 4; c++) if (oki_cnt[c] && !--oki_cnt[c]) oki_busy &= ~(1 << c);
    oki_status();
}
void rc_wr8(u32 a, u32 v, u32 pc)
{
    (void)pc;
    a &= 0xFFFFFF;
    if (a == 0x70000D) { oki_bank = v & 15; return; }
    if (a != 0x70000F) return;
    if (oki_wait) {
        int c;
        oki_wait = 0;
        for (c = 0; c < 4; c++) {
            if (!(v & (0x10 << c)) || (oki_busy & (1 << c))) continue;
            if (!oki_len[oki_bank][oki_phr]) continue;
            oki_busy |= 1 << c; oki_cnt[c] = oki_len[oki_bank][oki_phr];
            snd_play(c, oki_phr, oki_bank, v & 15);
        }
    } else if (v & 0x80) { oki_phr = v & 0x7F; oki_wait = 1; }
    else {
        int m = (v >> 3) & 15;
        if (m) { oki_busy &= ~m; snd_stop(m); { int c; for (c = 0; c < 4; c++) if (m & (1 << c)) oki_cnt[c] = 0; } }
    }
    oki_status();
}

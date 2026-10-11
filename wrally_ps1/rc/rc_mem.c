/* Mapa de memoria del World Rally para el codigo recompilado (comun al PC y a la PS1) */
#include "rc_rt.h"
u8 *rc_rpg[4096], *rc_wpg[4096];
#ifndef RC_ROM_EXTERN
u8 rc_rom[0x100000 + 4] __attribute__((aligned(4)));
#endif
u8 rc_ram[RC_RAMSZ] __attribute__((aligned(8))), rc_pal[0x4000 + 4] __attribute__((aligned(4)));
u8 rc_spr[0x1000 + 4] __attribute__((aligned(4))), rc_vram[0x4000 + 4] __attribute__((aligned(4)));
u8 rc_vregs[0x1000 + 4] __attribute__((aligned(4)));
u8 rc_iopg[0x1000 + 4] __attribute__((aligned(4))), rc_ffpg[0x1000 + 4] __attribute__((aligned(4)));
rc_regs_t rc_r;
void rc_gcrypt_init(void);
/* estado de los pares de escrituras a la VRAM (rc_vw16/rc_vw32 en rc_rt.h) */
u32 rc_vl_pc = 0xFFFFFFFF, rc_vl_off = 0xFFFFFFFE; u16 rc_vl_enc, rc_vl_dec;

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
    rc_gcrypt_init();
}
u32 rc_rd16(u32 a) { return RD16(a); }
u32 rc_rd8(u32 a) { return RD8(a); }
u32 rc_vram_writes;
#ifdef RCREG
/* perfil de regiones (rc/regiones.sh): se vuelca al salir en el fichero $RCREG */
#include <stdio.h>
#include <stdlib.h>
u32 rc_cur; u16 rc_regmask[0x10000];
static void rc_reg_dump(void) { FILE *f = fopen(getenv("RCREG"), "wb"); if (f) { fwrite(rc_regmask, 2, 0x10000, f); fclose(f); } }
__attribute__((constructor)) static void rc_reg_init(void) { atexit(rc_reg_dump); }
#endif
/* el codigo recompilado escribe en la VRAM sin pasar por aqui (rc_wr16_s/rc_wr32_s en rc_rt.h) */
void rc_wr16(u32 a, u32 v, u32 pc)
{
    a &= 0xFFFFFF;
    if (a >= 0x100000 && a < 0x104000) rc_vw16(a, v & 0xFFFF, pc);
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

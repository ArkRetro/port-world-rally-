/* Mapa de memoria del World Rally 2 para el codigo recompilado (comun al PC y a la PS1) */
#include "rc_rt.h"
#include "gae1.h"
u8 *rc_rpg[4096], *rc_wpg[4096];
#ifndef RC_ROM_EXTERN
u8 rc_rom[0x100000 + 4] RC_AL;
#endif
u8 rc_ram[0x10000 + 4] RC_AL, rc_vram[0x10000 + 4] RC_AL, rc_pal[0x2000 + 4] RC_AL, rc_xram[0x2000 + 4] RC_AL;
u8 rc_vregs[0x1000 + 4] RC_AL, rc_iopg[0x1000 + 4] RC_AL, rc_ffpg[0x1000 + 4] RC_AL;
rc_regs_t rc_r;
u16 rc_latch;          /* LS259 de 0x400000 (bit n = 0x400000 + 8n): contadores de monedas y el ADC del volante */
#ifdef RCREG
static u32 rc_fr;      /* cuadro, para el perfil de regiones */
#endif

/* entradas (activas a 0). IN0: DIPSW 2 + jugador 1 (arriba 01, abajo 02, derecha 04, izquierda 08, acelerar 10,
   marcha 20 activa a 1, ADC 40, START 80); IN1: DIPSW 1; IN2: jugador 2 + moneda 1 en el bit 8; IN3: servicio */
void rc_set_io(u16 in0, u16 in1, u16 in2, u16 in3)
{
#ifdef RCREG
    rc_fr++;
#endif
    *(u16 *)(rc_iopg + 0x0) = in0;
    *(u16 *)(rc_iopg + 0x2) = in1;
    *(u16 *)(rc_iopg + 0x4) = in2;
    *(u16 *)(rc_iopg + 0x6) = in3;
}

/* ---- GAE1: sus registros viven en la VRAM, en 0x2890 (lo que el juego lee). Las muestras pasan con el reloj del
   68000: en cada escritura se avanza hasta el momento de la instruccion (rc_r.cyrel, ciclos hasta el proximo vblank) y
   al acabar el cuadro hasta el final. 7 812,5 muestras por segundo a 59,1 cuadros: 132 o 133 por cuadro */
static u32 snd_acu, snd_nfr, snd_hecho;
static void snd_cuadro_nuevo(void) { snd_acu += 7812500; snd_nfr = snd_acu / 59100; snd_acu %= 59100; snd_hecho = 0; }
static void snd_al_dia(void)
{
    s32 pos = CYC_FRAME + rc_r.cyrel;
    u32 obj;
    if (pos < 0) pos = 0;
    if (pos > CYC_FRAME) pos = CYC_FRAME;
    obj = (u32)((u64)snd_nfr * (u32)pos / CYC_FRAME);
    if (obj > snd_hecho) { gae1_avanza(obj - snd_hecho); snd_hecho = obj; }
}
/* al tomar la IRQ del vblank (antes de que el juego lea los registros) */
void rc_snd_vblank(void)
{
    if (snd_nfr > snd_hecho) gae1_avanza(snd_nfr - snd_hecho);
    snd_cuadro_nuevo();
}

void rc_mem_init(void)
{
    int i;
    for (i = 0; i < 0x1004; i++) rc_ffpg[i] = rc_iopg[i] = 0xFF;
    for (i = 0; i < 4096; i++) { rc_rpg[i] = rc_ffpg; rc_wpg[i] = 0; }
    for (i = 0; i < 256; i++) rc_rpg[i] = rc_rom + i * 0x1000;
    for (i = 0; i < 16; i++) {
        rc_rpg[0x200 + i] = rc_wpg[0x200 + i] = rc_vram + i * 0x1000;
        rc_rpg[0xFE0 + i] = rc_wpg[0xFE0 + i] = rc_ram + i * 0x1000;
    }
    rc_wpg[0x202] = 0;                                       /* registros del GAE1: escritura por rc_wr16 */
    for (i = 0; i < 2; i++) {
        rc_rpg[0x210 + i] = rc_wpg[0x210 + i] = rc_pal + i * 0x1000;
        rc_rpg[0x212 + i] = rc_wpg[0x212 + i] = rc_xram + i * 0x1000;
    }
    rc_rpg[0x218] = rc_wpg[0x218] = rc_vregs;
    rc_rpg[0x300] = rc_iopg;
    rc_set_io(0xDDDF, 0xFFFF, 0xFFDF, 0xFFFF);
    gae1_init((u16 *)(rc_vram + 0x2890));
    snd_acu = 0; snd_cuadro_nuevo();
}
u32 rc_rd16(u32 a) { return RD16(a); }
u32 rc_rd8(u32 a) { return RD8(a); }
#ifdef RCREG
/* perfil de regiones (rc/regiones.py): se vuelca al salir en el fichero $RCREG, con las cuentas detras (total y
   maximo en un cuadro; el cuadro lo marca rc_set_io) */
#include <stdio.h>
#include <stdlib.h>
u32 rc_cur; u16 rc_regmask[0x10000];
static u32 rc_cnt[0x10000], rc_fmax[0x10000], rc_fcnt[0x10000], rc_fst[0x10000], rc_bk[0x10000];
void rc_regbk(u32 pc) { rc_bk[(pc >> 1) & 0xFFFF]++; }
void rc_regck(u32 pc)
{
    u32 i = (pc >> 1) & 0xFFFF;
    rc_cur = pc; rc_cnt[i]++;
    if (rc_fst[i] != rc_fr) { rc_fst[i] = rc_fr; rc_fcnt[i] = 0; }
    if (++rc_fcnt[i] > rc_fmax[i]) rc_fmax[i] = rc_fcnt[i];
}
static void rc_reg_dump(void)
{
    FILE *f = fopen(getenv("RCREG"), "wb");
    if (f) { fwrite(rc_regmask, 2, 0x10000, f); fwrite(rc_cnt, 4, 0x10000, f); fwrite(rc_fmax, 4, 0x10000, f); fwrite(&rc_fr, 4, 1, f); fwrite(rc_bk, 4, 0x10000, f); fclose(f); }
}
__attribute__((constructor)) static void rc_reg_init(void) { atexit(rc_reg_dump); }
#endif
/* escrituras a paginas sin puntero: la de la VRAM con el GAE1 (0x202xxx), el latch y lo que no esta mapeado */
static void wr_mask(u32 a, u32 v, u32 m)
{
    a &= 0xFFFFFE;
    if ((a >> 12) == 0x202) {
        if (a - 0x202890 < 0x70) { snd_al_dia(); gae1_w((a - 0x202890) >> 1, v, m); }
        else MW(rc_vram, a & 0xFFFF) = (u16)((MW(rc_vram, a & 0xFFFF) & ~m) | (v & m));
    } else if ((a >> 6) == (0x400000 >> 6)) {
        u32 b = (a >> 3) & 7;
        rc_latch = (u16)((rc_latch & ~(1u << b)) | ((v & 1) << b));
    }
}
void rc_wr16(u32 a, u32 v, u32 pc) { (void)pc; wr_mask(a, v, 0xFFFF); }
void rc_wr8(u32 a, u32 v, u32 pc) { (void)pc; if (a & 1) wr_mask(a, v & 0xFF, 0x00FF); else wr_mask(a, (v & 0xFF) << 8, 0xFF00); }
static void wr16p(u32 a, u32 v) { u8 *p = rc_wpg[(a >> 12) & 0xFFF]; if (p) *(u16 *)(p + (a & 0xFFE)) = (u16)v; else wr_mask(a, v, 0xFFFF); }
void rc_wr32(u32 a, u32 v, u32 pc) { (void)pc; wr16p(a, v >> 16); wr16p(a + 2, v & 0xFFFF); }

/* Mapa de memoria del World Rally 2 para el codigo recompilado (comun al PC y a la PS1) */
#include "rc_rt.h"
#include "gae1.h"
u8 *rc_rpg[4096], *rc_wpg[4096], *rc_romtab[4096];
void (*rc_romf_gancho)(u32 a);      /* la plataforma: carga el bloque (PS1: del CD; banco: ROMCHK) */
void rc_romf(u32 a) { if (rc_romf_gancho) rc_romf_gancho(a); else rc_bad(0xA00000 | a); }
#ifndef RC_GEN
u8 *rc_romp(u32 a) { u8 *p = rc_romtab[(a >> 8) & 0xFFF]; if (!p) { rc_romf(a & 0xFFFFF); p = rc_romtab[(a >> 8) & 0xFFF]; } return p + (a & 0xFFFFF); }
#endif
void rc_rom_mapa(u32 a0, u32 a1, u8 *p)
{
    u32 b;
    for (b = a0 >> 8; b < ((a1 + 255) >> 8) && b < 4096; b++) rc_romtab[b] = p ? p - a0 : 0;
}
/* los tramos residentes a rc_romres (desde una ROM contigua: el banco y R0) y a la tabla */
void rc_rom_residente(const u8 *rom)
{
    u32 k, o = 0, i;
    for (k = 0; k < rc_romres_n; k++) {
        u32 a0 = rc_romres_tab[k][0], n = rc_romres_tab[k][1] - a0 + 4;
        for (i = 0; i < n; i++) rc_romres[o + i] = rom[a0 + i];
        rc_rom_mapa(a0, rc_romres_tab[k][1], rc_romres + o);
        o += n;
    }
}
#ifndef RC_ROM_EXTERN
u8 rc_rom[0x100000 + 4] RC_AL;
#endif
u8 rc_ram[0x10000 + 4] RC_AL, rc_vram[0x10000 + 4] RC_AL, rc_pal[0x2000 + 4] RC_AL, rc_xram[0x2000 + 4] RC_AL;
u8 rc_vregs[0x1000 + 4] RC_AL, rc_iopg[0x1000 + 4] RC_AL, rc_ffpg[0x1000 + 4] RC_AL;
rc_regs_t rc_r;
u32 rc_cuadro;         /* cuadro (lo pone la plataforma: perfiles por cuadros) */
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
    s32 pos = CYC_FRAME + RC_REL;
    u32 obj;
    if (pos < 0) pos = 0;
    if (pos > CYC_FRAME) pos = CYC_FRAME;
    obj = snd_nfr * (u32)pos / CYC_FRAME;          /* <= 133 x 219 966: cabe en 32 bits (sin division de 64 en la PS1) */
    if (obj > snd_hecho) { gae1_avanza(obj - snd_hecho); snd_hecho = obj; }
}
/* al tomar la IRQ del vblank (antes de que el juego lea los registros) */
void rc_snd_vblank(void)
{
    if (snd_nfr > snd_hecho) gae1_avanza(snd_nfr - snd_hecho);
    snd_cuadro_nuevo();
}

/* ---- plazos del contador de ciclos (rc_rt.h): el vblank si la IRQ no esta enmascarada y los sucesos de la MCU */
#define RC_INF 0x3FFFFFFF
s32 rc_dl;
s32 mcu_proximo(void); void mcu_sucesos(s32 ahora); void mcu_desplaza(s32 d);
void rc_plazo(void)
{
    s32 rel = RC_REL, dl = rc_r.imask < 6 ? 0 : RC_INF, e = mcu_proximo();
    if (e < dl) dl = e;
    rc_dl = dl; rc_r.cyrel = rel - dl;
}
int rc_sucesos(void)
{
    s32 rel = RC_REL;
    mcu_sucesos(rel);
    rc_plazo();
    return rel >= 0 && rc_r.imask < 6;
}
/* tomada la IRQ: el contador pasa al vblank siguiente (los que hayan pasado con la IRQ enmascarada cuentan como uno) */
void rc_vb_cuadro(void)
{
    s32 rel = RC_REL, k = 1 + rel / CYC_FRAME;
    rel -= k * CYC_FRAME; mcu_desplaza(k * CYC_FRAME);
    rc_dl = 0; rc_r.cyrel = rel;
}

void rc_mem_init(void)
{
    int i;
    for (i = 0; i < 0x1004; i++) rc_ffpg[i] = rc_iopg[i] = 0xFF;
    for (i = 0; i < 4096; i++) { rc_rpg[i] = rc_ffpg; rc_wpg[i] = 0; }
    for (i = 0; i < 256; i++) rc_rpg[i] = 0;                 /* ROM: por bloques (rc_romtab) */
    rc_rom_mapa(0, 0x100000, rc_rom); rc_rom_residente(rc_rom);   /* banco y R0 (ROM en el EXE): toda la ROM debajo */
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
/* fin de la suma de la ROM del arranque (suceso de rc68.py en PC 0x2616): en R1 se libera aqui lo no residente */
int rc_suma_hecha;
void rc_rom_suma(void) { rc_suma_hecha = 1; }
u32 rc_rd16(u32 a) { return RD16(a); }
u32 rc_rd8(u32 a) { return RD8(a); }
#ifdef RCREG
/* perfil de regiones (rc/regiones.py): se vuelca al salir en el fichero $RCREG, con las cuentas detras (total y
   maximo en un cuadro; el cuadro lo marca rc_set_io) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
u32 rc_cur; u16 rc_regmask[0x10000];
static u32 rc_cnt[0x10000], rc_fmax[0x10000], rc_fcnt[0x10000], rc_fst[0x10000], rc_bk[0x10000];
void rc_regbk(u32 pc) { rc_bk[(pc >> 1) & 0xFFFF]++; }
static long rc_va = -1, rc_vb;      /* REGWIN=a-b: solo cuenta en los cuadros [a, b] (rc_cuadro, de la plataforma) */
void rc_regck(u32 pc)
{
    u32 i = (pc >> 1) & 0xFFFF;
    if (rc_va < 0) { const char *q = getenv("REGWIN"); rc_va = q ? atol(q) : 0; rc_vb = q && strchr(q, '-') ? atol(strchr(q, '-') + 1) : 0x7FFFFFFF; }
    rc_cur = pc;
    {   /* PCT=pc: momento de cada paso por pc en la ventana REGWIN (us desde el cuadro 0) */
        static long pct = -2; if (pct == -2) pct = getenv("PCT") ? strtol(getenv("PCT"), 0, 16) : -1;
        if ((long)pc == pct && (long)rc_cuadro >= rc_va && (long)rc_cuadro <= rc_vb)
            fprintf(stderr, "PC %06X cuadro %u %.1f us\n", pc, rc_cuadro, (rc_cuadro * (double)CYC_FRAME + RC_REL) / 13.0);
    }
    if ((long)rc_cuadro < rc_va || (long)rc_cuadro > rc_vb) return;
    rc_cnt[i]++;
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

#include "ng.h"
#include "mcs51.h"
extern const u8 mcu_rom[];
extern const u16 dec_first[];
u16 gdecrypt(u16 ep, u16 dp, u16 e);
void video_mark(u32 a); void sound_init(void); void sound_frame(void); void video_init(void); void video_frame(void);

#define IO ((vu16*)0x10A000)
#define WR(a) (*(vu16*)(0x10C000 + ((a) - 0xFEC000)))
mcs51_t mcu;
volatile u16 ng_frames;
u16 dbg_vfix, dbg_mcu;
u32 dbg_mcusteps;
u8 vdirty[2][32];
u8 gear, lastb, is_mvs;
#ifndef FSKIP
#define FSKIP 0            /* salto de cuadros fijo: no se dibuja 1 de cada FSKIP VBLANK (0 = sin salto) */
#endif          /* bit por columna de tiles (64 columnas) y capa */

static void fix_print(int x, int y, const char *s) { REG_VRAMMOD = 32; REG_VRAMADDR = 0x7000 + x * 32 + y; while (*s) VW(0xF000 | (u8)*s++); }
static void fix_hex(int x, int y, u32 v, int n) {
    char b[9]; int i;
    for (i = n - 1; i >= 0; i--) { int d = v & 15; b[i] = d < 10 ? '0' + d : 'A' + d - 10; v >>= 4; }
    b[n] = 0; fix_print(x, y, b);
}

void video_layer_reset(u16 layer, u16 cat1);
void vram_fill(u32 addr, u32 enc)
{
    u16 e0 = enc >> 16, e1 = enc, d0 = dec_first[e0], d1 = gdecrypt(e0, d0, e1);
    u32 v = ((u32)d0 << 16) | d1, *p = (u32 *)addr;
    u16 i;
    for (i = 0; i < 0x800; i++) p[i] = v;
    video_layer_reset(addr == 0x102000, d1 & 0x20);
}

void mcu_task_rc(mcs51_t *c);
void mcu_task(void)
{
#ifdef MCU_CHK
    *(vu16 *)0x10BFF0 = 1;          /* marca para el comprobador (Lua) */
#endif
    PM(6);
    mcu_task_rc(&mcu);
    PM(7);
#ifdef MCU_CHK
    *(vu16 *)0x10BFF0 = 2;
#endif
    mcu.pc = 0x013F;
    dbg_mcu++;
}

void ng_init(void)
{
    u16 i;
    u32 *p;
    u8 mvs = *(vu8 *)0x10FD82;          /* BIOS_MVS_FLAG (antes de que el juego use esa RAM) */
    REG_LSPCMODE = 0;
    REG_IRQACK = 7;
    /* temporizador: IRQ2 ~8 veces por cuadro (reloj de pixel 6 MHz) para el sonido */
    REG_LSPCMODE = 0x00B0;              /* habilitar, recargar al escribir y al llegar a 0 */
    *(vu16 *)0x3C0008 = 0;
#ifdef TIMERX
    *(vu16 *)0x3C0008 = TIMERX >> 16; *(vu16 *)0x3C000A = TIMERX & 0xFFFF;
#else
    *(vu16 *)0x3C000A = 25000;
#endif          /* (la recarga se produce al escribir la parte baja) */
    for (p = (u32 *)0x10A000; p < (u32 *)0x10C000; p++) *p = 0;
    REG_VRAMMOD = 1; REG_VRAMADDR = 0; for (i = 0; i < 0x7000; i++) { VW(0); if (!(i & 0x3FF)) REG_WATCHDOG = 0; }
    for (i = 0; i < 0x500; i++) VW(0xF020);
    /* sin bordes negros: la imagen reducida 13/16 cubre los 320 px (los ~10 px de cada lado son
       mapa algo mas alla del borde original) */
    REG_WATCHDOG = 0;
    REG_VRAMADDR = 0x8000; for (i = 0; i < 0x200; i++) VW(0x0FFF);
    for (i = 0; i < 0x200; i++) VW(0);
    for (i = 0; i < 0x200; i++) VW(0);
    PALRAM[15*16+1] = 0x7FFF; PALRAM[15*16+2] = 0x8000; BACKDROP = 0x0000;
#ifdef WR_DEBUG
    fix_print(2, 3, "WORLD RALLY NEO GEO - FASE 1");
#endif
    REG_WATCHDOG = 0;
    video_init();
    sound_init();
    /* MCU: arranque hasta el bucle principal */
    mcu.code = mcu_rom; mcu.xram = (u8 *)0x10C000;
    mcs51_reset(&mcu);
    mcs51_run(&mcu, 0x013F, 100000);
    /* entradas: DSW con juego libre, joystick, 2 mandos */
    gear = 0;
    is_mvs = mvs;
    /* DSW: valores de fabrica (con monedas, sonidos de demo), mandos de joystick */
    IO[0] = 0xFFDF; IO[1] = 0xFFEF; IO[2] = 0xFF00; IO[4] = 0xFFF3;
}

#ifdef WR_DEBUG
u16 dbg_copy[96];
u16 dbg_scb1[2][64];
u16 dbg_s34[2][448];
u16 dbg_slot;
static void dbg_vram(void)
{
    u16 i;
    for (i = 0; i < 448; i++) { REG_VRAMADDR = 0x8200 + i; dbg_s34[0][i] = REG_VRAMRW; REG_VRAMADDR = 0x8400 + i; dbg_s34[1][i] = REG_VRAMRW; }
    REG_VRAMMOD = 1;
    REG_VRAMADDR = 0x7000;
    for (i = 0; i < 32; i++) { REG_VRAMADDR = 0x7000 + i; dbg_copy[i] = REG_VRAMRW; }
    for (i = 0; i < 42; i++) { REG_VRAMADDR = 0x8401 + i; dbg_copy[32 + i] = REG_VRAMRW; }
    for (i = 0; i < 64; i++) { REG_VRAMADDR = (1 + (dbg_slot & 0xFF)) * 64 + i; dbg_scb1[0][i] = REG_VRAMRW; }
    for (i = 0; i < 64; i++) { REG_VRAMADDR = (22 + (dbg_slot & 0xFF)) * 64 + i; dbg_scb1[1][i] = REG_VRAMRW; }
    dbg_slot = 0;
}
#endif
void ng_vblank(void)
{
    u8 p1 = ~REG_P1CNT, p2 = ~REG_P2CNT, st = ~REG_STATUS_B;
    u16 v = 0xFFFF;
    PM(1);
    ng_frames++;
#ifdef SLOWTEST
    { volatile u16 _i; for (_i = 0; _i < SLOWTEST; _i++); }
#endif
    /* P1: arriba 1 abajo 2 izq 4 der 8 A 10 B 20 C 40 D 80 (Neo Geo, activos a 1 tras ~) */
    if (p1 & 1) v &= ~0x0001; if (p1 & 2) v &= ~0x0002; if (p1 & 8) v &= ~0x0004; if (p1 & 4) v &= ~0x0008;
    if (p1 & 0x10) v &= ~0x0020;
    if ((p1 & 0x20) && !(lastb & 0x20)) gear ^= 1;
    lastb = p1;
    if (!gear) v &= ~0x0010;
    if (p2 & 1) v &= ~0x0100; if (p2 & 2) v &= ~0x0200; if (p2 & 8) v &= ~0x0400; if (p2 & 4) v &= ~0x0800;
    if (p2 & 0x10) v &= ~0x2000; if (p2 & 0x20) v &= ~0x1000;
    if (st & 1) v &= ~0x4000; if (st & 4) v &= ~0x8000;
    /* monedas: SELECT del mando (AES) o ranuras de monedas (MVS) */
    if (st & 2) v &= ~0x0040;
    if (st & 8) v &= ~0x0080;
    if (is_mvs) { u8 c = ~REG_STATUS_A; if (c & 1) v &= ~0x0040; if (c & 2) v &= ~0x0080; }
    IO[1] = v;
    {   /* coche quieto en la salida hasta que empieza a correr el tiempo (ver stage_init_fix) */
        extern u16 start_a, start_b; extern u32 start_t0; extern u8 start_arm;
        if (start_arm) {
            u32 t = *(vu32 *)0x10C1AA;
            if (t != 0 && t != start_t0) start_arm = 0;
            else {
                *(vu32 *)0x10EAA0 = (u32)start_a << 16; *(vu32 *)0x10EAA4 = (u32)start_b << 16;
                *(vu32 *)0x10EAA8 = 0; *(vu32 *)0x10EAAC = 0;
            }
        }
    }
    /* lo que muestra este VBLANK es el ultimo cuadro completo del juego (convertido al terminarlo) */
    video_frame();
#ifdef PROFB_C
    *(vu16 *)0x10BFF6 = 21;
#endif
    sound_frame();
#ifdef PROFB_C
    *(vu16 *)0x10BFF6 = 22;
#endif
    PM(2);
#ifdef WR_DEBUG
    if (dbg_slot & 0x8000) dbg_vram();
#endif
#ifdef DBGSPD
    {
        extern u16 dbg_fid, dbg_skip; extern s8 tick_lead;
        static u16 n, f0, s0;
        if (++n >= 60) {
            u16 l = dbg_fid - f0, sk = dbg_skip - s0;
            f0 = dbg_fid; s0 = dbg_skip; n = 0;
            fix_print(1, 27, "BUC"); fix_hex(5, 27, l, 2);
            fix_print(8, 27, "SAL"); fix_hex(12, 27, sk, 2);
            fix_print(15, 27, "A6"); fix_hex(18, 27, WR(0xFEC1A6), 2);
            fix_print(21, 27, "TL"); fix_hex(24, 27, (u8)tick_lead, 2);
        }
        {
            extern void dbg_lay(u16 *o); u16 o[9];
            dbg_lay(o);
            fix_print(1, 26, "L0"); fix_hex(4, 26, o[0], 2); fix_hex(7, 26, o[1], 4); fix_hex(12, 26, o[2], 2);
            fix_print(15, 26, "L1"); fix_hex(18, 26, o[3], 2); fix_hex(21, 26, o[4], 4); fix_hex(26, 26, o[5], 2);
            fix_print(29, 26, "D"); fix_hex(30, 26, o[6] | (o[8] << 4), 2); fix_hex(33, 26, o[7], 2);

        }
    }
#endif
#ifdef DIAG5
    {   /* terreno bajo el coche (0xFEEB5C, 14 codigos), velocidad, direccion y llamadas a la MCU por segundo */
        static u16 n5, m0, mr; u16 k;
        fix_print(1, 20, "G");
        for (k = 0; k < 14; k++) fix_hex(2 + 3 * k - (k >= 7 ? 21 : 0), k >= 7 ? 22 : 20, WR(0xFEEB5C + 2 * k) & 0xFF, 2);
        fix_print(1, 22, "H");
        if (++n5 >= 60) { n5 = 0; mr = dbg_mcu - m0; m0 = dbg_mcu; }
        fix_print(1, 24, "V"); fix_hex(2, 24, WR(0xFEEAD2), 4); fix_print(7, 24, "D"); fix_hex(8, 24, WR(0xFEEAB0), 4);
        fix_print(13, 24, "M"); fix_hex(14, 24, mr, 2); fix_print(17, 24, "X"); fix_hex(18, 24, WR(0xFEEA98), 4); fix_hex(23, 24, WR(0xFEEA9C), 4);
    }
#endif
#ifdef DIAG4
    {
        extern u8 d4_cnt[10], d4_last; extern u16 d4_la, d4_lb, d4_lv, d4_lf; u16 k;
        fix_print(1, 20, "W");
        for (k = 1; k < 10; k++) fix_hex(3 * k, 20, d4_cnt[k], 2);
        fix_print(1, 22, "L"); fix_hex(2, 22, d4_last, 1); fix_hex(4, 22, (u16)(ng_frames - d4_lf), 4);
        fix_hex(9, 22, d4_la, 4); fix_hex(14, 22, d4_lb, 4); fix_print(19, 22, "V"); fix_hex(20, 22, d4_lv, 4);
        fix_print(1, 24, "X"); fix_hex(2, 24, WR(0xFEEA98), 4); fix_print(7, 24, "Y"); fix_hex(8, 24, WR(0xFEEA9C), 4);
        fix_print(13, 24, "T"); fix_hex(14, 24, WR(0xFEC1AA), 4); fix_print(19, 24, "R"); fix_hex(20, 24, WR(0xFEEB1C), 4); fix_hex(25, 24, WR(0xFEEB1E), 4);
    }
#endif
#ifdef DIAG3
    {
        extern u32 dbg_v0, dbg_v4; extern u16 dbg_v1, dbg_v2, dbg_v3, dbg_v5, dbg_n1, dbg_n2;
        fix_print(1, 20, "R"); fix_hex(2, 20, dbg_v1, 4); fix_print(7, 20, "E"); fix_hex(8, 20, dbg_v2, 4);
        fix_print(13, 20, "I"); fix_hex(14, 20, dbg_v3, 4); fix_print(19, 20, "V"); fix_hex(20, 20, dbg_v0, 8);
        fix_print(29, 20, "N"); fix_hex(30, 20, dbg_n1, 2);
        fix_print(1, 22, "S"); fix_hex(2, 22, dbg_v5, 4); fix_print(7, 22, "P"); fix_hex(8, 22, dbg_v4, 8);
        fix_print(17, 22, "N"); fix_hex(18, 22, dbg_n2, 2);
        fix_print(1, 24, "A"); fix_hex(2, 24, WR(0xFEF378), 4); fix_hex(7, 24, WR(0xFEF37A), 4);
        fix_print(12, 24, "S"); fix_hex(13, 24, WR(0xFEC198), 4);
        fix_print(18, 24, "X"); fix_hex(19, 24, WR(0xFEEA98), 4); fix_print(24, 24, "Y"); fix_hex(25, 24, WR(0xFEEA9C), 4);
    }
#endif
#ifdef WR_DEBUG
    fix_hex(2, 5, ng_frames, 4);
    fix_hex(8, 5, WR(0xFEC1A6), 4);
    fix_hex(14, 5, dbg_vfix, 4);
    fix_hex(20, 5, dbg_mcu, 4);
    fix_hex(26, 5, dbg_mcusteps, 6);
#endif
}

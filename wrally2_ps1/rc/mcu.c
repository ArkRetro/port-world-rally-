/* MCU del World Rally 2 (DS5002FP): los dos trabajos que pide el 68000, traducidos a C (mcu_rc.c, rc51c.py), y lo
   demas de su programa hecho en C (mcu.md del analisis): eco complementado de FC08/FC0C, version 14.06, estado tras el
   arranque. Fuera: puerto serie, grabador de entradas, contador FC10 y aviso de fallo de tension. Los contadores de
   explotacion (FDDA-FDEB) empiezan a 0, como una NVRAM nueva. Direcciones de la MCU (0x8000 + n = byte 0xFE8000 + n
   del 68000; aqui con la parte alta, como en la RAM compartida: 0xFC15 = byte bajo de la palabra 0xFEFC14) */
#include "rc_rt.h"
#include "mcs51.h"
mcs51_t rc_mcu;
#define SH(m, a) (m)[(0x8000 + ((a) & 0x7FFF)) ^ 1]
/* 0: el trabajo al momento de la peticion (como el WR1; el banco contra Musashi lo necesita en los dos lados).
   1: con su tiempo, como en la placa (la MCU trabaja en paralelo con el 68000):
   - Recogida: su bucle principal mira FC15 cada PERIODO ciclos; tras un trabajo, los arranques posibles son fin +
     DESFASE + n PERIODO, y vale el primero que llegue AVISO ciclos despues de la peticion (ajustado con los tiempos de
     MAME de 6 600 trabajos: 139,075 us, 7,7 us y 8 us; acierta el 99,7 %). Sin un trabajo en los ultimos cuadros
     (primero de una carrera) no se sabe la fase: RECOGIDA, la media de MAME (77 us).
   - En la recogida el trabajo (0x074D y el guion 0x40E5) corre entero sobre una copia de FC00-FDFF y apunta cada lectura
     y cada escritura con su momento (ciclos maquina contados por rc51c.py, MCK). Las escrituras se aplican a la RAM en su
     momento; cada lectura se comprueba en el suyo: si el 68000 ha cambiado ese byte desde la recogida (en las demos
     escribe la entrada grabada en FC46 cientos de us despues de pedir, y la MCU la lee mas tarde), el trabajo se repite
     desde la copia con el valor nuevo; de la repeticion valen lo que va desde ese momento (lo anterior ya se aplico).
     El trabajo es una funcion pura de la RAM compartida (mcu.md), asi que la repeticion es exacta salvo las lecturas
     anteriores del mismo byte. */
int mcu_modelo = 1;
void (*mcu_traza)(int k, s32 t);    /* depuracion (rc_host MCUV): 0 peticion, 1 recogida, 3 fin, 4 repeticion */
unsigned long mcu_repes, mcu_trabajos;   /* repeticiones y trabajos recogidos (cifras; puerta G-MCU) */
unsigned mcu_vigila;                /* depuracion: escrituras del trabajo a esta direccion (MCUVA) */
#define MCU_INF 0x3FFFFFFF
#define RECOGIDA 1006
#define PERIODO 1808
#define DESFASE 101
#define AVISO 104
/* ciclos maquina -> ciclos del 68000 (los dos a 13 MHz; 12 relojes por ciclo, y la IRQ del temporizador 1 se lleva 4
   de cada 113) */
#define MCK(mc) ((s32)((mc) * 12u * 113u / 109u))
#ifndef MCS51_XOR
#define MCS51_XOR 0
#endif
#define mcu_mc mcu_x.mc
#define mcu_sellos mcu_x.sellos
#define XRM(a) mcu_x.xm[((a) & 0x7FFF) ^ MCS51_XOR]
void mcu_part(mcs51_t *c, int k);
static s32 t1 = MCU_INF, tbase;                    /* recogida pendiente; momento de la recogida en curso */
static s32 ranura = MCU_INF;                       /* un arranque posible del bucle de la MCU (tras el ultimo trabajo) */
/* accesos del trabajo con su momento, en orden: escritura (se aplica) o lectura (se comprueba) */
typedef struct { s32 t; u16 a; u8 v, lee; } mcu_acc;
#define NQ 256                                    /* el trabajo mas largo apunta 92 accesos */
static mcu_acc q[NQ], q2[NQ], *qw = q;
static int nq, iq, nqw;
static u8 sh0[0x200] RC_AL, sh[0x200] RC_AL;      /* FC00-FDFF en la recogida (con lo cambiado por el 68000) y la copia */
static void cp512(u8 *d, const u8 *s) { int i; for (i = 0; i < 0x200 / 4; i++) ((u32 *)d)[i] = ((const u32 *)s)[i]; }   /* sin libc en la PS1 */
void mcu_log(unsigned short a, unsigned char v)
{
    if (nqw < NQ) { qw[nqw].t = tbase + MCK(mcu_mc); qw[nqw].a = a; qw[nqw].v = v; qw[nqw].lee = 0; nqw++; }
}
/* MOVC: el trabajo solo lee tablas en 1025-40E3 y 5488-54CD (mcu.md 2.1). En la PS1 (MCU_TABLAS) solo estan esas,
   seguidas en mcu_tab (build.sh); en el banco se lee la ROM entera y se cuentan las lecturas de fuera (rc_host) */
#define TAB0 0x1025
#define TAB1 0x40E4
#define TAB2 0x5488
#define TAB3 0x54CE
#ifdef MCU_TABLAS
extern const u8 mcu_tab[];
#define TB(i, b, o) mcu_tab[(i) - (b) + (o)]
#else
#define TB(i, b, o) mcu_x.code[i]
#endif
unsigned mcu_movc_fuera;
u8 mcu_movc(u16 i)
{
    if (i >= TAB0 && i < TAB1) return TB(i, TAB0, 0);
    if (i >= TAB2 && i < TAB3) return TB(i, TAB2, TAB1 - TAB0);
    mcu_movc_fuera++;
#ifdef MCU_TABLAS
    return 0;
#else
    return mcu_x.code[i];
#endif
}
/* MOVX del trabajo traducido: con sellos (modelo con tiempo) sobre la copia sh y apuntado; sin ellos, directo */
void mcu_xw(unsigned short a, unsigned char v) { XRM(a) = v; if (mcu_sellos) mcu_log(a, v); }
unsigned char mcu_lee(unsigned short a)
{
    u8 v = XRM(a);
    if (mcu_sellos && nqw < NQ) { qw[nqw].t = tbase + MCK(mcu_mc); qw[nqw].a = a; qw[nqw].v = v; qw[nqw].lee = 1; nqw++; }
    return v;
}
s32 mcu_proximo(void) { s32 e = t1; if (iq < nq && q[iq].t < e) e = q[iq].t; return e; }
/* corre el trabajo entero desde sh0 apuntando en qw */
static void corre(mcu_acc *dst)
{
    cp512(sh, sh0);
    rc_mcu.xram = sh - 0x7C00;
    qw = dst; nqw = 0; mcu_mc = 0; mcu_sellos = 1;
    mcu_part(&rc_mcu, 0); mcu_part(&rc_mcu, 1);
    mcu_log(0xFC13, 1);                            /* fin: FC13 = 1 */
    mcu_sellos = 0;
}
/* lo vencido hasta ahora (ciclos desde el proximo vblank), en orden */
void mcu_sucesos(s32 ahora)
{
    for (;;) {
        s32 e = mcu_proximo();
        if (e > ahora || e == MCU_INF) break;
        if (iq < nq && q[iq].t == e) {
            mcu_acc *x = &q[iq];
            if (!x->lee) { rc_ram[x->a ^ 1] = x->v; iq++; if (mcu_traza && x->a == mcu_vigila) mcu_traza(5, e); }
            else if (rc_ram[x->a ^ 1] == x->v) iq++;
            else {
                /* el 68000 cambio el byte antes de que el trabajo lo leyera: se repite con lo que hay ahora; de la
                   repeticion se toma lo que va desde esta lectura */
                int k = 0, n = iq;
                sh0[(x->a - 0xFC00) ^ 1] = rc_ram[x->a ^ 1];
                corre(q2);
                while (k < nqw && q2[k].t < e) k++;
                while (k < nqw && n < NQ) q[n++] = q2[k++];
                nq = n; qw = q; mcu_repes++;
                ranura = q[nq - 1].t + DESFASE;
                if (mcu_traza) mcu_traza(4, e);
            }
        } else {
            /* recogida: FC15 = 0 y FC13 = 0 al momento; el trabajo corre sobre la copia */
            SH(rc_ram, 0xFC15) = 0; SH(rc_ram, 0xFC13) = 0;
            cp512(sh0, rc_ram + 0xFC00);
            tbase = e; t1 = MCU_INF; mcu_trabajos++;
            corre(q); nq = nqw; iq = 0;
            ranura = q[nq - 1].t + DESFASE;
            if (mcu_traza) { mcu_traza(1, e); mcu_traza(3, q[nq - 1].t); }
        }
    }
}
void mcu_desplaza(s32 d)
{
    int i;
    if (t1 != MCU_INF) t1 -= d;
    if (ranura != MCU_INF) ranura = ranura - d < -4 * CYC_FRAME ? MCU_INF : ranura - d;   /* fase vieja: no vale */
    for (i = iq; i < nq; i++) q[i].t -= d;
    tbase -= d;
}

void mcu_boot(u8 *m, const u8 *code, mcs51_t *c)
{
    int i;
    for (i = 0; i < 256; i++) c->iram[i] = 0;
    for (i = 0; i < 128; i++) c->sfr[i] = 0;
    c->sfr[0x01] = 0x70;                          /* SP */
    c->code = code; c->xram = m + 0x8000;
    SH(m, 0xFC01) = 0;                          /* sin fallo de tension */
    SH(m, 0xFDD8) = 0x0E; SH(m, 0xFDD9) = 0x06;
    for (i = 0xFDDA; i <= 0xFDEB; i++) SH(m, i) = 0;
}
/* trabajo A (jugador 1) o B (jugador 2): el 68000 escribe 1 en FC15/FC19 y la MCU lo hace al momento */
void mcu_trabajo(u8 *m, mcs51_t *c, int b)
{
    c->xram = m + 0x8000;
    if (!b) { SH(m, 0xFC15) = 0; SH(m, 0xFC13) = 0; mcu_job_a(c); SH(m, 0xFC13) = 1; }
    else { SH(m, 0xFC19) = 0; SH(m, 0xFC17) = 1; }  /* el trabajo B (jugador 2) no se pide con un monitor: no se traduce */
}
/* eco complementado de FC08 y FC0C (su bucle lo hace cada ~150 us: aqui al escribirlos el 68000 y en cada cuadro) */
void mcu_eco(u8 *m)
{
    SH(m, 0xFC0A) = ~SH(m, 0xFC08); SH(m, 0xFC0B) = ~SH(m, 0xFC09);
    SH(m, 0xFC0E) = ~SH(m, 0xFC0C); SH(m, 0xFC0F) = ~SH(m, 0xFC0D);
}
/* una vez por cuadro (rc_vblank): lo que su bucle principal hace sin parar */
void mcu_cuadro(u8 *m, mcs51_t *c)
{
    SH(m, 0xFDD8) = 0x0E; SH(m, 0xFDD9) = 0x06;
    mcu_eco(m);
    if (SH(m, 0xFC15) == 1 && (m != rc_ram || (t1 == MCU_INF && iq >= nq)))
        mcu_trabajo(m, c, 0);                             /* por si una peticion no paso por el gancho */
    if (SH(m, 0xFC19) == 1) mcu_trabajo(m, c, 1);
}
void rc_mcu_a(void)
{
    s32 ahora = RC_REL;
    if (!mcu_modelo) { mcu_trabajo(rc_ram, &rc_mcu, 0); return; }
    mcu_sucesos(MCU_INF - 1);                  /* si quedara algo del anterior, se termina ya */
    if (mcu_traza) mcu_traza(0, ahora);
    if (ranura == MCU_INF) t1 = ahora + RECOGIDA;
    else { s32 n = (ahora + AVISO - ranura + PERIODO - 1) / PERIODO; t1 = ranura + (n > 0 ? n : 0) * PERIODO; }
    if (t1 < rc_dl) { rc_dl = t1; rc_r.cyrel = ahora - t1; }
}
void rc_mcu_b(void) { mcu_trabajo(rc_ram, &rc_mcu, 1); }
void rc_mcu_eco(void) { mcu_eco(rc_ram); }

/* MCU del World Rally 2 (DS5002FP): los dos trabajos que pide el 68000, traducidos a C (mcu_rc.c, rc51c.py), y lo
   demas de su programa hecho en C (mcu.md del analisis): eco complementado de FC08/FC0C, version 14.06, estado tras el
   arranque. Fuera: puerto serie, grabador de entradas, contador FC10 y aviso de fallo de tension. Los contadores de
   explotacion (FDDA-FDEB) empiezan a 0, como una NVRAM nueva. Direcciones de la MCU (0x8000 + n = byte 0xFE8000 + n
   del 68000; aqui con la parte alta, como en la RAM compartida: 0xFC15 = byte bajo de la palabra 0xFEFC14) */
#include "rc_rt.h"
#include "mcs51.h"
mcs51_t rc_mcu;
#define SH(m, a) (m)[(0x8000 + ((a) & 0x7FFF)) ^ 1]

void mcu_boot(u8 *ram, const u8 *code, mcs51_t *c)
{
    int i;
    for (i = 0; i < 256; i++) c->iram[i] = 0;
    for (i = 0; i < 128; i++) c->sfr[i] = 0;
    c->sfr[0x01] = 0x70;                          /* SP */
    c->code = code; c->xram = ram + 0x8000;
    SH(ram, 0xFC01) = 0;                          /* sin fallo de tension */
    SH(ram, 0xFDD8) = 0x0E; SH(ram, 0xFDD9) = 0x06;
    for (i = 0xFDDA; i <= 0xFDEB; i++) SH(ram, i) = 0;
}
/* trabajo A (jugador 1) o B (jugador 2): el 68000 escribe 1 en FC15/FC19 y la MCU lo hace al momento */
void mcu_trabajo(u8 *ram, mcs51_t *c, int b)
{
    if (!b) { SH(ram, 0xFC15) = 0; SH(ram, 0xFC13) = 0; mcu_job_a(c); SH(ram, 0xFC13) = 1; }
    else { SH(ram, 0xFC19) = 0; SH(ram, 0xFC17) = 0; mcu_job_b(c); SH(ram, 0xFC17) = 1; }
}
/* una vez por cuadro (rc_vblank): lo que su bucle principal hace sin parar */
void mcu_cuadro(u8 *ram, mcs51_t *c)
{
    SH(ram, 0xFDD8) = 0x0E; SH(ram, 0xFDD9) = 0x06;
    SH(ram, 0xFC0A) = ~SH(ram, 0xFC08); SH(ram, 0xFC0B) = ~SH(ram, 0xFC09);
    SH(ram, 0xFC0E) = ~SH(ram, 0xFC0C); SH(ram, 0xFC0F) = ~SH(ram, 0xFC0D);
    if (SH(ram, 0xFC15) == 1) mcu_trabajo(ram, c, 0);   /* por si una peticion no paso por el gancho */
    if (SH(ram, 0xFC19) == 1) mcu_trabajo(ram, c, 1);
}
void rc_mcu_a(void) { mcu_trabajo(rc_ram, &rc_mcu, 0); }
void rc_mcu_b(void) { mcu_trabajo(rc_ram, &rc_mcu, 1); }

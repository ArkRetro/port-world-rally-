#ifndef MCS51_H
#define MCS51_H
/* Estado del 8051 (DS5002FP de Gaelco) que usa la traduccion de rc51c.py (como el interprete del WR1) */
typedef struct {
    unsigned char iram[256];
    unsigned char sfr[128];
    unsigned short pc;
    const unsigned char *code;   /* 32 KB de programa (SRAM) */
    unsigned char *xram;         /* 32 KB compartidos con el 68000 (0xFE8000-0xFEFFFF, MOVX >= 0x8000) */
    unsigned long steps;
} mcs51_t;
/* registros del 8051 y estado del trabajo traducido (rc51c.py): ciclos maquina, apuntar accesos (mcu.c) y punteros */
typedef struct {
    unsigned mc; int sellos;
    unsigned char *ir, *sf, *xm; const unsigned char *code;
    unsigned short dptr; unsigned char a, cy;
} mcs51_x;
extern mcs51_x mcu_x;
void mcu_job_a(mcs51_t *c);
#endif

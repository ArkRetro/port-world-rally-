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
void mcu_job_a(mcs51_t *c), mcu_job_b(mcs51_t *c);
#endif

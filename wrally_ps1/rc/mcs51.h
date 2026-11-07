#ifndef MCS51_H
#define MCS51_H
/* Interprete 8051 minimo para el DS5002FP de Gaelco (World Rally) */
typedef struct {
    unsigned char iram[256];
    unsigned char sfr[128];
    unsigned short pc;
    const unsigned char *code;   /* 32 KB programa */
    unsigned char *xram;         /* 16 KB compartidos con el 68000 (mascara 0x3fff) */
    unsigned long steps;
} mcs51_t;
void mcs51_reset(mcs51_t *c);
/* ejecuta hasta llegar a stop_pc (tras al menos 1 instruccion) o agotar max pasos; devuelve pasos */
long mcs51_run(mcs51_t *c, unsigned short stop_pc, long max);
#endif

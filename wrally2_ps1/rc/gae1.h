/* Registros del sonido GAE1 de Gaelco (como gaelco.cpp de MAME): 7 canales de 8 palabras, dos trozos por canal
   (palabra 1/5: tipo, banco y volumen; 2/6: final >> 8; 3/7: muestras que quedan, que el juego lee) */
#ifndef GAE1_H
#define GAE1_H
#define GAE1_CANALES 7
typedef struct { unsigned char activo, bucle, trozo; } gae1_canal;
extern gae1_canal gae1_c[GAE1_CANALES];
extern unsigned short *gae1_reg;     /* 56 palabras (en el 68000, 0x202890-0x2028FF) */
/* sucesos para la plataforma: el canal ch empieza a sonar el trozo t (0/1) o se calla (t = -1) */
extern void (*gae1_suceso)(int ch, int t);
extern void (*gae1_traza)(unsigned off, unsigned dato, unsigned mascara);   /* cada escritura (banco: SNDW) */
void gae1_init(unsigned short *regs);
void gae1_w(unsigned off, unsigned dato, unsigned mascara);
void gae1_avanza(unsigned n);        /* n muestras a 7812,5 Hz */
#endif

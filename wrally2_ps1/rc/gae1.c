/* Registros del GAE1 (sonido de Gaelco) sin generar sonido: lo que el juego ve (las muestras que quedan en cada trozo,
   que lee en la IRQ) y los sucesos de cada canal para la plataforma. Igual que gaelco.cpp de MAME, pero avanzando las
   muestras de golpe (una vez por cuadro) en vez de una a una */
#include "gae1.h"
gae1_canal gae1_c[GAE1_CANALES];
unsigned short *gae1_reg;
void (*gae1_suceso)(int ch, int t);
void (*gae1_traza)(unsigned off, unsigned dato, unsigned mascara);
static void suceso(int ch, int t) { if (gae1_suceso) gae1_suceso(ch, t); }
void gae1_init(unsigned short *regs)
{
    int i;
    gae1_reg = regs;
    for (i = 0; i < GAE1_CANALES * 8; i++) gae1_reg[i] = 0;
    for (i = 0; i < GAE1_CANALES; i++) gae1_c[i].activo = gae1_c[i].bucle = gae1_c[i].trozo = 0;
}
void gae1_w(unsigned off, unsigned dato, unsigned mascara)
{
    gae1_canal *c = &gae1_c[off >> 3];
    unsigned short *r = &gae1_reg[off];
    if (gae1_traza) gae1_traza(off, dato, mascara);
    *r = (*r & ~mascara) | (dato & mascara);
    if ((off & 7) == 3) {                /* primer trozo: empieza, o se encola si ya suena */
        if (r[-1] && *r) {
            c->bucle = 1;
            if (!c->activo) { c->trozo = 0; c->activo = 1; suceso(off >> 3, 0); }
        } else if (c->activo) { c->activo = 0; suceso(off >> 3, -1); }
    } else if ((off & 7) == 7) {         /* segundo trozo */
        if (r[-1] && *r) {
            c->bucle = 1;
            if (!c->activo) { c->trozo = 1; c->activo = 1; suceso(off >> 3, 1); }
        } else c->bucle = 0;
    }
}
/* un canal avanza n muestras: el tipo 0x0C (estereo) gasta 2 por muestra (la ultima, 1); los demas, 1 */
static void avanza_canal(int ch, unsigned n)
{
    gae1_canal *c = &gae1_c[ch];
    while (n && c->activo) {
        int t = c->bucle ? c->trozo : 0;
        unsigned short *r = &gae1_reg[ch * 8 + t * 4];
        unsigned paso = ((r[1] >> 4) & 15) == 0x0C ? 2 : 1, k;
        if (!r[3]) { c->activo = 0; suceso(ch, -1); break; }   /* (no pasa: se mira al acabar cada trozo) */
        k = (r[3] + paso - 1) / paso;                           /* muestras de salida que le quedan al trozo */
        if (k > n) { r[3] -= n * paso; break; }
        n -= k; r[3] = 0;
        if (!c->bucle) { c->activo = 0; suceso(ch, -1); }
        else {
            c->trozo ^= 1;
            if (!gae1_reg[ch * 8 + c->trozo * 4 + 3]) { c->activo = 0; suceso(ch, -1); }
            else suceso(ch, c->trozo);
        }
    }
}
void gae1_avanza(unsigned n)
{
    int ch;
    for (ch = 0; ch < GAE1_CANALES; ch++) avanza_canal(ch, n);
}

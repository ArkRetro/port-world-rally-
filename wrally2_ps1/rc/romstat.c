/* ROMSTAT (banco: rc_host_rom, compilado con -DROMSTAT): bloques de 256 B de la ROM del 68000 leidos en cada fase.
   Fase = modo (FEC298), tramo (FEC3C6, FEC3C8) y si ya acabo la suma de la ROM del arranque (suceso en PC 0x2616),
   tomada al empezar cada cuadro. Fichero $ROMSTAT (o romstat.txt): una linea por bloque la primera vez que se lee en
   cada fase ("cuadro modo fec3c6 fec3c8 suma bloque", en hexadecimal salvo el cuadro) y al final una linea "F" por
   fase con su numero de bloques. Lo junta tools/romstat.py (residente y bancos, diseno 3.3) */
#include <stdio.h>
#include <stdlib.h>
#include "rc_rt.h"
#define NF 2048
typedef struct { u16 modo, t6, t8; int suma; u32 n, bits[4096 / 32]; } fase_t;
static fase_t fase[NF];
static int nf, cur = -1, llena;
static long fr;
static FILE *f;
extern int rc_suma_hecha;
static u16 W(u32 o) { return *(u16 *)(rc_ram + o); }
void rc_romstat_cuadro(long cuadro)
{
    u16 m = W(0xC298), a = W(0xC3C6), b = W(0xC3C8); int i;
    fr = cuadro;
    if (!f) { const char *n = getenv("ROMSTAT"); if (!(f = fopen(n ? n : "romstat.txt", "w"))) { perror("ROMSTAT"); exit(1); } }
    for (i = 0; i < nf; i++) if (fase[i].modo == m && fase[i].t6 == a && fase[i].t8 == b && fase[i].suma == rc_suma_hecha) break;
    if (i == nf) {
        if (nf == NF) { llena = 1; cur = -1; return; }
        fase[nf].modo = m; fase[nf].t6 = a; fase[nf].t8 = b; fase[nf].suma = rc_suma_hecha; nf++;
    }
    cur = i;
}
void rc_romrec(u32 a, u32 n)
{
    u32 b;
    if (cur < 0) { if (llena) return; rc_romstat_cuadro(fr); if (cur < 0) return; }   /* antes del primer cuadro */
    for (b = a >> 8; b <= (a + n - 1) >> 8 && b < 4096; b++) {
        fase_t *p = &fase[cur];
        if (p->bits[b >> 5] & (1u << (b & 31))) continue;
        p->bits[b >> 5] |= 1u << (b & 31); p->n++;
        fprintf(f, "%ld %x %x %x %d %03x\n", fr, p->modo, p->t6, p->t8, p->suma, b);
    }
}
void rc_romstat_fin(void)
{
    int i;
    if (!f) return;
    for (i = 0; i < nf; i++) fprintf(f, "F %x %x %x %d %u\n", fase[i].modo, fase[i].t6, fase[i].t8, fase[i].suma, fase[i].n);
    if (llena) fprintf(f, "F llena: mas de %d fases\n", NF);
    fclose(f);
}

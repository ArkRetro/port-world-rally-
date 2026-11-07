/* codificador PS-ADPCM (SPU de PlayStation): bloques de 28 muestras, busqueda exhaustiva de filtro y
   desplazamiento con el estado del decodificador. entrada: s16 LE crudo; salida: bloques de 16 bytes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const int F0[5] = {0, 60, 115, 98, 122}, F1[5] = {0, 0, -52, -55, -60};
static int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
int main(int argc, char **argv)
{
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    short *in; long n, nb, b; int s1 = 0, s2 = 0;
    fseek(fi, 0, SEEK_END); n = ftell(fi) / 2; fseek(fi, 0, SEEK_SET);
    nb = (n + 27) / 28;
    in = calloc(nb * 28, 2); fread(in, 2, n, fi);
    for (b = 0; b < nb; b++) {
        short *x = in + b * 28;
        int bf = 0, bs = 0; double be = 1e300; unsigned char best[16];
        int bo1 = 0, bo2 = 0, f, sh, i;
        for (f = 0; f < 5; f++) for (sh = 0; sh <= 12; sh++) {
            int o1 = s1, o2 = s2; double e = 0; unsigned char out[16];
            memset(out, 0, 16);
            for (i = 0; i < 28; i++) {
                int pred = (o1 * F0[f] + o2 * F1[f] + 32) >> 6;
                int r = x[i] - pred;
                int q = (r * (1 << sh) + (r >= 0 ? 2048 : -2048)) / 4096;   /* redondeo */
                int d;
                if (q > 7) q = 7; if (q < -8) q = -8;
                d = clamp16(((q << 12) >> sh) + pred);
                e += (double)(d - x[i]) * (d - x[i]);
                out[2 + i / 2] |= (q & 15) << ((i & 1) * 4);
                o2 = o1; o1 = d;
                if (e >= be) break;
            }
            if (e < be) { be = e; bf = f; bs = sh; memcpy(best, out, 16); bo1 = o1; bo2 = o2; }
        }
        best[0] = (unsigned char)(bs | (bf << 4));
        best[1] = (b == nb - 1) ? 1 : 0;        /* ultimo bloque: fin y silencio */
        s1 = bo1; s2 = bo2;
        fwrite(best, 1, 16, fo);
    }
    fclose(fo); return 0;
}

/* BLKLOG=fichero (banco, rc_host): registro por cuadro de lo que dibuja la pantalla izquierda, para construir los paquetes
   de tiles (diseno 10.2; lo lee tools/blklog.py). Al entrar en rc_vblank, con las reglas de video.md 4-5 y 4.3 del
   diseno: el plano 0 de la VRAM viva (24 x 16 posiciones desde el desplazamiento), los sprites de la copia del vblank
   anterior (vram_vb) con el banco de vregs[1] actual (entradas 0-511 activas con el bit 15 a 0, celdas en pantalla),
   y los codigos nuevos en todo el plano 64 x 32 (lo que el juego escribe antes de que se vea).
   Formato: palabras de 32 bits. Cabecera 0x80000000 | cuadro y 4 palabras: modo FEC298 | FEC146 << 16; FEC3C6 |
   FEC3C8 << 16; FEFD0A | FEFD08 << 16; FE8102 (noche) | n << 16. Luego n entradas: codigo (0-15), color (16-22; en el
   plano 6 bits), espejo X (23), espejo Y (24), clase (28-30: 0 plano visible, 1 celda de sprite, 2 celda de efecto,
   3 codigo nuevo en el plano), sin repetir dentro del cuadro. */
#include <stdio.h>
#include <stdlib.h>
#include "rc_rt.h"
#define W_ 368
#define H_ 240
static FILE *bf;
static u32 ent[4096]; static int ne;
static u8 visto[1 << 19];                    /* (codigo, color de 6 bits) ya escrito en el plano: 2^22 bits */
static void pon(u32 e)
{
    if (ne < 4096) ent[ne++] = e;
}
static int cmpu(const void *a, const void *b) { u32 x = *(const u32 *)a, y = *(const u32 *)b; return x < y ? -1 : x > y; }
static u16 W16(const u8 *p, u32 o) { return *(const u16 *)(p + o); }
void rc_blklog(long frame, const u8 *vram_vb)
{
    const u16 *vr = (const u16 *)rc_vregs, *v = (const u16 *)rc_vram, *vb = (const u16 *)vram_vb;
    const u16 *m = v + ((((vr[2] >> 9) & 7) * 0x2000) >> 1);
    u32 sx = (v[0x2802 >> 1] + 0x14 + 8) & 0x3FF, sy = (v[0x2800 >> 1] + 1) & 0x1FF, e, i, j, k, n, h[5];
    if (!bf) { const char *fn = getenv("BLKLOG"); if (!fn || !(bf = fopen(fn, "wb"))) { perror("BLKLOG"); exit(1); } }
    ne = 0;
    for (j = 0; j < 16; j++)
        for (i = 0; i < 24; i++) {
            u32 e = ((((sy >> 4) + j) & 31) * 64 + (((sx >> 4) + i) & 63)) * 2, w0 = m[e];
            pon(m[e + 1] | ((w0 >> 9) & 0x3F) << 16 | ((w0 >> 7) & 1) << 23 | ((w0 >> 6) & 1) << 24);
        }
    for (e = 0; e < 64 * 32 * 2; e += 2) {        /* todo el plano: codigo y color nuevos */
        u32 w0 = m[e], key = m[e + 1] | ((w0 >> 9) & 0x3F) << 16;
        if (!(visto[key >> 3] & (1 << (key & 7)))) { visto[key >> 3] |= 1 << (key & 7); pon(key | 3u << 28); }
    }
    for (k = (vr[3] & 0x10) * 0x80; k < ((vr[3] & 0x10) * 0x80) + 0x800; k += 4) {
        u32 d0 = vb[k], d1 = vb[k + 1], d2 = vb[k + 2], d3 = vb[k + 3], x, y;
        u32 xs = ((d2 >> 12) & 15) + 1, ys = ((d1 >> 12) & 15) + 1, fx = (d1 >> 11) & 1, fy = (d1 >> 10) & 1;
        if ((d0 & 0x8000) || !(d1 & 0x200)) continue;
        for (y = 0; y < ys; y++)
            for (x = 0; x < xs; x++) {
                u32 c = vb[((d3 >> 1) + y * xs + x) & 0x7FFF], col = (((d0 >> 9) & 0x7F) + (c >> 12)) & 0x7F;
                u32 ex = fx ? xs - 1 - x : x, ey = fy ? ys - 1 - y : y;
                int px = (int)(((d2 & 0x3FF) + ex * 16) & 0x3FF) - 127 - 8, py = (int)(((d1 & 0x1FF) + ey * 16) & 0x1FF) - 16;
                if (px <= -16 || px >= W_ || py <= -16 || py >= H_) continue;
                pon((((((d0 & 0x1FF) << 10) + (c & 0xFFF)) & 0xFFFF) | col << 16 | fx << 23 | fy << 24 | ((col & 0x3F) == 0x3F ? 2u : 1u) << 28));
            }
    }
    qsort(ent, ne, 4, cmpu);
    for (n = i = 0; i < (u32)ne; i++) if (!i || ent[i] != ent[i - 1]) ent[n++] = ent[i];
    h[0] = 0x80000000u | (u32)frame; h[1] = W16(rc_ram, 0xC298) | (u32)W16(rc_ram, 0xC146) << 16;
    h[2] = W16(rc_ram, 0xC3C6) | (u32)W16(rc_ram, 0xC3C8) << 16; h[3] = W16(rc_ram, 0xFD0A) | (u32)W16(rc_ram, 0xFD08) << 16;
    h[4] = W16(rc_ram, 0x8102) | n << 16;
    fwrite(h, 4, 5, bf); fwrite(ent, 4, n, bf);
}

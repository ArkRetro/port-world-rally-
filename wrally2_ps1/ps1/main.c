/* World Rally 2 -> PlayStation. Hito H2: el programa del 68000 recompilado (rc68.py) y la MCU (trabajo traducido y
   el resto en C) corriendo en la PS1, con la ROM entera dentro del EXE (configuracion R0) y la imagen en mosaico: cada
   tile del plano y de los sprites es un cuadrado del color de su paleta. Sirve para medir la CPU y la RAM en el
   hardware y ver que el juego avanza (attract, partida); sin sonido. Cifras arriba: modo del juego (FEC298), cuadro,
   lineas de video del 68000 y del dibujo por cuadro (263 = un cuadro entero), cuadros saltados, cuadros por segundo
   x1000 y KB libres de RAM. Uso personal. */
#include "../rc/rc_rt.h"
#include "../rc/mcs51.h"
#define REG(a) (*(volatile u32 *)(a))
#define GP0 REG(0x1F801810)
#define GP1 REG(0x1F801814)
#define I_STAT REG(0x1F801070)
#define I_MASK REG(0x1F801074)
#define D2_MADR REG(0x1F8010A0)
#define D2_BCR REG(0x1F8010A4)
#define D2_CHCR REG(0x1F8010A8)
#define DPCR REG(0x1F8010F0)
#define JOY_DATA (*(volatile u8 *)0x1F801040)
#define JOY_STAT (*(volatile u16 *)0x1F801044)
#define JOY_MODE (*(volatile u16 *)0x1F801048)
#define JOY_CTRL (*(volatile u16 *)0x1F80104A)
#define JOY_BAUD (*(volatile u16 *)0x1F80104E)

void rc_mem_init(void); void rc_set_io(u16 in0, u16 in1, u16 in2, u16 in3); void rc_snd_vblank(void);
void mcu_boot(u8 *m, const u8 *code, mcs51_t *c); void mcu_cuadro(u8 *m, mcs51_t *c);
extern mcs51_t rc_mcu; extern int mcu_modelo;
extern const u8 mcu_rom[0x8000];
extern char __bss_end[];
static inline u16 t1_get(void)
{
    volatile u16 *r = (volatile u16 *)0xBF801110; u16 a = *r, b;
    while ((b = *r) != a) a = b;
    return a;
}
#define T1 t1_get()

/* ------------------------------------------------------------ GPU */
#define W 368
#define H 240
static void gpu_wait(void) { while (!(GP1 & (1 << 26))); }
static void dma_wait(void) { while (D2_CHCR & (1 << 24)); }
/* lista de la GPU: nodos de hasta 16 palabras (el FIFO de la GPU; con nodos mas largos Mednafen pierde palabras y la
   GPU se queda a medio comando) seguidos en un bufer (lst), enlazados en orden y con primitivas enteras */
#define LSTN 24576
static u32 lst[LSTN];
static u32 *lp, *node;
static void l_begin(void) { node = lst; lp = lst + 1; }
static void l_close(u32 *next)
{
    u32 n = (u32)(lp - node - 1);
    node[0] = (n << 24) | ((u32)next & 0xFFFFFF);
}
static void l_room(int n)
{
    if (lp + n - node - 1 > 16 || lp + n + 2 >= lst + LSTN) {
        if (lp + n + 2 >= lst + LSTN) return;             /* lleno: se pierde el resto */
        l_close(lp); node = lp; lp = node + 1;
    }
}
static int l_full(int n) { return lp + n + 2 >= lst + LSTN; }
static void l_send(void)
{
    l_close((u32 *)0xFFFFFF);
    dma_wait(); gpu_wait(); D2_MADR = (u32)lst; D2_BCR = 0; D2_CHCR = 0x01000401;
}
static void rect16(int x, int y, u32 rgb)
{
    if (l_full(2)) return;
    l_room(2); lp[0] = 0x78000000 | rgb; lp[1] = ((u32)(y & 0xFFFF) << 16) | (x & 0xFFFF); lp += 2;
}
static void rect(int x, int y, int w, int h, u32 rgb)
{
    if (l_full(3)) return;
    l_room(3); lp[0] = 0x60000000 | rgb; lp[1] = ((u32)(y & 0xFFFF) << 16) | (x & 0xFFFF); lp[2] = ((u32)h << 16) | w; lp += 3;
}
/* color de la paleta del juego (xRRRRRGGGGGBBBBB) en 24 bits para la GPU (0xBBGGRR) */
static u32 rgb24(u32 i)
{
    u32 v = *(const u16 *)(rc_pal + ((i & 0xFFF) << 1));
    return ((v >> 10) & 31) << 3 | (((v >> 5) & 31) << 11) | ((v & 31) << 19);
}

/* ------------------------------------------------------------ mando (SIO0 directo) */
static void joy_init(void) { JOY_CTRL = 0x40; JOY_BAUD = 0x88; JOY_MODE = 0x0D; JOY_CTRL = 0; }
static u8 joy_x(u8 b, int ack)
{
    int t;
    for (t = 0; t < 2000 && !(JOY_STAT & 1); t++);
    JOY_DATA = b;
    for (t = 0; t < 2000 && !(JOY_STAT & 2); t++);
    b = JOY_DATA;
    if (ack) for (t = 0; t < 600 && !(JOY_STAT & 0x80); t++);
    JOY_CTRL |= 0x10;
    return b;
}
static u16 joy_read(void)
{
    u16 r = 0xFFFF; u8 id; int t;
    JOY_CTRL = 0x1003;
    for (t = 0; t < 300; t++) __asm__ volatile ("nop");
    joy_x(0x01, 1); id = joy_x(0x42, 1);
    if ((id & 0xF0) == 0x40 || (id & 0xF0) == 0x70 || (id & 0xF0) == 0x50) {
        joy_x(0, 1); r = joy_x(0, 1); r |= joy_x(0, 0) << 8;
    }
    JOY_CTRL = 0;
    return r;
}

/* ------------------------------------------------------------ cifras */
static const u16 font[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
static void digits(int x, int y, u32 v, int n)
{
    int i, k, d[8];
    for (i = 0; i < n; i++) { d[i] = v % 10; v /= 10; }
    rect(x - 2, y - 2, n * 8 + 2, 14, 0x000000);
    for (i = 0; i < n; i++)
        for (k = 0; k < 15; k++)
            if (font[d[n - 1 - i]] & (1 << (14 - k))) rect(x + i * 8 + (k % 3) * 2, y + (k / 3) * 2, 2, 2, 0xFFFFFF);
}

/* ------------------------------------------------------------ mosaico */
/* plano 0 (VRAM 0xC000 con vregs[0] = 0x0C90): 64 x 32 tiles de 16 x 16, dos palabras por tile (palabra 0: color en los
   bits 9-15, giros 6-7, bits altos del codigo 0-2; palabra 1: codigo). Se ven las columnas 8-375 de la placa */
static void plano(void)
{
    const u16 *vr = (const u16 *)rc_vregs, *v = (const u16 *)rc_vram;
    const u16 *m = v + ((((vr[2] >> 9) & 7) * 0x2000) >> 1);
    int sx = (v[0x2802 >> 1] + 0x14 + 8) & 0x3FF, sy = (v[0x2800 >> 1] + 1) & 0x1FF, i, j;
    int ox = sx & 15, oy = sy & 15;
    for (j = 0; j <= H / 16; j++)
        for (i = 0; i <= W / 16; i++) {
            int e = ((((sy >> 4) + j) & 31) * 64 + (((sx >> 4) + i) & 63)) * 2;
            u32 w0 = m[e], c = m[e + 1];
            rect16(i * 16 - ox, j * 16 - oy, rgb24(((w0 >> 9) & 0x7F) * 32 + 1 + (c % 15)));
        }
}
/* sprites del banco de vregs[1] bit 4 (0x0000 / 0x1000), entradas de 4 palabras; fuera las de la pantalla derecha
   (bit 15 de la palabra 0). Celdas de 16 x 16 en la lista de la palabra 3 */
static void sprites(void)
{
    const u16 *vr = (const u16 *)rc_vregs, *v = (const u16 *)rc_vram;
    u32 base = (vr[3] & 0x10) * 0x80, j;
    for (j = base; j < base + 0x800; j += 4) {
        u32 d0 = v[j], d1 = v[j + 1], d2 = v[j + 2], d3 = v[j + 3], x, y;
        u32 xs = ((d2 >> 12) & 15) + 1, ys = ((d1 >> 12) & 15) + 1, sx = d2 & 0x3FF, sy = d1 & 0x1FF;
        if ((d0 & 0x8000) || !(d1 & 0x200)) continue;
        for (y = 0; y < ys; y++)
            for (x = 0; x < xs; x++) {
                u32 c = v[((d3 >> 1) + y * xs + x) & 0x7FFF], col = ((d0 >> 9) & 0x7F) + ((c >> 12) & 15);
                int px = (int)((sx + x * 16) & 0x3FF) - 127 - 8, py = (int)((sy + y * 16) & 0x1FF) - 16;
                if (px <= -16 || px >= W || py <= -16 || py >= H || (col & 0x3F) == 0x3F) continue;
                rect16(px, py, rgb24(col * 32 + 1 + ((c & 0xFFF) % 15)));
            }
    }
}

/* ------------------------------------------------------------ cuadro */
static int fb, gear, sq_last, show = 1, skipped;
static u32 nframe, skips, lastv, l68, ldraw, l68_acc, ldraw_acc;
int fps_mil = 60000;
static void vsync(void) { I_STAT = ~1u; while (!(I_STAT & 1)); I_STAT = ~1u; }
static u16 RW(u32 a) { return *(const u16 *)(rc_ram + (a & 0xFFFE)); }
void rc_vblank(void)
{
    u16 pad, t0; int draw = 1;
    l68_acc += (u16)(T1 - lastv);
    dma_wait();
    if ((I_STAT & 1) && !skipped) draw = 0;     /* el vblank ya paso: este cuadro no se dibuja (nunca dos seguidos) */
    skipped = !draw; skips += !draw;
    if (draw) { vsync(); GP1 = 0x05000000 | ((fb ? 256 : 0) << 10); fb ^= 1; }
    else I_STAT = ~1u;
    GP1 = 0x03000000;
    t0 = T1;
    rc_snd_vblank();
    mcu_cuadro(rc_ram, &rc_mcu);
    if (draw) {
        l_begin();
        l_room(6);
        lp[0] = 0xE3000000 | ((fb ? 256u : 0u) << 10); lp[1] = 0xE4000000 | ((((fb ? 256u : 0u) + H - 1) << 10)) | (W - 1);
        lp[2] = 0xE5000000 | ((fb ? 256u : 0u) << 11); lp[3] = 0x02000000 | rgb24(0); lp[4] = (fb ? 256u : 0u) << 16; lp[5] = (H << 16) | W;
        lp += 6;
        plano();
        sprites();
        if (show) {
            digits(4, 4, RW(0xFEC298), 2); digits(28, 4, nframe, 6); digits(84, 4, l68, 3); digits(116, 4, ldraw, 3);
            digits(148, 4, skips, 4); digits(188, 4, fps_mil, 5); digits(236, 4, (0x801FFFF0u - 16384u - (u32)__bss_end) >> 10, 4);
        }
        l_send();
    }
    pad = joy_read();
    {   /* IN0: arriba 01, abajo 02, derecha 04, izquierda 08, acelerar 10, marcha 20 (activa a 1), START 80; IN2: moneda bit 8 */
        u16 in0 = 0xDDDF, in2 = 0xFFDF;
        if (!(pad & 0x0010)) in0 &= ~0x01;
        if (!(pad & 0x0040)) in0 &= ~0x02;
        if (!(pad & 0x0020)) in0 &= ~0x04;
        if (!(pad & 0x0080)) in0 &= ~0x08;
        if (!(pad & 0x4000)) in0 &= ~0x10;             /* X: acelerar */
        if (!(pad & 0x8000) && sq_last) gear ^= 1;      /* cuadrado: cambio de marcha */
        sq_last = (pad & 0x8000) != 0;
        if (gear) in0 |= 0x20;
        if (!(pad & 0x0008)) in0 &= ~0x80;             /* START */
        if (!(pad & 0x0001)) in2 &= ~0x100;            /* SELECT: moneda */
        if (!(pad & 0x0100) && !(pad & 0x0200)) show = 1;   /* L2 + R2: cifras */
        rc_set_io(in0, 0xFFFF, in2, 0xFFFF);
    }
    ldraw_acc += (u16)(T1 - t0);
    if ((++nframe & 31) == 0) { l68 = l68_acc >> 5; ldraw = ldraw_acc >> 5; l68_acc = ldraw_acc = 0; }
    lastv = T1;
}
static volatile u32 bad_pc;
void rc_bad(u32 pc)
{   /* salto fuera del codigo recompilado: pantalla roja con el PC (hay que anadir una semilla) */
    bad_pc = pc;
    for (;;) {
        dma_wait(); l_begin(); l_room(6);
        lp[0] = 0xE3000000; lp[1] = 0xE4000000 | ((H - 1) << 10) | (W - 1); lp[2] = 0xE5000000;
        lp[3] = 0x020000C0; lp[4] = 0; lp[5] = (H << 16) | W; lp += 6;
        digits(100, 100, pc, 8); l_send(); vsync(); GP1 = 0x05000000; GP1 = 0x03000000;
    }
}
void rc_divzero(u32 pc) { rc_bad(pc); }

/* cuadros de 263 lineas por segundo x1000, medidos contra el temporizador 2 (como el WR1) */
static void medir_fps(void)
{
    volatile u16 *t2 = (volatile u16 *)0xBF801120; u16 a, p, l0; u32 ck = 0, x = 4233600;
    int k = 0;
    *(volatile u16 *)0xBF801124 = 0x0200;
    l0 = t1_get(); while (t1_get() == l0 && ++k < 100000);
    l0 = t1_get(); p = *t2;
    while ((u16)(t1_get() - l0) < 263 && ++k < 2000000) { a = *t2; ck += (u16)(a - p); p = a; }
    if (ck > 62500 && ck < 77500) fps_mil = x / ck * 1000 + (x % ck) * 1000 / ck;
}
int main(void)
{
    __asm__ volatile ("mtc0 $0, $12");
    *(volatile u32 *)0xFFFE0130 = 0x0001E988;     /* cache de instrucciones activada (como la BIOS) */
    I_MASK = 0;
    DPCR |= 0x800;
    GP1 = 0x00000000;
    GP1 = 0x04000002;
    GP1 = 0x08000041;                             /* 368 x 240 NTSC */
    GP1 = 0x06C70260;
    GP1 = 0x07040010;
    joy_init();
    *(volatile u16 *)0x1F801114 = 0x0100;         /* temporizador 1: cuenta lineas */
    medir_fps();
    rc_mem_init();
    mcu_boot(rc_ram, mcu_rom, &rc_mcu);
    rc_r.a[7] = ((u32)*(u16 *)(rc_rom) << 16) | *(u16 *)(rc_rom + 2);
    rc_r.imask = 7;
    rc_r.cyrel = -(s32)(CYC_FRAME * 98 / 100);    /* el primer vblank como en MAME (rc_host FASE) */
    {   /* los dos bufer en azul y la pantalla encendida antes del primer cuadro del juego */
        int k;
        for (k = 0; k < 2; k++) {
            gpu_wait(); GP0 = 0x02200000; GP0 = (u32)(k * 256) << 16; GP0 = (H << 16) | W;
        }
        gpu_wait(); GP1 = 0x05000000; GP1 = 0x03000000;
    }
    lastv = T1;
    rc_run(((u32)*(u16 *)(rc_rom + 4) << 16) | *(u16 *)(rc_rom + 6));
    for (;;);
}

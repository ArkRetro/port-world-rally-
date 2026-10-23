/* Menu del disco con los dos World Rally (recopilatorio/): la pantalla de pantalla.py (bandera, ELIGE JUEGO, las dos
   portadas con su placa y PULSA START parpadeando); izquierda y derecha eligen (la portada elegida lleva un marco
   amarillo que late y la otra se oscurece), START, X o circulo cargan: fundido a negro, CARGANDO y LoadExec de la
   BIOS con el EXE del juego (el WR1 en la raiz y el WR2 en la carpeta WR2), con las interrupciones y la DMA como las
   dejo la BIOS al arrancar este EXE (inicio.s): asi el juego arranca igual que si lo hubiera lanzado la BIOS.
   Sin interrupciones: el vsync y el mando por sondeo (como en los ports). Uso personal; comentarios sin acentos. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define REG(a) (*(volatile u32 *)(a))
#define GP0 REG(0x1F801810)
#define GP1 REG(0x1F801814)
#define I_STAT REG(0x1F801070)
#define I_MASK REG(0x1F801074)
#define DPCR REG(0x1F8010F0)
#define D2_MADR REG(0x1F8010A0)
#define D2_BCR REG(0x1F8010A4)
#define D2_CHCR REG(0x1F8010A8)
#define JOY_DATA (*(volatile u8 *)0x1F801040)
#define JOY_STAT (*(volatile u16 *)0x1F801044)
#define JOY_MODE (*(volatile u16 *)0x1F801048)
#define JOY_CTRL (*(volatile u16 *)0x1F80104A)
#define JOY_BAUD (*(volatile u16 *)0x1F80104E)
#define B_START 0x0008
#define B_DER 0x0020
#define B_IZQ 0x0080
#define B_L1 0x0400
#define B_R1 0x0800
#define B_CIRC 0x2000
#define B_X 0x4000

extern const u16 pantalla[];               /* datos.s: pantalla.bin */
void bios_loadexec(const char *nombre, u32 pila, u32 desp);
void pon_sr(u32 sr);
static const char *const exe[2] = {"cdrom:\\WRPS1.EXE;1", "cdrom:\\WR2\\WR2PS1.EXE;1"};

/* VRAM: los dos bufer en (0, 0) y (0, 256); la pantalla guardada en (384, 0), PULSA START en (384, 256) y CARGANDO
   en (384, 288) */
#define GX 384
static int W, H, px[2], py[2], tw, th, rx, ry, rw, rh, cx, cy, cw, ch;

/* ------------------------------------------------------------ GPU */
static void gpu_wait(void) { while (!(GP1 & (1 << 26))); }
static void dma_wait(void) { while (D2_CHCR & (1 << 24)); }
static void upload(int x, int y, int w, int h, const void *d)
{
    int words = w * h / 2, bs = 16;
    dma_wait(); gpu_wait();
    GP0 = 0x01000000;
    GP0 = 0xA0000000; GP0 = ((u32)y << 16) | x; GP0 = ((u32)h << 16) | w;
    if (words % bs) bs = words < 16 ? words : 1;
    D2_MADR = (u32)d; D2_BCR = ((u32)(words / bs) << 16) | bs; D2_CHCR = 0x01000201;
    dma_wait();
}
static void copia(int sx, int sy, int dx, int dy, int w, int h)
{
    gpu_wait(); GP0 = 0x80000000; GP0 = ((u32)sy << 16) | sx; GP0 = ((u32)dy << 16) | dx; GP0 = ((u32)h << 16) | w;
}
static void rect(int x, int y, int w, int h, u32 c)        /* c: 0xBBGGRR */
{
    gpu_wait(); GP0 = 0x60000000 | c; GP0 = ((u32)y << 16) | x; GP0 = ((u32)h << 16) | w;
}
/* semitransparente: modo 0 (B/2 + F/2) oscurece a la mitad con negro; modo 2 (B - F) para el fundido */
static void semi(int x, int y, int w, int h, u32 c, int modo)
{
    gpu_wait(); GP0 = 0xE1000400 | (modo << 5);
    gpu_wait(); GP0 = 0x62000000 | c; GP0 = ((u32)y << 16) | x; GP0 = ((u32)h << 16) | w;
}
static void vsync(void) { I_STAT = ~1u; while (!(I_STAT & 1)); I_STAT = ~1u; }

/* ------------------------------------------------------------ mando (SIO0 directo, como en los ports) */
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
static u16 joy_read(void)                  /* botones pulsados a 1 */
{
    u16 r = 0xFFFF; u8 id; int t;
    JOY_CTRL = 0x1003;
    for (t = 0; t < 300; t++) __asm__ volatile ("nop");
    joy_x(0x01, 1); id = joy_x(0x42, 1);
    if ((id & 0xF0) == 0x40 || (id & 0xF0) == 0x70 || (id & 0xF0) == 0x50) {
        joy_x(0, 1); r = joy_x(0, 1); r |= joy_x(0, 0) << 8;
    }
    JOY_CTRL = 0;
    return (u16)~r;
}

/* ------------------------------------------------------------ pantalla */
static void dibuja(int b, int sel, int t)
{
    int y0 = b * 256, o = !sel, k = t & 31, v = 0xC0 + (k < 16 ? k : 31 - k) * 4;
    copia(GX, 0, 0, y0, W, H);
    if (t % 48 < 32) copia(GX, 256, rx, y0 + ry, rw, rh);
    semi(px[o] - 3, y0 + py[o] - 3, tw + 6, th + 6 + 24 + 6, 0, 0);     /* la otra portada y su placa, a la mitad */
    {   /* marco de la elegida: amarillo que late, 3 pixeles por fuera del marco rojo */
        u32 c = (u32)v << 8 | 0xFF;
        int x = px[sel] - 6, y = y0 + py[sel] - 6, w = tw + 12, h = th + 12;
        rect(x, y, w, 3, c); rect(x, y + h - 3, w, 3, c); rect(x, y, 3, h, c); rect(x + w - 3, y, 3, h, c);
    }
}
static void muestra(int b) { vsync(); gpu_wait(); GP1 = 0x05000000 | ((u32)b * 256 << 10); }

static void uno(u32 sr, u32 imask, u32 dpcr)
{
    const u16 *hd = pantalla;
    int fb = 0, sel = 0, t = 0, k;
    u16 antes, pad;
    char nom[32];
    *(volatile u32 *)0xFFFE0130 = 0x0001E988;     /* cache de instrucciones (como la BIOS) */
    W = hd[0]; H = hd[1]; px[0] = hd[2]; py[0] = hd[3]; tw = hd[4]; th = hd[5]; px[1] = hd[6]; py[1] = hd[7];
    rx = hd[10]; ry = hd[11]; rw = hd[12]; rh = hd[13]; cx = hd[14]; cy = hd[15]; cw = hd[16]; ch = hd[17];
    DPCR = dpcr | 0x800;
    GP1 = 0x00000000;
    GP1 = 0x04000002;
    GP1 = 0x08000041;                             /* 368 x 240 NTSC, como los dos juegos */
    GP1 = 0x06C70260;
    GP1 = 0x07040010;
    gpu_wait(); GP0 = 0xE3000000; GP0 = 0xE4000000 | (511 << 10) | 1023; GP0 = 0xE5000000; GP0 = 0xE6000000;
    joy_init();
    upload(GX, 0, W, H, hd + 24);
    upload(GX, 256, rw, rh, hd + 24 + W * H);
    upload(GX, 288, cw, ch, hd + 24 + W * H + rw * rh);
    dibuja(0, sel, 0); dibuja(1, sel, 0);
    muestra(0); GP1 = 0x03000000;
    antes = joy_read();                           /* lo que ya venga pulsado no cuenta hasta soltarlo */
    for (;;) {
        u16 nuevo;
        pad = joy_read(); nuevo = pad & ~antes; antes = pad;
        if (nuevo & (B_IZQ | B_L1)) sel = 0;
        if (nuevo & (B_DER | B_R1)) sel = 1;
        if (nuevo & (B_START | B_X | B_CIRC)) break;
        fb ^= 1; dibuja(fb, sel, ++t); muestra(fb);
    }
    for (k = 1; k <= 16; k++) {                   /* fundido a negro (medio segundo con el marco quieto) */
        fb ^= 1; dibuja(fb, sel, 0); semi(0, fb * 256, W, H, (u32)(k * 16 - 1) * 0x010101, 2); muestra(fb);
    }
    for (k = 0; k < 2; k++) { rect(0, k * 256, W, H, 0); copia(GX, 288, cx, k * 256 + cy, cw, ch); }
    vsync(); vsync();
    for (k = 0; (nom[k] = exe[sel][k]) != 0; k++);   /* el nombre en la pila: la carga pisa este EXE */
    dma_wait(); gpu_wait();
    DPCR = dpcr; I_STAT = ~1u; I_MASK = imask; pon_sr(sr);
    bios_loadexec(nom, 0x801FFFF0, 0);
}
void menu(u32 sr, u32 imask, u32 dpcr)
{
    for (;;) { uno(sr, imask, dpcr); pon_sr(0); I_MASK = 0; }    /* si LoadExec vuelve (no esta el EXE), otra vez */
}

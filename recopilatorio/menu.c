/* Menu del disco con los dos World Rally (recopilatorio/): la pantalla de pantalla.py (bandera, ELIGE JUEGO, las dos
   portadas con su placa y PULSA START parpadeando); izquierda y derecha eligen (la portada elegida lleva un marco
   amarillo que late y la otra se oscurece), START, X o circulo cargan: fundido a negro, CARGANDO y LoadExec de la
   BIOS con el EXE del juego (el WR1 en la raiz y el WR2 en la carpeta WR2), con las interrupciones y la DMA como las
   dejo la BIOS al arrancar este EXE (inicio.s): asi el juego arranca igual que si lo hubiera lanzado la BIOS.
   Musica (musica.py): la del juego elegido suena en bucle desde el principio cada vez que se cambia de portada (la
   otra se apaga en 0,1 s) y se funde con la pantalla al cargar.
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
extern const u32 musica[];                 /* datos.s: musica.bin (cabecera de 6 palabras y las dos piezas) */
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

/* ------------------------------------------------------------ SPU (como snd.c del WR1) */
#define R16(a) (*(volatile u16 *)((a) | 0xA0000000))
#define SPUCNT R16(0x1F801DAA)
#define SPUSTAT R16(0x1F801DAE)
#define VOZ(v, r) R16(0x1F801C00 + (v) * 16 + (r))
#define SPU0 0x1010                                /* antes, los bufer de captura */
static u32 m_dir[2], m_bucle[2], m_tono[2];
static int hay_musica;
static void spu_modo(u16 m) { int n; for (n = 0; n < 100000 && (SPUSTAT & 0x3F) != (m & 0x3F); n++); }
static void spu_sube(u32 dir, const void *d, u32 bytes)
{
    int n;
    SPUCNT = SPUCNT & ~0x30; spu_modo(SPUCNT);
    R16(0x1F801DA6) = dir >> 3;
    SPUCNT = (SPUCNT & ~0x30) | 0x20; spu_modo(SPUCNT);
    REG(0x1F8010C0) = (u32)d; REG(0x1F8010C4) = ((bytes / 64) << 16) | 16; REG(0x1F8010C8) = 0x01000201;
    for (n = 0; n < 10000000 && (REG(0x1F8010C8) & 0x01000000); n++);
    for (n = 0; n < 100000 && (SPUSTAT & 0x400); n++);
    SPUCNT = SPUCNT & ~0x30; spu_modo(SPUCNT);
}
/* teclas: entre dos escrituras al mismo registro, al menos un cuadro (aqui solo se escriben una vez por cuadro) */
static void spu_init(void)
{
    int v; const u32 *c = musica;
    SPUCNT = 0; spu_modo(0);
    R16(0x1F801D80) = 0x3FFF; R16(0x1F801D82) = 0x3FFF;   /* volumen general */
    R16(0x1F801D84) = 0; R16(0x1F801D86) = 0;             /* reverberacion */
    R16(0x1F801D8C) = 0xFFFF; R16(0x1F801D8E) = 0x00FF;   /* apagar voces */
    R16(0x1F801D90) = 0; R16(0x1F801D92) = 0; R16(0x1F801D94) = 0; R16(0x1F801D96) = 0;
    R16(0x1F801D98) = 0; R16(0x1F801D9A) = 0;
    R16(0x1F801DB0) = 0; R16(0x1F801DB2) = 0; R16(0x1F801DB4) = 0; R16(0x1F801DB6) = 0;
    R16(0x1F801DAC) = 0x0004;
    SPUCNT = 0xC000;                                       /* SPU sin silencio, sin audio del CD */
    for (v = 0; v < 24; v++) { VOZ(v, 0) = 0; VOZ(v, 2) = 0; }
    hay_musica = c[0] || c[3];                    /* sin la del WR2 (sin BIOS no se graba), solo la del WR1 */
    if (!hay_musica) return;
    m_dir[0] = SPU0; m_dir[1] = SPU0 + c[0];
    m_bucle[0] = c[1]; m_bucle[1] = c[4]; m_tono[0] = c[2]; m_tono[1] = c[5];
    if (c[0]) spu_sube(m_dir[0], c + 6, c[0]);
    if (c[3]) spu_sube(m_dir[1], (const u8 *)(c + 6) + c[0], c[3]);
    for (v = 0; v < 2; v++) {
        VOZ(v, 0) = 0x3FFF; VOZ(v, 2) = 0x3FFF; VOZ(v, 4) = m_tono[v]; VOZ(v, 6) = m_dir[v] >> 3;
        VOZ(v, 8) = 0x00FF; VOZ(v, 10) = 0x000B;          /* ataque inmediato, sostenido al maximo, salida en 0,1 s */
    }
}
static void musica_de(int j)                  /* la de j (voz j) desde el principio; la otra se apaga */
{
    if (!hay_musica) return;
    if (!musica[j * 3]) { R16(0x1F801D8C) = 3; return; }  /* de esa no hay: las dos calladas */
    VOZ(j, 6) = m_dir[j] >> 3; VOZ(j, 14) = (m_dir[j] + m_bucle[j]) >> 3;
    R16(0x1F801D8C) = 1 << !j; R16(0x1F801D88) = 1 << j;
}

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
    if ((id & 0xF0) == 0x40 || (id & 0xF0) == 0x70 || (id & 0xF0) == 0x50 || id == 0xE3 || id == 0x23) {   /* JogCon, NeGcon: START y cruceta igual */
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
    DPCR = dpcr | 0x800 | 0x80000;               /* DMA 2 (GPU) y 4 (SPU) */
    GP1 = 0x00000000;
    GP1 = 0x04000002;
    GP1 = 0x08000041;                             /* 368 x 240 NTSC, como los dos juegos */
    GP1 = 0x06C70260;
    GP1 = 0x07040010;
    gpu_wait(); GP0 = 0xE3000000; GP0 = 0xE4000000 | (511 << 10) | 1023; GP0 = 0xE5000000; GP0 = 0xE6000000;
    joy_init();
    spu_init();
    upload(GX, 0, W, H, hd + 24);
    upload(GX, 256, rw, rh, hd + 24 + W * H);
    upload(GX, 288, cw, ch, hd + 24 + W * H + rw * rh);
    dibuja(0, sel, 0); dibuja(1, sel, 0);
    muestra(0); GP1 = 0x03000000;
    musica_de(sel);
    antes = joy_read();                           /* lo que ya venga pulsado no cuenta hasta soltarlo */
    for (;;) {
        u16 nuevo;
        pad = joy_read(); nuevo = pad & ~antes; antes = pad;
        k = sel;
        if (nuevo & (B_IZQ | B_L1)) sel = 0;
        if (nuevo & (B_DER | B_R1)) sel = 1;
        if (sel != k) musica_de(sel);
        if (nuevo & (B_START | B_X | B_CIRC)) break;
        fb ^= 1; dibuja(fb, sel, ++t); muestra(fb);
    }
    for (k = 1; k <= 16; k++) {                   /* fundido a negro (medio segundo con el marco quieto) */
        fb ^= 1; dibuja(fb, sel, 0); semi(0, fb * 256, W, H, (u32)(k * 16 - 1) * 0x010101, 2); muestra(fb);
        R16(0x1F801D80) = 0x3FFF * (16 - k) / 16; R16(0x1F801D82) = 0x3FFF * (16 - k) / 16;   /* musica con la pantalla */
    }
    R16(0x1F801D8C) = 0x0003; SPUCNT = 0;         /* voces apagadas y SPU parada: el juego la prepara a su manera */
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

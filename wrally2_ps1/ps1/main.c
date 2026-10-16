/* World Rally 2 -> PlayStation. Hito H3: el programa del 68000 recompilado (rc68.py) y la MCU (trabajo traducido y
   el resto en C) corriendo en la PS1 con la ROM entera dentro del EXE (configuracion R0), sin sonido. Imagen: en los
   menus la de verdad con tiles A de 8 bpp (video.c) traidos del CD (carga.c: MENUS.PAK por pantalla y emergencias de
   TILESA.BIN); en el mapa de la demo y la demo (FEC298 9 y 5) con tiles B de 4 bpp y CLUT por hueco (DEMOr.PAK,
   prelectura en el titulo, emergencias de TILES.BIN); en carrera (1) sigue el mosaico de H2 (cada tile un cuadrado
   del color de su paleta). Cifras arriba: modo del juego (FEC298), cuadro, lineas de video del 68000 y del dibujo por
   cuadro (263 = un cuadro entero), cuadros saltados, cuadros por segundo x1000, KB libres de RAM, faltas de tiles en
   el cuadro, sectores leidos del CD y lineas del dibujo de video.c; en B, otra fila: huecos fijos y rotativos
   ocupados, subidas de la L2, emergencias, lineas del ultimo negro antes del mapa y de la demo. Uso personal. */
#include "../rc/rc_rt.h"
#include "../rc/mcs51.h"
#include "video.h"
#include "carga.h"
#include "snd.h"
#include "../rc/gae1.h"
#define REG(a) (*(volatile u32 *)(a))
#define GP0 REG(0x1F801810)
#define GP1 REG(0x1F801814)
#define I_STAT REG(0x1F801070)
#define I_MASK REG(0x1F801074)
#define DPCR REG(0x1F8010F0)
#define D2_CHCR REG(0x1F8010A8)
#define JOY_DATA (*(volatile u8 *)0x1F801040)
#define JOY_STAT (*(volatile u16 *)0x1F801044)
#define JOY_MODE (*(volatile u16 *)0x1F801048)
#define JOY_CTRL (*(volatile u16 *)0x1F80104A)
#define JOY_BAUD (*(volatile u16 *)0x1F80104E)

void rc_mem_init(void); void rc_set_io(u16 in0, u16 in1, u16 in2, u16 in3); void rc_snd_vblank(void);
void mcu_boot(u8 *m, const u8 *code, mcs51_t *c); void mcu_cuadro(u8 *m, mcs51_t *c);
extern mcs51_t rc_mcu; extern int mcu_modelo;
extern const u8 mcu_tab[];
void reservas(void);
extern char __bss_end[];
static inline u16 t1_get(void)
{
    volatile u16 *r = (volatile u16 *)0xBF801110; u16 a = *r, b;
    while ((b = *r) != a) a = b;
    return a;
}
#define T1 t1_get()

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

/* ------------------------------------------------------------ mosaico */
/* plano 0 (VRAM 0xC000 con vregs[0] = 0x0C90): 64 x 32 tiles de 16 x 16, dos palabras por tile (palabra 0: color en los
   bits 9-15, giros 6-7, bits altos del codigo 0-2; palabra 1: codigo). Se ven las columnas 8-375 de la placa */
static void plano(void)
{
    const u16 *vr = (const u16 *)rc_vregs, *v = (const u16 *)rc_vram;
    const u16 *m = v + ((((vr[2] >> 9) & 7) * 0x2000) >> 1);
    int sx = (v[0x2802 >> 1] + 0x14 + 8) & 0x3FF, sy = (v[0x2800 >> 1] + 1 + 16) & 0x1FF, i, j;   /* +16: lineas 16-255 de MAME (video.c) */
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
static int fb, gear, sq_last, show, lr_last, skipped, start_up;
static u16 snd_tv;                              /* ultima lectura del temporizador 1 del reloj del sonido */   /* show: cifras de depuracion (L2 + R2) */
static u32 nframe, skips, lastv, l68, ldraw, l68_acc, ldraw_acc, lvid, lvid_acc;
extern int cd_dbg[8];                           /* cd.c del WR1: [2] errores del controlador */
static u32 modo_ant, f146_ant, modo_ped;
int fps_mil = 60000;
/* espera del vsync sondeando el CD (la GPU esta parada: lo que llega se sube) */
static void vsync(void) { I_STAT = ~1u; while (!(I_STAT & 1)) carga_atiende(); I_STAT = ~1u; }
static u16 RW(u32 a) { return *(const u16 *)(rc_ram + (a & 0xFFFE)); }
/* pantallas: en el primer cuadro negro de una transicion (FEC298 = 0) se carga la seccion de la pantalla pedida
   (FEC29C, el ultimo valor no nulo) con el FEC146 de ahora; si la clave no esta (FEC146 cambia al entrar), en el
   primer cuadro del modo nuevo (la pantalla aun no se ha visto: sale un poco mas tarde). Tambien al cambiar FEC146
   dentro de un modo (logo -> fotos -> titulo) si su seccion no esta ya. El juego esta congelado mientras tanto */
/* Demo: en el titulo (3/16) empieza la prelectura de la demo siguiente (FEC3C6 + 1: FEC3C6 cambia al entrar en el
   mapa); los negros antes del mapa y de la demo pasan al reparto B y cargan lo que falte; al volver a un menu, A */
/* lo que va a sonar en la pantalla que viene, a la SPU en el negro de antes (no se ve; si no, lo pide el trozo y se
   carga con el juego parado): mapa de la demo, logo, elegir coche (S7C), elegir rally (S7E y el banco del coche,
   FE8126: 0 -> SPUCAR1, 1 -> SPUCAR0, 2 -> SPUCAR2) y la carrera */
static void precarga_spu(u32 ped)
{
    static const u8 coche[3] = {9, 8, 10};
    u16 c = RW(0xFE8126);
    if (ped != 9 && ped != 3 && ped != 8 && ped != 0xA && ped != 1) return;
    carga_para();
    if (ped == 9) snd_precarga(2 + ((RW(0xFEC3C6) + 1) & 3));
    else if (ped == 3) snd_precarga(1);
    else if (ped == 8) snd_precarga(6);
    else { if (ped == 0xA) snd_precarga(7); if (c < 3) snd_precarga(coche[c]); }
}
static void pantalla(u32 modo)
{
    u32 f146 = RW(0xFEC146), ped = RW(0xFEC29C);
    if (ped) modo_ped = ped;
    if (modo == 0 && modo_ant != 0) {
        /* las voces siguen sonando durante las cargas bloqueantes: el modelo del GAE1 va con el reloj (tiempo real) */
        u16 t0 = T1;
        if (modo_ped == 0xB) ;                  /* mapa del tramo: la clave cambia al entrar en el (carga_demo_modo) */
        else if (modo_ped == 9 || modo_ped == 5 || modo_ped == 1) carga_demo_negro(modo_ped, fb ^ 1);
        else { carga_demo_fin(); carga_pantalla(modo_ped << 16 | f146, fb ^ 1, 0); }
        precarga_spu(modo_ped);
        if (modo_ped == 1) carga_relleno(fb ^ 1, (u16)(T1 - t0));   /* antes de cada tramo: CARGANDO al menos 3 s
                                                   (la L2, la ROM del tramo y lo pendiente, de fondo mientras tanto); en
                                                   los demas negros solo si una carga pasa de 0,5 s, como antes */
    } else if (modo == 9 || modo == 5 || modo == 0xB || modo == 1) {
        carga_demo_modo(modo, fb ^ 1);          /* recarga si no hubo negro (bloquea, con las voces quietas) */
    }
    else if (modo != 0 && modo != 1 && (modo != modo_ant || f146 != f146_ant)) {
        int tit = modo == 3 && f146 == 0x16;
        if (!tit) carga_demo_fin();
        carga_pantalla(modo << 16 | f146, fb ^ 1, (modo == modo_ant) | 2);   /* a la vista: parcial si es el mismo modo */
        if (tit) carga_demo_titulo((RW(0xFEC3C6) + 1) & 3);
    }
    modo_ant = modo; f146_ant = f146;
}
/* ---- pausa: START mientras se corre (como el WR1). El 68000 y la MCU se quedan parados aqui dentro; el cuadro que
   se ve se oscurece con PAUSA encima; las voces a tono 0; el CD acaba lo que estaba leyendo (y el fondo sigue llegando:
   la pausa tambien sirve de bufer) pero no se dibuja nada. El tiempo de la pausa no cuenta para el reloj del sonido.
   Sale con otra pulsacion de START */
/* se corre: modo carrera (la demo es el 5) y FEFD1E = 1, que el juego pone con el GO y quita en la meta y al acabarse el
   tiempo. En el Continue? (aun modo 1) START es del juego: antes se lo quedaba la pausa y no se podia continuar */
static int corriendo(void) { return RW(0xFEC298) == 1 && RW(0xFEFD1E) == 1; }
static void pausa(void)
{
    int up = 0;
    snd_pausa(1);
    while (D2_CHCR & (1 << 24)) carga_poll();   /* la lista de este cuadro acabada antes de tocar la GPU */
    vid_pausa(fb ^ 1);                          /* el bufer que se ve (el otro se mostraria al volver) */
    for (;;) {
        u16 p;
        while (!(I_STAT & 1)) carga_poll();
        I_STAT = ~1u;
        p = joy_read();
        if (p & 0x0008) up = 1; else if (up) break;
    }
    snd_pausa(0);
    snd_lin_pausa = 0; snd_tv = T1;             /* la pausa no cuenta (el temporizador da la vuelta en 4,2 s) */
    start_up = 0;                               /* START sigue pulsado: no vuelve a pausar hasta soltarlo */
}
void rc_vblank(void)
{
    u16 pad, t0; int draw = 1, menu, b; u32 modo;
    l68_acc += (u16)(T1 - lastv);
    while (D2_CHCR & (1 << 24)) carga_poll();   /* la lista anterior aun en la GPU: sondear mientras (hasta 10 ms) */
    carga_atiende();
    if ((I_STAT & 1) && !skipped) draw = 0;     /* el vblank ya paso: este cuadro no se dibuja (nunca dos seguidos) */
    skipped = !draw; skips += !draw;
    if (draw) { vsync(); GP1 = 0x05000000 | ((fb ? 256 : 0) << 10); fb ^= 1; }
    else I_STAT = ~1u;
    GP1 = 0x03000000;
    t0 = T1;
#ifndef SND_JUEGO
    {   /* el modelo del GAE1 a tiempo real, como el chip de la placa: muestras de 7 812,5 Hz por lineas del temporizador 1
           (15 734,27 por segundo: 5 000 / 10 070) sin las pausas de las cargas; la SPU toca a 0x2D6, el mismo reloj.
           -DSND_JUEGO: por cuadros del juego y tono escalado por fps (huella igual al banco para G-ESTADO/G-SNDLOG) */
        static u32 acu; static int ini;
        u16 t = T1; u32 d = (u16)(t - snd_tv); snd_tv = t;
        d = d > snd_lin_pausa ? d - snd_lin_pausa : 0; snd_lin_pausa = 0;
        if (!ini) { ini = 1; d = 263; rc_snd_real_on = 1; }
        if (d > 263 * 30) d = 263;              /* algo raro (no deberia pasar): un cuadro */
        acu += d * 5000u; rc_snd_real += acu / 10070u; acu %= 10070u;
    }
#endif
    rc_snd_vblank();
    carga_poll();                               /* el CD entre paso y paso del vblank (hueco <= ~3 ms) */
    snd_modo_tit = RW(0xFEC298) == 3;
    if (snd_pide) { carga_para(); snd_pausa(1); snd_atiende(); snd_pausa(0); }
    snd_vblank();
    carga_poll();
    mcu_cuadro(rc_ram, &rc_mcu);
    carga_poll();
    modo = RW(0xFEC298); menu = modo != 1;
    pantalla(modo);
    carga_poll();
#ifdef R1
    if (rc_suma_hecha) carga_rom_suma();
#endif
    b = vid_lay() == 2;                         /* mapa de la demo y demo (y sus negros): formato B */
    if (draw) {
        if (menu || b) vid_paletas();
        carga_poll();
        l_begin();
        vid_marco(fb);
        if (b || menu) { if (b) vid_b(); else vid_menu(); lvid_acc += vid_lineas; carga_poll(); }
        else { plano(); sprites(); }            /* carrera sin su paquete: mosaico */
        if (show) {
            digits(4, 4, modo, 2); digits(28, 4, nframe, 6); digits(84, 4, l68, 3); digits(116, 4, ldraw, 3);
            digits(148, 4, skips, 4); digits(188, 4, fps_mil, 5); digits(236, 4, (0x801FFFF0u - 16384u - (u32)__bss_end) >> 10, 4);
            digits(272, 4, vid_faltas_cuadro, 3); digits(300, 4, carga_sectores, 5); digits(344, 4, lvid, 3);
            digits(4, 36, snd_ck_mal, 4); digits(44, 36, snd_ck_perdidos, 4); digits(84, 36, snd_spu_mal, 4);   /* SPU.BIN: mal leidos, perdidos; SPU mal */
            digits(124, 36, (u32)cd_dbg[2], 4); digits(164, 36, carga_err, 4);                                 /* errores del CD y de los paquetes */
            if (b) {
                digits(4, 20, vid_b_fijos, 4); digits(44, 20, vid_b_rot, 4); digits(84, 20, vid_b_l2, 5); digits(132, 20, carga_emerg, 5);
                digits(180, 20, carga_dm_neg[0], 4); digits(220, 20, carga_dm_neg[1], 4);
            }
        }
        carga_atiende();
        l_send();
    }
    vid_spr_copia();                            /* S(N): los sprites que se dibujan en el cuadro siguiente */
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
        if (!(pad & 0x0008) && !corriendo()) in0 &= ~0x80;   /* START (mientras se corre es la pausa) */
        if (!(pad & 0x0001)) in2 &= ~0x100;            /* SELECT: moneda */
        {   /* L2 + R2: cifras de depuracion si/no */
            int lr = !(pad & 0x0100) && !(pad & 0x0200);
            if (lr && !lr_last) show ^= 1, vid_cifras = show;
            lr_last = lr;
        }
        rc_set_io(in0, 0xFFFF, in2, 0xFFFF);
    }
    if (!(pad & 0x0008) && start_up && corriendo()) {   /* solo mientras se corre (no en el Continue? ni en la meta) */
        pausa(); pad = joy_read();
        sq_last = (pad & 0x8000) != 0;          /* lo que siga pulsado al volver no cambia nada */
    }
    start_up = (pad & 0x0008) != 0;
    carga_poll();
    ldraw_acc += (u16)(T1 - t0);
    if ((++nframe & 31) == 0) { l68 = l68_acc >> 5; ldraw = ldraw_acc >> 5; lvid = lvid_acc >> 5; l68_acc = ldraw_acc = lvid_acc = 0; }
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
        digits(100, 100, pc, 8); l_send(); I_STAT = ~1u; while (!(I_STAT & 1)); GP1 = 0x05000000; GP1 = 0x03000000;
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
    mcu_boot(rc_ram, mcu_tab, &rc_mcu);            /* MCU_TABLAS: el trabajo solo lee las tablas de MOVC */
    reservas();
    rc_r.imask = 7;
    rc_r.cyrel = -(s32)(CYC_FRAME * 98 / 100);    /* el primer vblank como en MAME (rc_host FASE) */
    {   /* los dos bufer en azul y la pantalla encendida antes del primer cuadro del juego */
        int k;
        for (k = 0; k < 2; k++) {
            gpu_wait(); GP0 = 0x02200000; GP0 = (u32)(k * 256) << 16; GP0 = (H << 16) | W;
        }
        gpu_wait(); GP1 = 0x05000000; GP1 = 0x03000000;
    }
    vid_init();                                   /* paletas y CLUT de efecto en S0, huecos vacios */
    carga_init();                                 /* la unidad, el directorio y el indice de MENUS.PAK */
#ifdef SND_JUEGO
    snd_juego = 1;
#endif
    snd_init(); gae1_suceso = snd_suceso;         /* SPUFIJO.BIN; la zona variable la piden los trozos */
#ifdef R1
    if (carga_rom_arranque()) rc_bad(0xB00000);   /* ROM.BIN entera al area (la suma del arranque la lee toda) */
#endif
    rc_r.a[7] = (rc_rd16(0) << 16) | rc_rd16(2);  /* vectores del 68000 */
#ifndef SIN_GANCHO
    rc_sondeo_gancho = carga_poll; rc_sondeo_per = 16384; rc_sondeo_t = 0;   /* el CD se sondea tambien mientras corre el 68000 */
#endif
    lastv = T1;
    rc_run((rc_rd16(4) << 16) | rc_rd16(6));
    for (;;);
}

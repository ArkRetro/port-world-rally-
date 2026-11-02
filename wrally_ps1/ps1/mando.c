/* Mando del puerto 1 por SIO0 (sondeo, sin interrupciones), comun a los dos ports: pad digital o analogico y el JogCon
   de Namco. El JogCon (ID E3h, el modo de la luz encendida) da los botones como el pad, la posicion de la rueda (16 bits
   con signo, absoluta) y un estado; su motor se manda con un byte de cada sondeo, el que se le asigna con el comando
   4Dh en el modo de configuracion (como pscontroller-rs, probado con el mando: 43h 01 entrar, 4Dh 00 FF FF FF FF FF el
   motor en el primer byte, 43h 00 salir). Orden del motor: nibble alto 0 parar, 1 derecha, 2 izquierda, 3 sujetar en la
   posicion de sujecion, C esa posicion = la actual; nibble bajo la fuerza (0-15). Al verlo se configura, se toma la
   posicion de ese momento como el centro y el motor la sujeta (un muelle, MANDO_FUERZA): el giro (mando_giro, -256 a
   256, izquierda negativa) es la distancia al centro, con todo el giro a vuelta[mando_sens] unidades (no se sabe
   cuantas da una vuelta de la rueda: se elige con L1 y R1, 7 niveles). Si se pulsa MODE (luz apagada) el JogCon pasa a
   ser un pad digital y se lee como tal. Uso personal. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef short s16;
#define JOY_DATA (*(volatile u8 *)0x1F801040)
#define JOY_STAT (*(volatile u16 *)0x1F801044)
#define JOY_MODE (*(volatile u16 *)0x1F801048)
#define JOY_CTRL (*(volatile u16 *)0x1F80104A)
#define JOY_BAUD (*(volatile u16 *)0x1F80104E)
#define MANDO_FUERZA 8                  /* fuerza del muelle (0-15) */
/* unidades de la rueda para todo el giro, por nivel de sensibilidad (L1 menos, R1 mas); se empieza en el del medio */
static const u16 vuelta[7] = {192, 144, 112, 88, 68, 52, 40};
int mando_jog;                          /* 1: hay un JogCon en modo JogCon */
int mando_giro;                         /* -256 (izquierda del todo) a 256 (derecha del todo) */
int mando_sens = 3;                     /* nivel de vuelta[] */
int mando_aviso;                        /* cuadros que quedan para ensenar el nivel tras cambiarlo (el port lo pinta) */
static int conf, centro, l1_ant = 1, r1_ant = 1;
static u8 motor;                        /* orden para el proximo sondeo */

void mando_init(void) { JOY_CTRL = 0x40; JOY_BAUD = 0x88; JOY_MODE = 0x0D; JOY_CTRL = 0; }
/* un byte; tras el ultimo el mando no da ACK y no hay que esperarlo */
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
static void sel(void) { int t; JOY_CTRL = 0x1003; for (t = 0; t < 300; t++) __asm__ volatile ("nop"); }
/* un comando entero: los n bytes de b tras el 01; devuelve el ID */
static u8 comando(const u8 *b, int n)
{
    u8 id; int i;
    sel(); joy_x(0x01, 1); id = joy_x(b[0], 1);
    for (i = 1; i < n; i++) joy_x(b[i], i < n - 1);
    JOY_CTRL = 0;
    return id;
}
static void configura(void)
{
    static const u8 entra[8] = {0x43, 0x00, 0x01}, mapa[8] = {0x4D, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                    sale[8] = {0x43, 0x00, 0x00};
    comando(entra, 8); comando(mapa, 8); comando(sale, 8);
    motor = 0xC0;                       /* el centro: donde este la rueda ahora; despues, el muelle */
    conf = 1;
}
/* los botones (activos a 0, como el pad digital); con un JogCon tambien mando_giro */
u16 mando_lee(void)
{
    u16 r = 0xFFFF; u8 id;
    sel();
    joy_x(0x01, 1); id = joy_x(0x42, 1);
    if (id == 0xE3) {                   /* JogCon: 5A, botones (2), posicion (2), estado, y uno mas */
        s16 pos; int g, v;
        joy_x(0, 1);
        r = joy_x(conf ? motor : 0, 1); r |= joy_x(0, 1) << 8;
        pos = (s16)joy_x(0, 1); pos = (s16)((u16)pos | joy_x(0, 1) << 8);
        joy_x(0, 1); joy_x(0, 0);
        JOY_CTRL = 0;
        if (!conf) { configura(); centro = pos; mando_giro = 0; mando_jog = 1; return r; }
        if (motor == 0xC0) { centro = pos; motor = 0x30 | MANDO_FUERZA; }
        if (!(r & 0x0400) && l1_ant && mando_sens > 0) mando_sens--, mando_aviso = 90;   /* L1: hay que girar mas */
        if (!(r & 0x0800) && r1_ant && mando_sens < 6) mando_sens++, mando_aviso = 90;   /* R1: hay que girar menos */
        l1_ant = (r & 0x0400) != 0; r1_ant = (r & 0x0800) != 0;
        r |= 0x0C00;
        v = vuelta[mando_sens];
        g = (pos - centro) * 256 / v;
        mando_giro = g < -256 ? -256 : g > 256 ? 256 : g;
        mando_jog = 1;
        return r;
    }
    if ((id & 0xF0) == 0x40 || (id & 0xF0) == 0x70 || (id & 0xF0) == 0x50) {
        joy_x(0, 1); r = joy_x(0, 1); r |= joy_x(0, 0) << 8;
    }
    JOY_CTRL = 0;
    mando_jog = 0; conf = 0;            /* otro mando (o el JogCon en modo digital): se vuelve a configurar al verlo */
    return r;
}

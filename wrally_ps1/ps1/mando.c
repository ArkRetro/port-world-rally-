/* Mando del puerto 1 por SIO0 (sondeo, sin interrupciones), comun a los dos ports: pad digital o analogico y el JogCon
   de Namco. El JogCon (ID E3h, el modo de la luz encendida) da los botones como el pad, la posicion de la rueda (16 bits
   con signo, absoluta) y un estado; su motor se manda con un byte de cada sondeo, el que se le asigna con el comando
   4Dh en el modo de configuracion (como pscontroller-rs, probado con el mando: 43h 01 entrar, 4Dh 00 FF FF FF FF FF el
   motor en el primer byte, 43h 00 salir). Orden del motor: nibble alto 0 parar, 1 derecha, 2 izquierda, 3 sujetar en la
   posicion de sujecion, C esa posicion = la actual; nibble bajo la fuerza (0-15). Al verlo se configura, se toma la
   posicion de ese momento como el centro y el motor la sujeta (un muelle, MANDO_FUERZA): el giro (mando_giro, -256 a
   256, izquierda negativa) es la distancia al centro, con todo el giro a mando_vuelta unidades. No se sabe cuantas
   da un cuarto de vuelta (el giro completo que se quiere: la marca de arriba a las 3 o a las 9), asi que se mide:
   con L1+R1 pulsados se gira hasta ahi y al soltar, lo que mas se haya separado del centro es el giro completo (el
   port ensena el numero un rato, mando_aviso). Si se pulsa MODE (luz apagada) el JogCon pasa a
   ser un pad digital y se lee como tal; al conectarlo (o al arrancar) se le pide el modo JogCon (pide_jogcon), porque el
   mando real arranca en modo digital. Uso personal. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef short s16;
#define JOY_DATA (*(volatile u8 *)0x1F801040)
#define JOY_STAT (*(volatile u16 *)0x1F801044)
#define JOY_MODE (*(volatile u16 *)0x1F801048)
#define JOY_CTRL (*(volatile u16 *)0x1F80104A)
#define JOY_BAUD (*(volatile u16 *)0x1F80104E)
#define MANDO_FUERZA 8                  /* fuerza del muelle (0-15) */
#define VUELTA_DEF 88                   /* unidades de la rueda para todo el giro hasta medirlo (un cuarto de vuelta?) */
int mando_jog;                          /* 1: hay un JogCon en modo JogCon */
int mando_giro;                         /* -256 (izquierda del todo) a 256 (derecha del todo) */
int mando_vuelta = VUELTA_DEF;          /* unidades de la rueda para todo el giro (L1+R1: se mide) */
int mando_aviso;                        /* cuadros que quedan para ensenar mando_cifra (el port lo pinta) */
int mando_cifra;                        /* lo que se ensena: lo medido hasta ahora mientras se mide, luego mando_vuelta */
static int conf, centro, midiendo, maximo;
static int probado;                     /* ya se le mando pasar a modo JogCon (una vez por conexion): 1, y 2 si al hacerlo
                                           resulto ser un DualShock, que se devuelve a digital */
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
static void pausa(void) { int t; for (t = 0; t < 12000; t++) __asm__ volatile ("nop"); }   /* ~1 ms entre comandos */
/* un pad en modo digital (41h) puede ser un JogCon: el suyo arranca asi y es el juego el que lo pasa a modo JogCon (la
   luz de MODE se enciende), como Ridge Racer Type 4: 43h 01 entrar en configuracion, 44h 01 (modo JogCon, sin bloquear
   el boton MODE) y 43h 00 salir. Un pad digital sin configuracion no hace caso; un DualShock pasaria a analogico (73h),
   y se le devuelve a digital al verlo (44h 00) */
static void pide_jogcon(int modo)
{
    static const u8 entra[8] = {0x43, 0x00, 0x01}, sale[8] = {0x43, 0x00, 0x00};
    u8 m[8] = {0x44, 0x00, 0x00, 0x02};
    m[2] = (u8)modo;
    pausa(); comando(entra, 8); pausa(); comando(m, 8); pausa(); comando(sale, 8); pausa();
}
static void configura(void)
{
    static const u8 entra[8] = {0x43, 0x00, 0x01}, mapa[8] = {0x4D, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                    sale[8] = {0x43, 0x00, 0x00};
    pausa(); comando(entra, 8); pausa(); comando(mapa, 8); pausa(); comando(sale, 8); pausa();
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
        if (!(r & 0x0400) && !(r & 0x0800)) {          /* L1+R1: midiendo el giro completo */
            int d = pos - centro; if (d < 0) d = -d;
            if (!midiendo) midiendo = 1, maximo = 0;
            if (d > maximo) maximo = d;
            mando_cifra = maximo; mando_aviso = 120;
        } else if (midiendo) {
            midiendo = 0;
            if (maximo >= 8) mando_vuelta = maximo;      /* (si casi no se giro, se deja como estaba) */
            mando_cifra = mando_vuelta; mando_aviso = 180;
        }
        r |= 0x0C00;
        v = mando_vuelta;
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
    if (id == 0x41 && !probado) { probado = 1; pide_jogcon(1); }          /* puede ser un JogCon en modo digital */
    else if ((id & 0xF0) == 0x70 && probado == 1) { probado = 2; pide_jogcon(0); }   /* era un DualShock: a digital */
    else if (id == 0xFF || id == 0x00) probado = 0;                       /* sin mando: al conectar otro, se prueba */
    return r;
}

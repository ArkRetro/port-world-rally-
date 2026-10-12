/* Lector de CD-ROM por sondeo (sin interrupciones de la CPU ni BIOS): comandos directos al controlador y una
   maquina de estados que avanza cd_poll(). En las cargas se llama en bucle; en la partida varias veces por cuadro
   (tambien mientras se espera el vblank) y nunca espera. Los sectores se piden de 0x924 bytes para ver la
   cabecera (MSF) de cada uno: si se pierde alguno se vuelve a buscar. El audio CD-DA va a velocidad normal y con
   pausa automatica al final de la pista (INT4); los datos a doble velocidad solo en las cargas grandes (cambiar
   de velocidad tarda casi un segundo). Al acabar una lectura la unidad sigue leyendo un momento (sin pausa) por si
   llega otra que empiece un poco mas adelante.
   Para el hardware real (y PSIO/XStation, que usan o imitan el controlador de Sony): registros por KSEG1 (la cola de
   escritura de la CPU no adelanta una lectura a la escritura del indice), HINTSTS leido dos veces (en placas antiguas
   las banderas cambian sin sincronizar: 1 antes de 3, 4 antes de 5), Pause rechazada mientras busca (INT5 80h)
   repetida, el audio cuenta desde que suena (bit 7 del stat, no desde la respuesta de Play), espera tras un error
   antes de reintentar y errores contados seguidos. */
#include "cd.h"
typedef unsigned char u8; typedef unsigned short u16;
#define CD0 (*(volatile u8 *)0xBF801800)
#define CD1 (*(volatile u8 *)0xBF801801)
#define CD2 (*(volatile u8 *)0xBF801802)
#define CD3 (*(volatile u8 *)0xBF801803)
#define T1 t1_get()                         /* temporizador 1: lineas de video (263 por cuadro) */
#define D3_MADR (*(volatile unsigned *)0xBF8010B0)   /* DMA 3: CD -> RAM */
#define D3_BCR (*(volatile unsigned *)0xBF8010B4)
#define D3_CHCR (*(volatile unsigned *)0xBF8010B8)
#define DPCR (*(volatile unsigned *)0xBF8010F0)
#define M_AUDIO 0x22                        /* velocidad normal, sectores de 0x924, pausa al final de la pista */
#define M_FAST 0xA2                         /* lo mismo a doble velocidad */
#define SEG(s) ((s) * 15750)                /* lineas por segundo */
#define GRACE 526                           /* lineas (2 cuadros) que se sigue leyendo al acabar una lectura */

int cd_dbg[8];                              /* 0 sectores, 1 perdidos, 2 errores, 3 vigilancia, 4 modo, 5 ultimo codigo de error,
                                               6 Pause rechazadas */
static u8 resp[16]; static int nresp;
static int drv;                             /* unidad: 0 parada, 1 leyendo, 2 audio */
static int cmd, cmd_arg, wait2;             /* comando sin primera respuesta (INT3); Pause sin segunda (INT2) */
static u16 t_cmd, t_ev, t_done;             /* lineas: envio del comando, ultimo sector, fin de la ultima lectura */
static int mode = -1, want_mode = M_AUDIO, loc_at = -1, drv_next = -1, lost;
static int seek_to = -1, seek_n;            /* ReadN sin Pause: hasta ver este sector (o 3) los que lleguen son de antes */
static int heard; static u16 t_play;        /* Play: ya suena (bit 7 del stat; antes busca) y cuando se acepto */
static int hold; static u16 t_hold, n_hold; /* tras una Pause rechazada o un error: no mandar nada en n_hold lineas */
/* peticion de lectura de los sectores lba..lba+n-1: a buf (como mucho 32, en cualquier orden: rd_got), o en orden
   a cb (por sbuf) o a donde diga dst (0: aun no hay sitio, se volvera a buscar). rd_i: el primero que falta */
static int rd_on, rd_lba, rd_n, rd_i, rd_fast, rd_err, rd_res; static unsigned rd_got;
static u8 *rd_buf; static void (*rd_cb)(int i, const unsigned char *d); static u8 *(*rd_dst)(int i);
static u8 sbuf[2048] __attribute__((aligned(4))); static int sbuf_dir = -1;   /* sbuf tiene el directorio */
/* los 2 sectores que llegan justo al acabar (o cortar) una lectura, mientras la unidad sigue un momento: si la
   siguiente empieza por ellos (el paquete de fondo que va detras en el disco: SNDFX, RACE y MENU2 al arrancar; POST
   y POST2 en la meta), se dan de aqui y la unidad sigue sin buscar. Antes, a 1x, cuando se pedia la siguiente (un
   cuadro despues) la unidad ya habia pasado su primer sector y habia que volver a buscar (0,15-0,3 s) */
static u8 spill[2][2048] __attribute__((aligned(4))); static int spill_lba[2] = {-1, -1}, spill_next = -1, spill_k;
/* audio */
static int play_at = -1, stop_req, loc_req, loc_res = -1, play_last, play_try;
int cd_ended;                               /* fin de la pista (INT4) o fallo al tocarla */

static u8 bcd(int v) { return (u8)(((v / 10) << 4) | (v % 10)); }
static int bcd2(u8 v) { return (v >> 4) * 10 + (v & 15); }
static int msf(const u8 *p) { return (bcd2(p[0]) * 60 + bcd2(p[1])) * 75 + bcd2(p[2]) - 150; }
static void send(int c, const u8 *p, int n, int arg)
{
    int i;
    CD0 = 1; CD3 = 0x40;                    /* vaciar la cola de parametros */
    CD0 = 0;
    for (i = 0; i < n; i++) CD2 = p[i];
    CD0 = 0; CD1 = c;
    cmd = c; cmd_arg = arg; t_cmd = T1;
}
static void setloc(int lba)
{
    u8 p[3]; int a = lba + 150;
    p[0] = bcd(a / 4500); p[1] = bcd((a / 75) % 60); p[2] = bcd(a % 75);
    send(0x02, p, 3, lba);
}
static void setmode(int m) { u8 p = m; send(0x0E, &p, 1, m); }
/* primer sector que falta desde l; -1 si la unidad ya se ha pasado */
static int need_from(int l)
{
    int i = l - rd_lba;
    if (!rd_buf) return i <= rd_i ? rd_lba + rd_i : -1;
    for (i = i < rd_i ? rd_i : i; i < rd_n; i++) if (!(rd_got & (1u << i))) return rd_lba + i;
    return -1;
}
/* INT1: lee el sector (antes de reconocerlo). Devuelve el destino si hace falta (*idx: su indice) */
static u8 *sector(int *idx)
{
    u8 h[12], *d = 0, *w; int k, i, l;
    CD0 = 0; CD3 = 0x80;                    /* pedir los datos del sector */
    for (k = 0; k < 1000 && !(CD0 & 0x40); k++);
    for (k = 0; k < 12; k++) h[k] = CD2;    /* cabecera y subcabecera */
    l = msf(h); i = l - rd_lba;
    if (rd_on && i >= 0 && i < rd_n) {
        if (rd_buf) { if (!(rd_got & (1u << i))) d = rd_buf + i * 2048; }
        else if (i == rd_i) d = rd_dst ? rd_dst(i) : sbuf;
    }
    w = d;
    if (!rd_on && l == spill_next && spill_k < 2) { w = spill[spill_k]; spill_lba[spill_k++] = l; spill_next++; }
#ifdef CD_DMA
    /* (-DCD_DMA, sin probar en MiSTer) los 2048 bytes de datos por DMA (la cabecera ya se leyo con la CPU: la DMA sigue por el mismo FIFO). Con la CPU,
       byte a byte, son ~23000 ciclos por sector (psxprof); con la DMA, 512 palabras a ~24-28 ciclos */
    if (w) { D3_MADR = (unsigned)w; D3_BCR = 0x00010200; D3_CHCR = 0x11000000; while (D3_CHCR & 0x01000000); }
#else
    if (w) for (k = 0; k < 2048; k += 4) { w[k] = CD2; w[k + 1] = CD2; w[k + 2] = CD2; w[k + 3] = CD2; }
#endif
    CD0 = 0; CD3 = 0;                       /* soltar el resto */
    cd_dbg[0]++;
    if (rd_on && !d && i > rd_i && !rd_buf) cd_dbg[1]++;
    if (seek_to >= 0 && (l == seek_to || ++seek_n > 2)) seek_to = -1;
    if (seek_to < 0 && i >= -8 && i <= rd_n + 8) drv_next = l + 1;   /* los de antes de buscar no cuentan */
    t_ev = T1; *idx = i;
    return d;
}
/* sector i de la lectura en d: anotarlo (y si es el ultimo, la lectura acaba: lo que llegue detras va a spill) */
static void got(int i, const u8 *d)
{
    rd_err = 0;                             /* errores seguidos: uno bueno los borra */
    if (rd_buf) { rd_got |= 1u << i; while (rd_i < rd_n && (rd_got & (1u << rd_i))) rd_i++; } else rd_i++;
    if (rd_i == rd_n) { rd_on = 0; rd_res = 1; t_done = T1; spill_next = rd_lba + rd_n; spill_k = 0; }
    if (rd_cb) rd_cb(i, d);
}
static void event(int t)
{
    if (t == 3) {                           /* primera respuesta */
        switch (cmd) {
        case 0x02: loc_at = cmd_arg; break;
        case 0x06: drv = 1; drv_next = seek_to = loc_at; seek_n = 0; loc_at = -1; t_ev = T1; lost = 0; break;
        case 0x03: drv = 2; heard = 0; t_play = T1; loc_at = -1; break;
        case 0x09: case 0x0A: wait2 = cmd; break;
        case 0x0E:                          /* cambio de velocidad: 3 cuadros antes de lo siguiente (como Sony) */
            if ((mode ^ cmd_arg) & 0x80) { hold = 1; t_hold = T1; n_hold = 3 * 263; }
            mode = cmd_arg; break;
        case 0x11: loc_res = nresp >= 8 ? msf(resp + 5) : -1; loc_req = 0; break;
        }
        /* GetStat tras Play: ya suena (bit 7), o con el motor en marcha no dice ni que busca (bit 6) ni error de
           busqueda (bit 2): un ODE que no diera el bit 7 */
        if (cmd == 0x01 && drv == 2 && !heard && nresp && (resp[0] & 0x80 || (resp[0] & 0x46) == 0x02)) heard = 1;
        cmd = 0;
    } else if (t == 2) {                    /* segunda respuesta (Pause, Init) */
        if (wait2) { drv = 0; wait2 = 0; }
    } else if (t == 4) {                    /* fin de pista (pausa automatica) o de disco */
        if (drv == 2) cd_ended = 1;
        drv = 0;
    } else if (t == 5) {                    /* error */
        int pc = cmd, ec = nresp > 2 && (resp[1] & 1) ? resp[2] : nresp > 1 ? resp[1] : 0x100;   /* codigo (tras uno o dos stat) */
        if (cmd == 0x01) cmd = 0;           /* GetStat no falla: es de la unidad (p. ej. la busqueda de Play); el GetStat sigue */
        cd_dbg[2]++; cd_dbg[5] = ec;
        if (cmd == 0x09) {                  /* Pause rechazada (aun busca: Play o ReadN): repetirla dentro de 2 cuadros */
            cd_dbg[6]++; hold = 1; t_hold = T1; n_hold = 526;
            if (drv == 2 && play_at < 0) stop_req = 1;
            cmd = 0; return;
        }
        if (rd_on && (cmd == 0x02 || cmd == 0x06 || (!cmd && drv == 1))) { hold = 1; t_hold = T1; n_hold = ec == 0x80 ? 15 * 263 : 526; }   /* aun no lista: 0,25 s; si no, 2 cuadros */
        if (cmd == 0x11) { loc_res = -1; loc_req = 0; }
        if (cmd == 0x03 && ec == 0x80 && !stop_req && ++play_try < 8) {   /* Play: aun no lista, otra vez */
            play_at = play_last; hold = 1; t_hold = T1; n_hold = 15 * 263;
        } else if (cmd == 0x03 || (cmd == 0x02 && play_at >= 0) || (!cmd && drv == 2)) { play_at = -1; cd_ended = 1; }
        if ((cmd == 0x02 || cmd == 0x06 || (!cmd && drv == 1)) && ec != 0x80) rd_err++;   /* 80h: aun no lista */
        if (cmd == 0x02) loc_at = -1;
        if (cmd == 0x06 || cmd == 0x03 || !cmd) drv = 0;
        if (wait2 && !cmd) { wait2 = 0; drv = 0; }
        cmd = pc == 0x01 ? pc : 0;
    }
    if (drv != 1) seek_to = -1;
}
/* siguiente comando segun lo que se quiere */
static void step(void)
{
    if (cmd || wait2 || (CD0 & 0x80)) return;
    if (hold) { if ((u16)(T1 - t_hold) < n_hold) return; hold = 0; }
    if (loc_req) { if (drv == 2) { send(0x11, 0, 0, 0); return; } loc_req = 0; loc_res = -1; }
    if (play_at >= 0) {
        if (drv == 1) { send(0x09, 0, 0, 0); return; }          /* la lectura sigue luego */
        if (mode != M_AUDIO) { setmode(M_AUDIO); return; }
        if (loc_at != play_at) { setloc(play_at); return; }
        send(0x03, 0, 0, 0); play_last = play_at; play_at = -1; cd_ended = 0; return;
    }
    if (stop_req) { stop_req = 0; if (drv) { send(0x09, 0, 0, 0); return; } }
    /* Play: GetStat cada cuadro hasta que suene (como mucho 3 s: la busqueda no cuenta como sonando) */
    if (drv == 2 && !heard) {
        if ((u16)(T1 - t_play) > SEG(3)) heard = 1;
        else if ((u16)(T1 - t_cmd) >= 263) { send(0x01, 0, 0, 0); return; }
    }
    if (rd_on) {
        int at = rd_lba + rd_i, m = rd_fast ? M_FAST : want_mode;
        if (drv == 2) return;                                   /* el audio tiene prioridad */
        if (rd_err > 6) { rd_on = 0; rd_res = -1; return; }
        if (mode != m) { if (drv) send(0x09, 0, 0, 0); else setmode(m); return; }
        if (drv == 1) {
            int nx = drv_next >= 0 ? need_from(drv_next) : -1;
            if (!lost && nx >= 0 && nx - drv_next <= 8) return;   /* lo que falta aun llega */
            lost = 0;                                           /* si no, volver a buscar (sin parar: SetLoc y ReadN) */
        }
        if (loc_at != at) { setloc(at); return; }
        send(0x06, 0, 0, 0); return;
    }
    if (drv == 1) { if (rd_res == 0 || rd_res == -1 || (u16)(T1 - t_done) >= GRACE) send(0x09, 0, 0, 0); return; }
    if (!drv && mode != want_mode) setmode(want_mode);
}
void cd_poll(void)
{
    int k, t;
    for (k = 0; k < 8; k++) {
        u8 *d = 0; int i = 0;
        CD0 = 1; t = CD3 & 7;
        if (!t) break;
        while (t != (CD3 & 7)) t = CD3 & 7;   /* dos lecturas iguales (psx-spx: 0 -> 1 -> 3, 0 -> 4 -> 5) */
        nresp = 0;
        while ((CD0 & 0x20) && nresp < 16) resp[nresp++] = CD1;
        if (t == 1 && drv == 1) d = sector(&i);
        CD0 = 1; CD3 = 7;                   /* reconocer */
        if (t != 1) event(t);
        else if (d) got(i, d);
    }
    /* vigilancia: respuestas que no llegan */
    if ((cmd && (u16)(T1 - t_cmd) > SEG(2)) || (wait2 && (u16)(T1 - t_cmd) > (wait2 == 0x0A ? SEG(4) : SEG(2)))) {
        cd_dbg[3]++; cmd = 0; wait2 = 0; loc_at = -1; seek_to = -1;
        if (drv != 2) { drv = 1; lost = 1; }   /* estado incierto: parar, o si se lee, volver a buscar ya (no esperar
                                               sectores de una unidad que quiza esta parada) */
        if (rd_on) rd_err++;
    }
    if (rd_on && drv == 1 && (u16)(T1 - t_ev) > SEG(3)) { cd_dbg[3]++; rd_err++; lost = 1; t_ev = T1; seek_to = -1; }
    step();
}
int cd_busy(void) { return cmd || wait2 || rd_on || play_at >= 0 || stop_req || loc_req; }
static int pending(void) { return cd_busy() || drv == 1 || (!drv && mode != want_mode); }
/* ---- lectura */
static void rd_start(int lba, int n, void *buf, void (*cb)(int, const unsigned char *), u8 *(*dst)(int), int fast)
{
    int k, j;
    if (!buf && !dst) sbuf_dir = -1;
    rd_lba = lba; rd_n = n; rd_i = 0; rd_buf = buf; rd_cb = cb; rd_dst = dst; rd_fast = fast; rd_err = 0; rd_res = 0; rd_got = 0;
    rd_on = n > 0; if (!rd_on) rd_res = 1;
    lost = 0;
    for (k = 0; k < 2 && rd_on; k++)        /* lo que trae spill, sin leerlo otra vez */
        if (spill_lba[k] >= 0 && spill_lba[k] == rd_lba + rd_i) {
            u8 *d = rd_buf ? rd_buf + rd_i * 2048 : rd_dst ? rd_dst(rd_i) : spill[k];
            if (!d) break;
            if (d != spill[k]) for (j = 0; j < 512; j++) ((unsigned *)d)[j] = ((const unsigned *)spill[k])[j];
            got(rd_i, d);
        }
}
int cd_read_async(int lba, int n, void *buf)
{
    if (rd_on || n > 32) return -1;
    rd_start(lba, n, buf, 0, 0, 0);
    return 0;
}
/* en orden y sin esperar: el sector i va a dst(i); cd_read_pos() dice cuantos han llegado */
int cd_stream_async(int lba, int n, u8 *(*dst)(int i))
{
    if (rd_on) return -1;
    rd_start(lba, n, 0, 0, dst, 0);
    return 0;
}
int cd_read_pos(void) { return rd_i; }
int cd_read_state(void) { return rd_on ? 0 : rd_res; }
/* cancelada (-2): como al acabar, la unidad sigue leyendo un momento por si se pide otra cosa enseguida */
void cd_cancel(void) { if (rd_on) { t_done = T1; spill_next = drv_next; spill_k = 0; } rd_on = 0; rd_res = -2; }
static int wait_rd(void)
{
    u16 t0 = T1; int s = 0;
    while (rd_on) {
        cd_poll();
        if ((u16)(T1 - t0) > SEG(1)) { t0 = T1; if (++s > 20) { rd_on = 0; rd_res = -1; } }
    }
    return rd_res > 0 ? 0 : -1;
}
/* lee n sectores seguidos (sin pausas ni busquedas entre ellos): a buf (de 32 en 32), o sector a sector a cb.
   Para el audio */
int cd_read(int lba, int n, void *buf, int fast)
{
    int k, r = 0;
    cd_cancel(); if (cd_audio()) cd_stop();
    for (k = 0; k < n && !r; k += 32) { rd_start(lba + k, n - k < 32 ? n - k : 32, (u8 *)buf + k * 2048, 0, 0, fast); r = wait_rd(); }
    return r;
}
int cd_stream(int lba, int n, void (*cb)(int i, const unsigned char *d), int fast) { cd_cancel(); if (cd_audio()) cd_stop(); rd_start(lba, n, 0, cb, 0, fast); return wait_rd(); }
/* esperar a que no quede nada pendiente (y la unidad parada si no hay audio) */
void cd_sync(void)
{
    u16 t0 = T1; int s = 0;
    do { cd_poll(); if ((u16)(T1 - t0) > SEG(1)) { t0 = T1; s++; } } while (pending() && s < 8);
}
/* velocidad para cuando no haya trabajo (normal: lista para el audio) */
void cd_speed(int fast) { want_mode = fast ? M_FAST : M_AUDIO; }

/* lo que haya dejado la BIOS o el cargador del ODE (p. ej. la segunda respuesta de su ultima Pause): reconocerlo
   hasta que pasen estas lineas sin nada y el controlador no este ocupado (como mucho 1 s) */
static void cd_drain(int lines)
{
    u16 t0 = T1, tq = t0;
    while ((u16)(T1 - tq) < lines || (CD0 & 0x80)) {
        CD0 = 1;
        if (CD3 & 7) { while (CD0 & 0x20) (void)CD1; CD0 = 1; CD3 = 0x5F; tq = T1; }
        if ((u16)(T1 - t0) > SEG(1)) break;
    }
}
int cd_init(void)
{
    int k;
    *(volatile unsigned *)0xBF801018 = 0x00020943;   /* tiempos del bus del CD y COM_DELAY como los deja la BIOS (por si */
    *(volatile unsigned *)0xBF801020 = 0x0000132C;   /* un cargador de ODE los cambio) */
    CD0 = 0; CD3 = 0;                       /* HCHPCTL: sin peticiones de datos */
    CD0 = 1; CD2 = 0x1F;                    /* habilitar banderas de interrupcion del controlador */
    CD0 = 1; CD3 = 0x1F;
    DPCR |= 0x8000;                         /* DMA 3 (CD) */
    cd_drain(2400);                         /* 0,15 s sin nada (la segunda respuesta de una Pause a 1x: hasta 65 ms) */
#ifndef CD_SIN_INIT
    for (k = 0; k < 3 && mode != M_FAST; k++) {   /* Init: para la unidad, motor en marcha; si no contesta, otra vez */
        cmd = wait2 = 0; drv = 1; want_mode = M_FAST; mode = -1;
        send(0x0A, 0, 0, 0);
        cd_sync();
        if (mode != M_FAST) cd_drain(2400);
    }
#else
    cmd = wait2 = 0; drv = 1; want_mode = M_FAST;
    /* (-DCD_SIN_INIT, sin probar en MiSTer) sin Init: la BIOS acaba de leer el EXE (a doble velocidad) y deja la unidad lista; Init la pondria a velocidad
       normal y buscaria el sector 0 (en DuckStation y MiSTer, mas de un segundo) para volver luego a doble. Basta
       Pause (por si seguia leyendo) y Setmode, que no cambia la velocidad si ya era doble. Si la unidad contesta con
       error o sin motor, Init como antes */
    send(0x09, 0, 0, 0);
    cd_sync();
    send(0x01, 0, 0, 0); cd_sync();
    if ((resp[0] & 0x03) != 0x02) { drv = 1; mode = -1; send(0x0A, 0, 0, 0); cd_sync(); }
#endif
    for (k = 0; k < 8; k++) {               /* GetStat: borra 'tapa abierta' */
        send(0x01, 0, 0, 0); cd_sync();
        if (!(resp[0] & 0x10)) break;
    }
    send(0x0C, 0, 0, 0); cd_sync();         /* Demute */
    cd_dbg[4] = mode;
    return mode == M_FAST ? 0 : -1;
}
/* ---- directorio raiz ISO9660: se guarda su primer sector en sbuf mientras no se lea otra cosa (al arrancar) */
static int dir_n = -1, dir_lba;
static unsigned int le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
/* busca NOMBRE (sin ";1") en el directorio raiz; devuelve lba y tamano */
int cd_find(const char *name, int *lba, int *size)
{
    int off, s;
    for (s = 0; dir_n < 0 || s < dir_n; s++) {
        const u8 *sec = sbuf;
        if (dir_n < 0 || s || sbuf_dir != dir_lba) {
            if (dir_n < 0) {
                if (cd_read(16, 1, sbuf, 1)) return -1;
                dir_lba = le32(sbuf + 156 + 2); dir_n = (le32(sbuf + 156 + 10) + 2047) / 2048;
            }
            if (cd_read(dir_lba + s, 1, sbuf, 1)) { dir_n = -1; return -1; }
            sbuf_dir = s ? -1 : dir_lba;
        }
        for (off = 0; off < 2048 && sec[off]; off += sec[off]) {
            int nl = sec[off + 32], i;
            const char *nm = (const char *)sec + off + 33;
            for (i = 0; name[i] && i < nl && nm[i] == name[i]; i++);
            if (!name[i] && (i == nl || nm[i] == ';')) { *lba = le32(sec + off + 2); *size = le32(sec + off + 10); return 0; }
        }
    }
    return -1;
}

/* ---- audio CD-DA */
/* volumen de la salida de audio del CD (0x80 = 100 %) */
void cd_volume(int v)
{
    CD0 = 2; CD2 = v; CD3 = 0;
    CD0 = 3; CD1 = v; CD2 = 0; CD3 = 0x20;
}
void cd_play(int lba) { play_at = lba; stop_req = 0; cd_ended = 0; heard = 0; play_try = 0; }
void cd_stop(void) { play_at = -1; stop_req = 1; }
int cd_audio(void) { return drv == 2 || play_at >= 0 || cmd == 0x03; }
int cd_playing(void) { return drv == 2 && heard; }
/* posicion del audio sin esperar: cd_loc_start() pide GetlocP y cd_loc_get() da -2 mientras no llega la
   respuesta, luego la posicion (-1 si no suena o no contesta) */
void cd_loc_start(void) { loc_res = -1; loc_req = drv == 2 && heard; }
int cd_loc_get(void) { return loc_req ? -2 : loc_res; }
/* posicion del audio (sincrona, GetlocP); -1 si no suena */
int cd_where(void)
{
    u16 t0 = T1;
    if (drv != 2 || !heard) return -1;      /* aun busca: GetlocP no (y la frase aun no ha empezado) */
    loc_req = 1; loc_res = -1;
    while (loc_req && (u16)(T1 - t0) < SEG(1)) cd_poll();
    loc_req = 0;
    return loc_res;
}

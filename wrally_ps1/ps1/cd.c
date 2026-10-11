/* Lector de CD-ROM por sondeo (sin interrupciones de la CPU ni BIOS): comandos directos al controlador y una
   maquina de estados que avanza cd_poll(). En las cargas se llama en bucle; en la partida varias veces por cuadro
   (tambien mientras se espera el vblank) y nunca espera. Los sectores se piden de 0x924 bytes para ver la
   cabecera (MSF) de cada uno: si se pierde alguno se vuelve a buscar. El audio CD-DA va a velocidad normal y con
   pausa automatica al final de la pista (INT4); los datos a doble velocidad solo en las cargas grandes (cambiar
   de velocidad tarda casi un segundo). Al acabar una lectura la unidad sigue leyendo un momento (sin pausa) por si
   llega otra que empiece un poco mas adelante. */
#include "cd.h"
typedef unsigned char u8; typedef unsigned short u16;
#define CD0 (*(volatile u8 *)0x1F801800)
#define CD1 (*(volatile u8 *)0x1F801801)
#define CD2 (*(volatile u8 *)0x1F801802)
#define CD3 (*(volatile u8 *)0x1F801803)
#define T1 (*(volatile u16 *)0x1F801110)     /* temporizador 1: lineas de video (263 por cuadro) */
#define M_AUDIO 0x22                        /* velocidad normal, sectores de 0x924, pausa al final de la pista */
#define M_FAST 0xA2                         /* lo mismo a doble velocidad */
#define SEG(s) ((s) * 15750)                /* lineas por segundo */
#define GRACE 526                           /* lineas (2 cuadros) que se sigue leyendo al acabar una lectura */

int cd_dbg[8];                              /* 0 sectores, 1 perdidos, 2 errores, 3 vigilancia */
static u8 resp[16]; static int nresp;
static int drv;                             /* unidad: 0 parada, 1 leyendo, 2 audio */
static int cmd, cmd_arg, wait2;             /* comando sin primera respuesta (INT3); Pause sin segunda (INT2) */
static u16 t_cmd, t_ev, t_done;             /* lineas: envio del comando, ultimo sector, fin de la ultima lectura */
static int mode = -1, want_mode = M_AUDIO, loc_at = -1, drv_next = -1, lost;
/* peticion de lectura de los sectores lba..lba+n-1: a buf (como mucho 32, en cualquier orden: rd_got), o en orden
   a cb (por sbuf) o a donde diga dst (0: aun no hay sitio, se volvera a buscar). rd_i: el primero que falta */
static int rd_on, rd_lba, rd_n, rd_i, rd_fast, rd_err, rd_res; static unsigned rd_got;
static u8 *rd_buf; static void (*rd_cb)(int i, const unsigned char *d); static u8 *(*rd_dst)(int i);
static u8 sbuf[2048] __attribute__((aligned(4))); static int sbuf_dir = -1;   /* sbuf tiene el directorio */
/* audio */
static int play_at = -1, stop_req, loc_req, loc_res = -1;
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
    u8 h[12], *d = 0; int k, i;
    CD0 = 0; CD3 = 0x80;                    /* pedir los datos del sector */
    for (k = 0; k < 1000 && !(CD0 & 0x40); k++);
    for (k = 0; k < 12; k++) h[k] = CD2;    /* cabecera y subcabecera */
    i = msf(h) - rd_lba;
    if (rd_on && i >= 0 && i < rd_n) {
        if (rd_buf) { if (!(rd_got & (1u << i))) d = rd_buf + i * 2048; }
        else if (i == rd_i) d = rd_dst ? rd_dst(i) : sbuf;
    }
    if (d) for (k = 0; k < 2048; k += 4) { d[k] = CD2; d[k + 1] = CD2; d[k + 2] = CD2; d[k + 3] = CD2; }
    CD0 = 0; CD3 = 0;                       /* soltar el resto */
    cd_dbg[0]++;
    if (rd_on && !d && i > rd_i && !rd_buf) cd_dbg[1]++;
    if (i >= -8 && i <= rd_n + 8) drv_next = rd_lba + i + 1;   /* los de otra parte del disco (de antes de buscar) no cuentan */
    t_ev = T1; *idx = i;
    return d;
}
static void event(int t)
{
    if (t == 3) {                           /* primera respuesta */
        switch (cmd) {
        case 0x02: loc_at = cmd_arg; break;
        case 0x06: drv = 1; drv_next = loc_at; loc_at = -1; t_ev = T1; lost = 0; break;
        case 0x03: drv = 2; loc_at = -1; break;
        case 0x09: case 0x0A: wait2 = cmd; break;
        case 0x0E: mode = cmd_arg; break;
        case 0x11: loc_res = nresp >= 8 ? msf(resp + 5) : -1; loc_req = 0; break;
        }
        cmd = 0;
    } else if (t == 2) {                    /* segunda respuesta (Pause, Init) */
        if (wait2) { drv = 0; wait2 = 0; }
    } else if (t == 4) {                    /* fin de pista (pausa automatica) o de disco */
        if (drv == 2) cd_ended = 1;
        drv = 0;
    } else if (t == 5) {                    /* error */
        cd_dbg[2]++;
        if (cmd == 0x11) { loc_res = -1; loc_req = 0; }
        if (cmd == 0x03 || (cmd == 0x02 && play_at >= 0) || (!cmd && drv == 2)) { play_at = -1; cd_ended = 1; }
        if (cmd == 0x02 || cmd == 0x06 || (!cmd && drv == 1)) rd_err++;
        if (cmd == 0x02) loc_at = -1;
        if (cmd == 0x06 || cmd == 0x03 || !cmd) drv = 0;
        if (wait2 && !cmd) { wait2 = 0; drv = 0; }
        cmd = 0;
    }
}
/* siguiente comando segun lo que se quiere */
static void step(void)
{
    if (cmd || wait2 || (CD0 & 0x80)) return;
    if (loc_req) { if (drv == 2) { send(0x11, 0, 0, 0); return; } loc_req = 0; loc_res = -1; }
    if (play_at >= 0) {
        if (drv == 1) { send(0x09, 0, 0, 0); return; }          /* la lectura sigue luego */
        if (mode != M_AUDIO) { setmode(M_AUDIO); return; }
        if (loc_at != play_at) { setloc(play_at); return; }
        send(0x03, 0, 0, 0); play_at = -1; cd_ended = 0; return;
    }
    if (stop_req) { stop_req = 0; if (drv) { send(0x09, 0, 0, 0); return; } }
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
    if (drv == 1) { if (rd_res <= 0 || (u16)(T1 - t_done) >= GRACE) send(0x09, 0, 0, 0); return; }
    if (!drv && mode != want_mode) setmode(want_mode);
}
void cd_poll(void)
{
    int k, t;
    for (k = 0; k < 8; k++) {
        u8 *d = 0; int i = 0;
        CD0 = 1; t = CD3 & 7;
        if (!t) break;
        nresp = 0;
        while ((CD0 & 0x20) && nresp < 16) resp[nresp++] = CD1;
        if (t == 1 && drv == 1) d = sector(&i);
        CD0 = 1; CD3 = 7;                   /* reconocer */
        if (t != 1) event(t);
        else if (d) {
            if (rd_buf) { rd_got |= 1u << i; while (rd_i < rd_n && (rd_got & (1u << rd_i))) rd_i++; } else rd_i++;
            if (rd_i == rd_n) { rd_on = 0; rd_res = 1; t_done = T1; }
            if (rd_cb) rd_cb(i, d);
        }
    }
    /* vigilancia: respuestas que no llegan */
    if ((cmd && (u16)(T1 - t_cmd) > SEG(2)) || (wait2 && (u16)(T1 - t_cmd) > SEG(2))) {
        cd_dbg[3]++; cmd = 0; wait2 = 0; loc_at = -1;
        if (drv != 2) drv = 1;              /* estado incierto: parar y volver a empezar */
        if (rd_on) rd_err++;
    }
    if (rd_on && drv == 1 && (u16)(T1 - t_ev) > SEG(3)) { cd_dbg[3]++; rd_err++; lost = 1; t_ev = T1; }
    step();
}
int cd_busy(void) { return cmd || wait2 || rd_on || play_at >= 0 || stop_req || loc_req; }
static int pending(void) { return cd_busy() || drv == 1 || (!drv && mode != want_mode); }
/* ---- lectura */
static void rd_start(int lba, int n, void *buf, void (*cb)(int, const unsigned char *), u8 *(*dst)(int), int fast)
{
    if (!buf && !dst) sbuf_dir = -1;
    rd_lba = lba; rd_n = n; rd_i = 0; rd_buf = buf; rd_cb = cb; rd_dst = dst; rd_fast = fast; rd_err = 0; rd_res = 0; rd_got = 0;
    rd_on = n > 0; if (!rd_on) rd_res = 1;
    lost = 0;
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
void cd_cancel(void) { rd_on = 0; rd_res = -1; }
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

int cd_init(void)
{
    int k;
    CD0 = 1; CD2 = 0x1F;                    /* habilitar banderas de interrupcion del controlador */
    CD0 = 1; CD3 = 0x1F;
    cmd = wait2 = 0; drv = 1; want_mode = M_FAST;
    send(0x0A, 0, 0, 0);                    /* Init: para la unidad, motor en marcha */
    cd_sync();
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
void cd_play(int lba) { play_at = lba; stop_req = 0; cd_ended = 0; }
void cd_stop(void) { play_at = -1; stop_req = 1; }
int cd_audio(void) { return drv == 2 || play_at >= 0 || cmd == 0x03; }
int cd_playing(void) { return drv == 2; }
/* posicion del audio sin esperar: cd_loc_start() pide GetlocP y cd_loc_get() da -2 mientras no llega la
   respuesta, luego la posicion (-1 si no suena o no contesta) */
void cd_loc_start(void) { loc_res = -1; loc_req = drv == 2; }
int cd_loc_get(void) { return loc_req ? -2 : loc_res; }
/* posicion del audio (sincrona, GetlocP); -1 si no suena */
int cd_where(void)
{
    u16 t0 = T1;
    if (drv != 2) return -1;
    loc_req = 1; loc_res = -1;
    while (loc_req && (u16)(T1 - t0) < SEG(1)) cd_poll();
    loc_req = 0;
    return loc_res;
}

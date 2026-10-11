/* Lector de CD-ROM por sondeo (sin interrupciones de la CPU ni BIOS): comandos directos al controlador,
   lectura de sectores de 2048 bytes a doble velocidad y busqueda de ficheros en el directorio raiz ISO9660. */
#include "cd.h"
#define CD0 (*(volatile unsigned char *)0x1F801800)
#define CD1 (*(volatile unsigned char *)0x1F801801)
#define CD2 (*(volatile unsigned char *)0x1F801802)
#define CD3 (*(volatile unsigned char *)0x1F801803)

static unsigned char resp[16];
int cd_dbg[8];
static int nresp;

static int wait_int(void)
{
    int t, n = 0;
    CD0 = 1;
    for (n = 0; n < 20000000 && !(CD3 & 7); n++);
    t = CD3 & 7;
    nresp = 0;
    while ((CD0 & 0x20) && nresp < 16) resp[nresp++] = CD1;
    CD0 = 1; CD3 = 7;                    /* reconocer */
    for (n = 0; n < 200 && (CD3 & 7); n++) { CD0 = 1; CD3 = 7; }
    return t;
}
static void cmd(unsigned char c, const unsigned char *p, int n)
{
    int i;
    while (CD0 & 0x80);
    CD0 = 1; CD3 = 0x1F; CD3 = 0x40;     /* limpiar banderas y la cola de parametros */
    CD0 = 0;
    for (i = 0; i < n; i++) CD2 = p[i];
    CD0 = 0; CD1 = c;
}
static int cmd_ack(unsigned char c, const unsigned char *p, int n)
{
    cmd(c, p, n);
    return wait_int();
}

int cd_init(void)
{
    unsigned char m = 0x80;              /* doble velocidad, sectores de 2048 */
    int t;
    CD0 = 1; CD2 = 0x1F;                 /* habilitar banderas de interrupcion del controlador */
    CD0 = 1; CD3 = 0x1F;
    t = cmd_ack(0x0A, 0, 0);             /* Init */
    cd_dbg[0] = t * 100 + resp[0];
    if (t == 3) cd_dbg[1] = wait_int() * 100 + resp[0];
    for (t = 0; t < 8; t++) { cmd_ack(0x01, 0, 0); if (!(resp[0] & 0x10)) break; }   /* GetStat: borra 'tapa abierta' */
    t = cmd_ack(0x1E, 0, 0);             /* ReadTOC */
    if (t == 3) t = wait_int();
    cd_dbg[6] = t * 100 + resp[0];
    cmd_ack(0x0C, 0, 0);                 /* Demute */
    t = cmd_ack(0x0E, &m, 1);            /* Setmode */
    cd_dbg[2] = t * 100 + resp[0];
    return t == 3 ? 0 : -1;
}
static unsigned char bcd(int v) { return (unsigned char)(((v / 10) << 4) | (v % 10)); }
static void setloc(int lba)
{
    unsigned char p[3];
    lba += 150;
    p[0] = bcd(lba / 4500); p[1] = bcd((lba / 75) % 60); p[2] = bcd(lba % 75);
    cmd_ack(0x02, p, 3);
}
/* lee n sectores seguidos a partir de lba en buf (2048 bytes cada uno). 0 = bien */
int cd_read(int lba, int n, void *buf)
{
    unsigned char *d = (unsigned char *)buf;
    int s, k, t, tries;
    for (tries = 0; tries < 4; tries++) {
        setloc(lba);
        cmd(0x06, 0, 0);                 /* ReadN */
        t = wait_int();
        cd_dbg[3] = t * 100 + resp[0]; cd_dbg[7] = resp[1];
        if (t != 3) { cmd_ack(0x09, 0, 0); continue; }
        for (s = 0; s < n; s++) {
            t = wait_int();
            cd_dbg[4] = t * 100 + resp[0]; cd_dbg[5]++;
            if (t != 1) break;
            CD0 = 0; CD3 = 0x80;         /* pedir los datos del sector */
            for (k = 0; k < 2000 && !(CD0 & 0x40); k++);
            for (k = 0; k < 2048; k++) d[s * 2048 + k] = CD2;
            CD0 = 0; CD3 = 0x00;
        }
        t = cmd_ack(0x09, 0, 0);         /* Pause */
        if (t == 3) wait_int();
        if (s == n) return 0;
        lba += s; n -= s; d += s * 2048;
    }
    return -1;
}
static unsigned int le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
/* busca NOMBRE (sin ";1") en el directorio raiz; devuelve lba y tamano */
int cd_find(const char *name, int *lba, int *size)
{
    static unsigned char sec[2048];
    int root, rsize, off, s;
    if (cd_read(16, 1, sec)) return -1;
    root = le32(sec + 156 + 2); rsize = le32(sec + 156 + 10);
    for (s = 0; s * 2048 < rsize; s++) {
        if (cd_read(root + s, 1, sec)) return -1;
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
static int bcd2(unsigned char v) { return (v >> 4) * 10 + (v & 15); }
/* lba del principio de la pista t (GetTD), -1 si falla */
int cd_track_lba(int t)
{
    unsigned char p = bcd(t);
    if (cmd_ack(0x14, &p, 1) != 3 || nresp < 3) return -1;
    return (bcd2(resp[1]) * 60 + bcd2(resp[2])) * 75 - 150;
}
/* volumen de la salida de audio del CD (0x80 = 100 %) */
void cd_volume(int v)
{
    CD0 = 2; CD2 = v; CD3 = 0;
    CD0 = 3; CD1 = v; CD2 = 0; CD3 = 0x20;
}
int cd_play_at(int lba)
{
    unsigned char m = 0x80;
    setloc(lba);
    cmd_ack(0x03, 0, 0);                 /* Play desde la posicion de Setloc */
    (void)m;
    return 0;
}
void cd_stop_audio(void)
{
    int t = cmd_ack(0x09, 0, 0);
    if (t == 3) wait_int();
}

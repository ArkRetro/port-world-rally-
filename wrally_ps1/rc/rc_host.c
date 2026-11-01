/* Banco de pruebas del codigo recompilado en el PC. Con -DRCDBG se ejecuta en paralelo con Musashi
   (instruccion a instruccion) y se comparan registros, banderas vivas y la RAM en cada cuadro. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
/* Windows (herramienta/windows): sin fork, el piloto automatico (AUTOP) no va; las corridas repiten las partidas
   grabadas (tools/pilotos.txt). mkdir sin permisos */
#include <direct.h>
#define mkdir(d, m) _mkdir(d)
static int fork(void) { fprintf(stderr, "AUTOP: el piloto automatico necesita fork (Windows no lo tiene)\n"); exit(1); }
static int pipe(int *p) { (void)p; return 0; }
static int wait(int *s) { (void)s; return -1; }
#else
#include <sys/wait.h>
#endif
#include "rc_rt.h"
/* ficheros de trabajo en $WR_WORK (por defecto ../work) */
static const char *WORKF(const char *n) { static char b[4][512]; static int k; const char *w = getenv("WR_WORK"); k = (k + 1) & 3; snprintf(b[k], 512, "%s/%s", w ? w : "../work", n); return b[k]; }
#include "mcs51.h"

static u8 mcurom[0x8000];
static mcs51_t mcu_sw;
#ifdef RCDBG
static mcs51_t mcu_be;
void mcs51_reset(mcs51_t *c); long mcs51_run(mcs51_t *c, unsigned short stop, long max);
#endif
void mcs51_reset_sw(mcs51_t *c); long mcs51_run_sw(mcs51_t *c, unsigned short stop, long max);
u16 gdecrypt(u16 ep, u16 dp, u16 e);
static u16 io_in = 0xFFEF, io_dsw = 0xFFDF;
static long frame, nframes = 3000;
static int ap_child = -1;                   /* piloto automatico: candidato que prueba este proceso hijo */
static const char *outdir = ".";

static void load(const char *fn, u8 *d, int n)
{
    FILE *f = fopen(fn, "rb");
    if (!f || fread(d, 1, n, f) != (size_t)n) { fprintf(stderr, "no %s\n", fn); exit(1); }
    fclose(f);
}

/* ---- E/S para la referencia Musashi */
static u16 io_r(u32 a)
{
    if ((a & 0xFE) == 0x0E) return *(u16 *)(rc_iopg + 0xE);
    switch (a & 0xFE) {
    case 0x00: return io_dsw;
    case 0x02: return io_in;
    case 0x04: return 0xFF00;
    case 0x08: return 0xFFF3;
    case 0x0E: return 0x0000;
    default: return 0xFFFF;
    }
}
void rc_mem_init(void); void rc_set_io(u16 dsw, u16 in); void rc_oki_frame(void);
static FILE *sndlog;
void snd_play(int ch, int phrase, int bank, int att) { if (sndlog && ap_child < 0) fprintf(sndlog, "%ld P %d %d %d %d\n", frame, ch, phrase, bank, att); }
void snd_stop(int mask) { if (sndlog && ap_child < 0) fprintf(sndlog, "%ld S %d\n", frame, mask); }
extern u32 rc_vram_writes; static unsigned long mcu_steps;
void mcu_run_c(mcs51_t *c);
void rc_mcu(void)
{
#ifdef MCU_INTERP
    mcu_steps += mcs51_run_sw(&mcu_sw, 0x013F, 2000000);
#else
    mcu_run_c(&mcu_sw);
#endif
}
void rc_divzero(u32 pc) { fprintf(stderr, "division por cero en %06X\n", pc); exit(1); }
void rc_bad(u32 pc) { fprintf(stderr, "despacho desconocido: %06X (cuadro %ld)\n", pc, frame); exit(1); }

static void dump(void)
{
    char fn[512]; FILE *f; int i; u8 t[0x4000];
#define D(name, buf, n) for (i = 0; i < n; i++) t[i] = buf[i ^ 1]; sprintf(fn, "%s/%s", outdir, name); f = fopen(fn, "wb"); fwrite(t, 1, n, f); fclose(f);
    D("d_vram.bin", rc_vram, 0x4000) D("d_pal.bin", rc_pal, 0x4000) D("d_vregs.bin", rc_vregs, 0x10)
    D("d_spr.bin", rc_spr, 0x1000) D("d_ram.bin", rc_ram, 0x4000)
}

/* botones por cuadro (como perf/psxprof): moneda en 600 (NOCOIN: sin ella), START en 800/1000/1200, acelerar desde
   1300; STEER=n: direccion oscilante desde 1300 (n cuadros recto, derecha, recto, izquierda) para ver choques;
   INPUTS=a-b:BOT,...: botones de mas en [a, b) (a-b%P/W: cuando (cuadro - a) % P < W) con las letras de psxprof:
   C moneda, S START, X acelerar, x soltarlo, Q cuadrado (cambio de marcha), cruceta R L U D. P. ej. para elegir
   rally en 'Choose a Rally': R en 1180-1190 Monte-Carlo, R tres veces Acropolis, L 1000 Lagos */
static u16 inputs(long fr)
{
    static long st = -1; static int gear, qlast;
    const char *q;
    int qn = 0;
    u16 in = 0xFFEF;
    if (st < 0) st = getenv("STEER") ? atol(getenv("STEER")) : 0;
    if (fr >= 600 && fr < 606 && !getenv("NOCOIN")) in &= ~0x40;
    if ((fr >= 800 && fr < 810) || (fr >= 1000 && fr < 1010) || (fr >= 1200 && fr < 1210)) in &= ~0x4000;
    if (fr >= 1300) in &= ~0x20;
    if (st > 0 && fr >= 1300) { long ph = (fr - 1300) / st % 4; if (ph == 1) in &= ~0x04; else if (ph == 3) in &= ~0x08; }
    for (q = getenv("INPUTS"); q && *q; ) {
        long a = strtol(q, (char **)&q, 10), b = a + 1, P = 0, Wd = 0; const char *bt, *c;
        if (*q == '-') b = strtol(q + 1, (char **)&q, 10);
        if (*q == '%') { P = strtol(q + 1, (char **)&q, 10); if (*q == '/') Wd = strtol(q + 1, (char **)&q, 10); }
        if (*q == ':') q++;
        for (bt = q; *q && *q != ','; q++);
        if (fr >= a && fr < b && (!P || (fr - a) % P < Wd)) for (c = bt; c < q; c++) switch (*c) {
            case 'C': in &= ~0x40; break; case 'S': in &= ~0x4000; break; case 'X': in &= ~0x20; break; case 'x': in |= 0x20; break;
            case 'Q': qn = 1; break; case 'L': in &= ~0x08; break; case 'R': in &= ~0x04; break;
            case 'U': in &= ~0x01; break; case 'D': in &= ~0x02; break;
            }
        if (*q == ',') q++;
    }
    if (qn && !qlast) gear ^= 1;                    /* como el port: el cuadrado cambia de marcha al pulsarlo */
    qlast = qn;
    if (gear) in |= 0x10;
    return in;
}

#ifdef RCDBG
/* ================= referencia Musashi ================= */
#include "m68k.h"
static u8 m_vram[0x4000], m_pal[0x4000], m_vregs[0x10], m_spr[0x1000], m_ram[0x4000];
void mcs51_reset(mcs51_t *c); long mcs51_run(mcs51_t *c, unsigned short stop, long max);
static u8 *m_map(u32 a, u32 *mask)
{
    a &= 0xFFFFFF;
    if (a < 0x100000) { *mask = 0xFFFFF; return NULL; }
    if (a >= 0x100000 && a < 0x104000) { *mask = 0x3FFF; return m_vram; }
    if (a >= 0x108000 && a < 0x108010) { *mask = 0xF; return m_vregs; }
    if (a >= 0x200000 && a < 0x204000) { *mask = 0x3FFF; return m_pal; }
    if (a >= 0x440000 && a < 0x441000) { *mask = 0xFFF; return m_spr; }
    if (a >= 0xFEC000) { *mask = 0x3FFF; return m_ram; }
    return NULL;
}
static u8 rombe[0x100000];
unsigned int m68k_read_memory_8(unsigned int a)
{
    u32 m; u8 *p = m_map(a, &m);
    a &= 0xFFFFFF;
    if (a < 0x100000) return rombe[a];
    if (p) return p[a & m];
    if ((a & 0xFFFF00) == 0x700000) { u16 v = io_r(a); return (a & 1) ? (v & 0xFF) : (v >> 8); }
    return 0xFF;
}
unsigned int m68k_read_memory_16(unsigned int a)
{
    u32 m; u8 *p = m_map(a, &m);
    a &= 0xFFFFFF;
    if (a < 0x100000) return (rombe[a] << 8) | rombe[a + 1];
    if (p) { a &= m; return (p[a] << 8) | p[a + 1]; }
    if ((a & 0xFFFF00) == 0x700000) return io_r(a);
    return 0xFFFF;
}
unsigned int m68k_read_memory_32(unsigned int a) { return (m68k_read_memory_16(a) << 16) | m68k_read_memory_16(a + 2); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
static u32 lastpc; static int lastoff = -2; static u16 lastenc, lastdec;
void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    u32 m; u8 *p;
    a &= 0xFFFFFF; v &= 0xFFFF;
    if (a >= 0x100000 && a < 0x104000) {
        u32 pc = m68k_get_reg(NULL, M68K_REG_PPC); int off = (a & 0x3FFF) >> 1;
        if (lastpc == pc && off == lastoff + 1) { lastpc = 0; v = gdecrypt(lastenc, lastdec, v); }
        else { lastenc = v; lastpc = pc; lastoff = off; v = lastdec = gdecrypt(0, 0, v); }
    }
    if (a < 0x100000) return;
    p = m_map(a, &m);
    if (p) { p[a & m] = v >> 8; p[(a & m) + 1] = v; if ((a == 0xFEC102 && v == 1) || a == 0xFEF500) mcs51_run(&mcu_be, 0x013F, 2000000); }
}
void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    u32 m; u8 *p;
    a &= 0xFFFFFF;
    if (a < 0x100000) return;
    p = m_map(a, &m);
    if (p) p[a & m] = v;
}
void m68k_write_memory_32(unsigned int a, unsigned int v) { m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v & 0xFFFF); }
static int irq_ack(int lvl) { m68k_set_irq(0); return M68K_INT_ACK_AUTOVECTOR; }

static unsigned long nck;
static void fail(const char *what, u32 pc, u32 a, u32 b)
{
    char buf[200]; int i;
    m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
    fprintf(stderr, "DIFERENCIA en %06X (%s) tras %lu instr, cuadro %ld: %s rc=%08X ref=%08X\n", pc, buf, nck, frame, what, a, b);
    for (i = 0; i < 8; i++) fprintf(stderr, " d%d %08X/%08X  a%d %08X/%08X\n", i, rc_r.d[i], m68k_get_reg(NULL, M68K_REG_D0 + i), i, rc_r.a[i], m68k_get_reg(NULL, M68K_REG_A0 + i));
    fprintf(stderr, " ultima instr ref: %06X\n", m68k_get_reg(NULL, M68K_REG_PPC));
    exit(2);
}
static u32 lastck;
void rc_ck(u32 pc, u32 live)
{
    u32 sr, i, mpc = m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF;
    if (mpc != pc) fail("PC", lastck, pc, mpc);
    for (i = 0; i < 8; i++) {
        if (rc_r.d[i] != m68k_get_reg(NULL, M68K_REG_D0 + i)) fail("D", lastck, i, 0);
        if (rc_r.a[i] != m68k_get_reg(NULL, M68K_REG_A0 + i)) fail("A", lastck, i, 0);
    }
    sr = m68k_get_reg(NULL, M68K_REG_SR);
    {
        u32 rs = (rc_r.imask << 8) | (rc_r.fX << 4) | (rc_r.fN << 3) | (rc_r.fZ << 2) | (rc_r.fV << 1) | rc_r.fC;
        u32 mask = 0x700 | live;
        if ((rs & mask) != (sr & mask)) fail("SR", lastck, rs, sr);
    }
    lastck = pc;
    nck++;
    m68k_execute(1);
}
void m68k_check_irq_now(void);
void rc_dbg_irq(void)
{
    u32 sp;
    m68k_set_irq(6); m68k_check_irq_now();
    /* el SR apilado lleva banderas que en el recompilado pueden estar muertas: se copia el de referencia */
    sp = m68k_get_reg(NULL, M68K_REG_A7) & 0xFFFFFF;
    *(u16 *)(rc_ram + (sp & 0x3FFF)) = m68k_read_memory_16(sp);
}
static void cmp_mem(void)
{
    int i;
    for (i = 0; i < 0x4000; i++) if (rc_ram[i ^ 1] != m_ram[i] && (i < 0x3E00)) { fprintf(stderr, "RAM %06X rc=%02X ref=%02X cuadro %ld\n", 0xFEC000 + i, rc_ram[i ^ 1], m_ram[i], frame); exit(3); }
    for (i = 0; i < 0x4000; i++) if (rc_vram[i ^ 1] != m_vram[i]) { fprintf(stderr, "VRAM %06X cuadro %ld\n", 0x100000 + i, frame); exit(3); }
    for (i = 0; i < 0x4000; i++) if (rc_pal[i ^ 1] != m_pal[i]) { fprintf(stderr, "PAL %06X cuadro %ld\n", 0x200000 + i, frame); exit(3); }
    for (i = 0; i < 0x1000; i++) if (rc_spr[i ^ 1] != m_spr[i]) { fprintf(stderr, "SPR %06X cuadro %ld\n", 0x440000 + i, frame); exit(3); }
}
#endif

#ifdef BLKLOG
static FILE *blkf;
static void blklog(void)
{
    static unsigned char seen[1024]; static unsigned char cur[32768]; int L, i, j; const u16 *vr = (const u16 *)rc_vregs;
    memset(cur, 0, sizeof(cur));
    for (L = 0; L < 2; L++) {
        int sx = L ? (vr[3] + 8) & 1023 : (vr[1] + 12) & 1023, sy = L ? (vr[2] + 16) & 511 : (vr[0] + 16) & 511;
        const u16 *m = (const u16 *)(rc_vram + L * 0x2000);
        for (j = 0; j < 17; j++) for (i = 0; i < 25; i++) {
            int e = ((((sy >> 4) + j) & 31) * 64 + (((sx >> 4) + i) & 63)) * 2;
            cur[m[e] & 0x3FFF] = 1;
        }
    }
    {
        const u16 *sp = (const u16 *)rc_spr;
        for (i = 3; i < (0x1000 - 6) / 2; i += 4) {
            u16 w0 = sp[i], w2 = sp[i + 2], w3 = sp[i + 3];
            int sx = ((w2 & 0x3FF) - 23) & 0x3FF, sy = ((240 - (w0 & 0xFF)) & 0xFF) - 16;
            if (sx >= 0x3F0) sx -= 0x400;
            if (sx >= 368 || sy >= 240 || sx <= -16 || sy <= -16) continue;
            cur[16384 + (w3 & 0x3FFF)] = 1;
        }
    }
    if (!blkf) blkf = fopen(getenv("BLKLOG"), "wb");
    {   u32 lv = ((u32)*(u16 *)(rc_ram + 0x190) << 16) | *(u16 *)(rc_ram + 0x192);
        for (i = 0; i < 32768; i++) if (cur[i]) { u32 rec[3] = { (u32)frame, (u32)i, lv }; fwrite(rec, 4, 3, blkf); } }
    (void)seen;
}
#endif
#ifdef TILESTAT
static unsigned char tl_used[16384]; static long tl_first[16384];
static void tilestat(void)
{
    int i;
    for (i = 0; i < 4096; i++) { u16 c = *(u16 *)(rc_vram + i * 4) & 0x3FFF; if (!tl_used[c]) { tl_used[c] = 1; tl_first[c] = frame; } }
    for (i = 3; i < (0x1000 - 6) / 2; i += 4) { u16 c = *(u16 *)(rc_spr + (i + 3) * 2) & 0x3FFF; u16 y = *(u16 *)(rc_spr + i * 2);
        if (!tl_used[c]) { tl_used[c] = 2; tl_first[c] = frame; } }
}
static void tiledump(void)
{
    {   /* primer cuadro de uso de cada bloque de 16 tiles (solo sprites visibles y mapa visible) */
    }
    FILE *f = fopen(WORKF("tl_first.bin"), "wb"); fwrite(tl_first, sizeof(long), 16384, f); fclose(f);
    f = fopen(WORKF("tl_used.bin"), "wb"); fwrite(tl_used, 1, 16384, f); fclose(f);
}
#endif
#ifdef PROFCK
extern long prof_frame, prof_lo, prof_hi; extern unsigned int pc_hist[]; extern unsigned long fr_cnt[];
#endif
/* piloto automatico (AUTOP=a-b): de a a b, cada AP_STEP cuadros (10) se prueba cada candidato (direccion a un lado,
   al otro, a ratos o nada) en un proceso hijo durante AP_LOOK cuadros (150) y se sigue con el que mas avanza en el
   tramo (palabra 0xFEC1CC del juego). Asi las partidas registradas llegan a la meta y pasan por los tres tramos de
   cada rally (y los siguientes), que con la direccion fija no se ven. AP_V=1: el avance por stderr; al acabar, el
   guion de botones (INPUTS) que ha seguido, para repetirlo en psxprof */
static const char *ap_pat[] = {"", "L", "R", "Lh", "Rh", "Lq", "Rq"};
#define AP_NC 7
static int ap_cur, ap_fd; static long ap_a = -2, ap_b, ap_step, ap_look, ap_eval, ap_from, ap_t0;
static char ap_out[1 << 16]; static int ap_outn;
static int ap_progress(void) { return *(u16 *)(rc_ram + 0x1CC); }
static void ap_apply(int c, long fr, long t0)
{
    const char *p = ap_pat[c];
    if (*p && (!p[1] || (fr - t0) % 8 < (p[1] == 'h' ? 4 : 2))) io_in &= p[0] == 'L' ? ~0x08 : ~0x04;
}
static void ap_log(long to)
{
    const char *p = ap_pat[ap_cur];
    if (*p && ap_outn < (int)sizeof ap_out - 64)
        ap_outn += sprintf(ap_out + ap_outn, "%ld-%ld%s:%c,", ap_from, to, p[1] == 'h' ? "%8/4" : p[1] == 'q' ? "%8/2" : "", p[0]);
}
static void autop(void)
{
    if (ap_a == -2) {
        const char *q = getenv("AUTOP");
        ap_a = q ? atol(q) : -1; ap_b = q && strchr(q, '-') ? atol(strchr(q, '-') + 1) : -1; ap_from = ap_a;
        ap_step = getenv("AP_STEP") ? atol(getenv("AP_STEP")) : 10; ap_look = getenv("AP_LOOK") ? atol(getenv("AP_LOOK")) : 150;
    }
    if (ap_a < 0) return;
    if (ap_child >= 0) {                        /* hijo: su candidato hasta ap_eval, y entonces cuenta cuanto ha avanzado */
        if (frame >= ap_eval) { int v = ap_progress(); if (write(ap_fd, &v, sizeof v) < 0) {} _exit(0); }
        ap_apply(ap_child, frame, ap_t0); return;
    }
    if (frame >= ap_a && frame < ap_b && frame + ap_look < nframes && (frame - ap_a) % ap_step == 0) {
        int c, best = ap_cur, bv = -1, fd[AP_NC], v;
        fflush(NULL);
        for (c = 0; c < AP_NC; c++) {
            int pp[2];
            if (pipe(pp)) { perror("pipe"); exit(1); }
            if (!fork()) { close(pp[0]); ap_child = c; ap_fd = pp[1]; ap_eval = frame + ap_look; ap_t0 = frame; ap_apply(c, frame, frame); return; }
            close(pp[1]); fd[c] = pp[0];
        }
        for (c = 0; c < AP_NC; c++) { v = -1; if (read(fd[c], &v, sizeof v) < 0) v = -1; close(fd[c]); if (v > bv || (v == bv && c == ap_cur)) { bv = v; best = c; } }
        while (wait(0) > 0);
        if (best != ap_cur) { ap_log(frame); ap_cur = best; ap_from = frame; }
        if (getenv("AP_V")) fprintf(stderr, "cuadro %ld avance %d: %s\n", frame, ap_progress(), ap_pat[best]);
    }
    if (frame == ap_b) { ap_log(frame); ap_cur = 0; if (ap_outn) ap_out[ap_outn - 1] = 0; fprintf(stderr, "INPUTS=%s\n", ap_out); }
    if (frame >= ap_a && frame < ap_b) ap_apply(ap_cur, frame, ap_from);
}
void rc_vblank(void)
{
#ifdef BLKLOG
    if (ap_child < 0) blklog();
#endif
    rc_oki_frame();
    if (getenv("DUMPK") && frame % atol(getenv("DUMPK")) == 0 && ap_child < 0) {
        static char d[600]; const char *o = outdir; sprintf(d, "%s/f%05ld", o, frame);
        mkdir(d, 0755); outdir = d; dump(); outdir = o; }
    if (!sndlog && getenv("SNDLOG")) sndlog = fopen(getenv("SNDLOG"), "w");
    /* la MCU da una vuelta a su bucle principal por cuadro (contestacion 0xA53E en 0xFEF500, etc.) */
    rc_mcu();
#ifdef RCDBG
    mcs51_run(&mcu_be, 0x013F, 100000);
#endif
#ifdef PROFCK
    prof_frame = frame + 1;
    if (frame + 1 >= nframes) { FILE *f = fopen(WORKF("pch.bin"), "wb"); fwrite(pc_hist, 4, 0x80000, f); fclose(f);
        f = fopen(WORKF("frc.bin"), "wb"); fwrite(fr_cnt, 8, nframes, f); fclose(f); }
#endif
#ifdef TILESTAT
    tilestat();
    if (frame + 1 >= nframes) tiledump();
#endif
#ifdef RCDBG
    cmp_mem();
#endif
    frame++;
    io_in = inputs(frame); autop(); rc_set_io(io_dsw, io_in);
    if (getenv("V") && frame % 100 == 0) { fprintf(stderr, "cuadro %ld vram %u mcu %lu\n", frame, rc_vram_writes, mcu_steps); }
    rc_vram_writes = 0; mcu_steps = 0;
    if (frame >= nframes) {
        dump();
        fprintf(stderr, "fin: %ld cuadros\n", frame);
#ifdef ROMSTAT
        { FILE *f = fopen(WORKF("romrd.bin"), "wb"); fwrite(romrd, sizeof(long), 256, f); fclose(f); }
#endif
#ifdef RCDBG
        fprintf(stderr, "instrucciones comparadas: %lu\n", nck);
#endif
        exit(0);
    }
}

int main(int argc, char **argv)
{
    int i;
    u8 be[0x100000];
    if (argc > 1) nframes = atol(argv[1]);
    if (argc > 2) outdir = argv[2];
#ifdef PROFCK
    if (argc > 4) { prof_lo = atol(argv[3]); prof_hi = atol(argv[4]); }
#endif
    load(WORKF("prog.bin"), be, 0x100000);
    for (i = 0; i < 0x100000; i++) rc_rom[i] = be[i ^ 1];
    load(WORKF("mcu.bin"), mcurom, 0x8000);
#ifndef ROMSTAT
#endif
    rc_mem_init();
    mcu_sw.code = mcurom; mcu_sw.xram = rc_ram;
    mcs51_reset_sw(&mcu_sw); mcs51_run_sw(&mcu_sw, 0x013F, 1000000);
    memset(&rc_r, 0, sizeof(rc_r));
    rc_r.a[7] = (be[0] << 24) | (be[1] << 16) | (be[2] << 8) | be[3];
    rc_r.imask = 7;
    rc_r.cyc_next = CYC_FRAME;
#ifdef RCDBG
    memcpy(rombe, be, 0x100000);
    mcu_be.code = mcurom; mcu_be.xram = m_ram;
    mcs51_reset(&mcu_be); mcs51_run(&mcu_be, 0x013F, 1000000);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(irq_ack);
    m68k_pulse_reset();
    m68k_execute(1);     /* consume los ciclos de reset */
    if ((m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF) != 0x2400) fprintf(stderr, "pc tras reset %X\n", m68k_get_reg(NULL, M68K_REG_PC));
#endif
    io_in = inputs(0); rc_set_io(io_dsw, io_in);
    rc_run((be[4] << 24) | (be[5] << 16) | (be[6] << 8) | be[7]);
    return 1;
}

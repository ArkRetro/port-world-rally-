/* Banco de pruebas del codigo recompilado del World Rally 2 en el PC. Con -DRCDBG se ejecuta en paralelo con Musashi
   (instruccion a instruccion) y se comparan registros, banderas vivas y la memoria en cada cuadro.
   Uso: rc_host cuadros [carpeta]. Ficheros de trabajo en $WR2_WORK (por defecto ../work).
   Los cuadros se cuentan como en MAME (lua de referencia del analisis): el cuadro n es el n-esimo vblank y el juego ve
   en su IRQ las entradas del guion del cuadro n (el volcado y el registro del cuadro n, las del n-1). Variables:
     INPUTS=a-b:LETRAS,...   botones en los cuadros [a, b) (a-b%P/W: cuando (cuadro - a) % P < W): C moneda, S START,
                             X acelerar, G marcha (pulsada = larga), L R U D cruceta, V servicio (como to_inputs.py)
     DUMP=a-b/p              volcado del estado en esos cuadros, en el formato de wr2ref.lua (WR2D), todos seguidos en
                             <carpeta>/dumps.bin (como WR2_DUMPCAT=1); se comparan con cmpdump.py
     LOG=1                   <carpeta>/log.txt, una linea por cuadro con las columnas del log.txt de MAME
     SNDLOG=fichero          sucesos del GAE1 (canal, trozo, registros)
     V=1                     cuadros y ciclos por stderr cada 100 cuadros
     FASE=n                  momento del primer vblank en % de cuadro (98, como MAME)
     MCU=0                   trabajo de la MCU al momento (por defecto 1: con su tiempo, como la placa)
     VIRQ=n                  cada IRQ hasta el cuadro n por stderr (pc, ciclos, contador de la espera)
   Solo con -DRCDBG (rc_host_dbg, comparado con Musashi):
     TRAZA=n                 cada instruccion de los cuadros n y n+1
     MARCAS=pc,pc,...        cuadro y momento del cuadro de la primera vez en cada pc
     RELOJ=1                 pc en cada cuarto de cuadro hasta la primera IRQ
   Compilado con -DHPROF: veces por destino del despacho (rts, rte, jmp indirecto) en $HPROF o work/hprof.txt.
   rc_host_rom (banco.sh con PERFILES=1, -DROMSTAT y romstat.c): bloques de la ROM leidos por fase en $ROMSTAT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "rc_rt.h"
#include "mcs51.h"
#include "gae1.h"
static const char *WORKF(const char *n) { static char b[4][512]; static int k; const char *w = getenv("WR2_WORK"); k = (k + 1) & 3; snprintf(b[k], 512, "%s/%s", w ? w : "../work", n); return b[k]; }

void rc_mem_init(void); void rc_set_io(u16 in0, u16 in1, u16 in2, u16 in3); void rc_snd_vblank(void);
void mcu_boot(u8 *ram, const u8 *code, mcs51_t *c); void mcu_trabajo(u8 *ram, mcs51_t *c, int b); void mcu_cuadro(u8 *ram, mcs51_t *c);
void mcu_eco(u8 *ram);
extern mcs51_t rc_mcu; extern u16 rc_latch; extern int mcu_modelo; extern u32 rc_cuadro; extern unsigned mcu_vigila;
static s32 fase0;
static u8 mcurom[0x8000], rombe[0x100000];
static u16 in0, in1 = 0xFFFF, in2, in3 = 0xFFFF;
static long frame, nframes = 3000;
static const char *outdir = ".";
static FILE *logf, *dumpf, *sndlog;
static long dump_a = -1, dump_b, dump_p = 1;
#ifdef ROMSTAT
void rc_romstat_cuadro(long cuadro), rc_romstat_fin(void);
#endif
#ifdef HPROF
/* perfil del despacho (rts, rte, jmp indirecto): veces por destino, a $HPROF o work/hprof.txt al acabar */
static unsigned hprof[0x10000];
void rc_hprof(u32 k) { hprof[(((k << 16) | (k >> 16)) >> 1) & 0xFFFF]++; }
static void hprof_fin(void)
{
    FILE *f = fopen(getenv("HPROF") ? getenv("HPROF") : WORKF("hprof.txt"), "w"); int i;
    for (i = 0; i < 0x10000; i++) if (hprof[i]) fprintf(f, "%06x %u\n", i * 2, hprof[i]);
    fclose(f);
}
#endif

static void load(const char *fn, u8 *d, int n)
{
    FILE *f = fopen(fn, "rb");
    if (!f || fread(d, 1, n, f) != (size_t)n) { fprintf(stderr, "no %s\n", fn); exit(1); }
    fclose(f);
}
void rc_divzero(u32 pc) { fprintf(stderr, "division por cero en %06X\n", pc); exit(1); }
void rc_bad(u32 pc) { fprintf(stderr, "despacho desconocido: %06X (cuadro %ld)\n", pc, frame); exit(1); }

/* entradas del guion en el cuadro fr (IN0: arriba 01, abajo 02, derecha 04, izquierda 08, acelerar 10, marcha 20
   activa a 1, ADC 40, START 80, DIPSW 2 en la parte alta con un monitor; IN2: moneda 1 en el bit 8) */
static void inputs(long fr)
{
    const char *q;
    in0 = 0xDDDF; in2 = 0xFFDF; in3 = 0xFFFF;
    for (q = getenv("INPUTS"); q && *q; ) {
        long a = strtol(q, (char **)&q, 10), b = a + 1, P = 0, Wd = 0; const char *bt, *c;
        if (*q == '-') b = strtol(q + 1, (char **)&q, 10);
        if (*q == '%') { P = strtol(q + 1, (char **)&q, 10); if (*q == '/') Wd = strtol(q + 1, (char **)&q, 10); }
        if (*q == ':') q++;
        for (bt = q; *q && *q != ','; q++);
        if (fr >= a && fr < b && (!P || (fr - a) % P < Wd)) for (c = bt; c < q; c++) switch (*c) {
            case 'C': in2 &= ~0x100; break; case 'S': in0 &= ~0x80; break; case 'X': in0 &= ~0x10; break;
            case 'G': in0 |= 0x20; break; case 'L': in0 &= ~0x08; break; case 'R': in0 &= ~0x04; break;
            case 'U': in0 &= ~0x01; break; case 'D': in0 &= ~0x02; break; case 'V': in3 &= ~0x01; break;
            }
        if (*q == ',') q++;
    }
}

/* ---- volcados WR2D (big-endian, como lua/wr2ref.lua) */
static void put32(FILE *f, u32 v) { u8 b[4] = { v >> 24, v >> 16, v >> 8, v }; fwrite(b, 1, 4, f); }
static void blk(FILE *f, u32 base, const u8 *p, u32 n, int words)
{
    u32 i; put32(f, base); put32(f, n);
    for (i = 0; i < n; i++) fputc(words ? p[i ^ 1] : p[i], f);
}
static u8 vram_vb[0x10000];       /* copia de la VRAM en el vblank anterior (los sprites que se ven) */
static void dump_state(void)
{
    u8 r[76], io[8]; int i; u32 v;
    u64 t = 0; double tm = frame / 59.1;
    if (!dumpf) { char fn[600]; sprintf(fn, "%s/dumps.bin", outdir); dumpf = fopen(fn, "wb"); }
    fwrite("WR2D", 1, 4, dumpf); put32(dumpf, 1); put32(dumpf, (u32)frame); put32(dumpf, 8);
    memcpy(&t, &tm, 8); put32(dumpf, (u32)(t >> 32)); put32(dumpf, (u32)t);
    blk(dumpf, 0xFE0000, rc_ram, 0x10000, 1); blk(dumpf, 0x200000, rc_vram, 0x10000, 1);
    blk(dumpf, 0x210000, rc_pal, 0x2000, 1); blk(dumpf, 0x212000, rc_xram, 0x2000, 1);
    blk(dumpf, 0x218004, rc_vregs + 4, 6, 1); blk(dumpf, 0x01200000, vram_vb, 0x10000, 1);
    for (i = 0; i < 19; i++) {
        v = i < 8 ? rc_r.d[i] : i < 16 ? rc_r.a[i - 8] : i == 16 ? 0 : i == 17 ? (0x2000 | (rc_r.imask << 8) | (rc_r.fX << 4) | (rc_r.fN << 3) | (rc_r.fZ << 2) | (rc_r.fV << 1) | rc_r.fC) : rc_r.pc;
        r[i * 4] = v >> 24; r[i * 4 + 1] = v >> 16; r[i * 4 + 2] = v >> 8; r[i * 4 + 3] = v;
    }
    blk(dumpf, 0x68000000, r, 76, 0);
    for (i = 0; i < 8; i++) io[i] = rc_iopg[i ^ 1];
    blk(dumpf, 0x300000, io, 8, 0);
}
static u16 W(u32 a) { return *(u16 *)(rc_ram + (a & 0xFFFE)); }
static void log_line(void)
{
    if (!logf) { char fn[600]; sprintf(fn, "%s/log.txt", outdir); logf = fopen(fn, "w");
        fprintf(logf, "# fr pc sr in0 in2 vr0 vr1 scy0 scx0 fec298 fec29c fec146 fec290 fec3c6 fec3ce fefd0a fe80fe fefd14 fe8114\n"); }
    fprintf(logf, "%ld %06x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n", frame,
        rc_r.pc & 0xFFFFFF, 0x2000 | (rc_r.imask << 8) | (rc_r.fX << 4) | (rc_r.fN << 3) | (rc_r.fZ << 2) | (rc_r.fV << 1) | rc_r.fC,
        *(u16 *)rc_iopg, *(u16 *)(rc_iopg + 4), *(u16 *)(rc_vregs + 4), *(u16 *)(rc_vregs + 6),
        *(u16 *)(rc_vram + 0x2800), *(u16 *)(rc_vram + 0x2802), W(0xC298), W(0xC29C), W(0xC146), W(0xC290), W(0xC3C6), W(0xC3CE),
        W(0xFD0A), W(0x80FE), W(0xFD14), W(0x8114));
}
extern void (*mcu_traza)(int k, s32 t);
static void mcu_tr(int k, s32 t)
{
    static double t0; static const char *nom[] = {"peticion", "recogida", "-", "fin", "repite", "escribe"};
    double us = (frame * (double)CYC_FRAME + t) / 13.0;     /* us desde el cuadro 0 (como los tiempos de MAME, salvo el origen) */
    if (k == 0) t0 = us;
    fprintf(stderr, "MCU cuadro %ld %-8s %.1f us (+%.1f)\n", frame, nom[k], us, us - t0);
}
static void snd_suceso(int ch, int t)
{
    if (!sndlog) return;
    if (t < 0) fprintf(sndlog, "%ld %d para\n", frame, ch);
    else { const u16 *r = gae1_reg + ch * 8 + t * 4; fprintf(sndlog, "%ld %d trozo %d %04x %04x %04x\n", frame, ch, t, r[1], r[2], r[3]); }
}

#ifdef RCDBG
/* ================= referencia Musashi (memoria en el mismo orden que el recompilado: byte a en a^1) ================= */
#include "m68k.h"
static u8 m_ram[0x10000] RC_AL, m_vram[0x10000] RC_AL, m_pal[0x2000] RC_AL, m_xram[0x2000] RC_AL, m_vregs[0x1000] RC_AL;
static mcs51_t mcu_ref;
/* puntero a la palabra de la direccion a */
static u8 *m_map(u32 a)
{
    a &= 0xFFFFFE;
    if ((a >> 16) == 0xFE) return m_ram + (a & 0xFFFF);
    if ((a >> 16) == 0x20) return m_vram + (a & 0xFFFF);
    if ((a >> 13) == 0x108) return m_pal + (a & 0x1FFF);
    if ((a >> 13) == 0x109) return m_xram + (a & 0x1FFF);
    if ((a >> 12) == 0x218) return m_vregs + (a & 0xFFF);
    return NULL;
}
/* los registros del GAE1 son estado del chip: Musashi lee los del modelo (rc_mem.c avanza las muestras con el reloj del
   recompilado) y sus escrituras solo las hace el recompilado, que va una instruccion por detras */
static int gae1_win(u32 a) { a &= 0xFFFFFF; return a - 0x202890 < 0x70; }
static u16 rd16(u32 a)
{
    u8 *p;
    a &= 0xFFFFFE;
    if (a < 0x100000) return (rombe[a] << 8) | rombe[a + 1];
    if (gae1_win(a)) return *(u16 *)(rc_vram + (a & 0xFFFF));
    if ((a >> 12) == 0x300) return *(u16 *)(rc_iopg + (a & 0xFFE));
    p = m_map(a);
    return p ? *(u16 *)p : 0xFFFF;
}
unsigned int m68k_read_memory_8(unsigned int a) { u16 v = rd16(a); return (a & 1) ? (v & 0xFF) : (v >> 8); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return (rd16(a) << 16) | rd16(a + 2); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
/* MCUCHK=fichero: instrucciones del 68000 que escriben en la ventana de la MCU (FEFC00-FEFDFF), con las direcciones:
   las que escriben entradas del trabajo llevan gancho en rc68.py (MCU_PCS) para el modelo por acceso */
static unsigned long mchk_n; static u32 mchk[4096][2];
static void mcu_chk(u32 a)
{
    u32 pc, i;
    if ((a & 0xFFFE00) != 0xFEFC00 || !getenv("MCUCHK")) return;
    pc = m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF;
    for (i = 0; i < mchk_n; i++) if (mchk[i][0] == pc && mchk[i][1] == a) return;
    if (mchk_n < 4096) { mchk[mchk_n][0] = pc; mchk[mchk_n][1] = a; mchk_n++; }
}
static void mcu_chk_fin(void)
{
    FILE *f; u32 i;
    if (!getenv("MCUCHK") || !(f = fopen(getenv("MCUCHK"), "w"))) return;
    for (i = 0; i < mchk_n; i++) fprintf(f, "%06x %06x\n", mchk[i][0], mchk[i][1]);
    fclose(f);
}
void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    u8 *p;
    a &= 0xFFFFFE; v &= 0xFFFF;
    mcu_chk(a);
    if (gae1_win(a)) return;
    p = m_map(a);
    if (!p) return;
    *(u16 *)p = v;
    if ((a == 0xFEFC14 || a == 0xFEFC18) && (v & 0xFF) == 1) mcu_trabajo(m_ram, &mcu_ref, a == 0xFEFC18);
    if (a == 0xFEFC08 || a == 0xFEFC0C) mcu_eco(m_ram);
}
void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    u8 *p;
    a &= 0xFFFFFF;
    mcu_chk(a & 0xFFFFFE);
    if (gae1_win(a)) return;
    p = m_map(a);
    if (p) p[(a & 1) ^ 1] = v;
}
void m68k_write_memory_32(unsigned int a, unsigned int v) { m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v & 0xFFFF); }
static int irq_ack(int lvl) { (void)lvl; m68k_set_irq(0); return M68K_INT_ACK_AUTOVECTOR; }

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
    if (getenv("RELOJ") && !frame) {    /* arranque: pc al pasar cada cuarto de cuadro (antes de la primera IRQ) */
        static long q; long c = RC_REL + fase0;
        if (c >= q * (CYC_FRAME / 4)) { fprintf(stderr, "R %ld/4 cuadro %06X ciclo %ld\n", q, pc, c); while (c >= q * (CYC_FRAME / 4)) q++; }
    }
    if (getenv("MARCAS")) {             /* primera vez en cada pc de la lista: cuadro y ciclos del cuadro (0..CYC_FRAME) */
        static u32 m[32]; static int nm = -1; int k;
        if (nm < 0) { const char *q = getenv("MARCAS"); for (nm = 0; *q && nm < 32; ) { m[nm++] = strtoul(q, (char **)&q, 16); if (*q == ',') q++; } }
        for (k = 0; k < nm; k++) if (m[k] == pc) { fprintf(stderr, "M %06X cuadro %ld +%.3f\n", pc, frame, frame ? (CYC_FRAME + RC_REL) / (double)CYC_FRAME : (RC_REL + fase0) / (double)CYC_FRAME); m[k] = 1; }
    }
    if (getenv("TRAZA") && frame >= atol(getenv("TRAZA")) && frame <= atol(getenv("TRAZA")) + 1)
        fprintf(stderr, "T %ld %06X cyr %d fe8114 %04X fe8118 %04X%04X ref %04X%04X\n", frame, pc, rc_r.cyrel,
                *(u16 *)(rc_ram + 0x8114), *(u16 *)(rc_ram + 0x8118), *(u16 *)(rc_ram + 0x811A), *(u16 *)(m_ram + 0x8118), *(u16 *)(m_ram + 0x811A));
    m68k_execute(1);
}
void m68k_check_irq_now(void);
void rc_dbg_irq(void)
{
    u32 sp;
    m68k_set_irq(6); m68k_check_irq_now();
    /* el SR apilado lleva banderas que en el recompilado pueden estar muertas: se copia el de referencia */
    sp = m68k_get_reg(NULL, M68K_REG_A7) & 0xFFFFFF;
    *(u16 *)(rc_ram + (sp & 0xFFFF)) = rd16(sp);
}
void rc_dbg_idle(u32 o, u32 v) { u16 *q = (u16 *)(m_ram + o); u32 x = ((u32)q[0] << 16 | q[1]) + v; q[0] = x >> 16; q[1] = x; }
static void cmp_blk(const char *n, const u8 *a, const u8 *b, u32 len, u32 base)
{
    u32 i;
    for (i = 0; i < len; i++) if (a[i] != b[i] && !(base == 0x200000 && (i ^ 1) - 0x2890 < 0x70)) {
        fprintf(stderr, "%s %06X rc=%02X ref=%02X cuadro %ld\n", n, base + (i ^ 1), a[i], b[i], frame); exit(3); }
}
static void cmp_mem(void)
{
    cmp_blk("RAM", rc_ram, m_ram, 0x10000, 0xFE0000); cmp_blk("VRAM", rc_vram, m_vram, 0x10000, 0x200000);
    cmp_blk("PAL", rc_pal, m_pal, 0x2000, 0x210000); cmp_blk("XRAM", rc_xram, m_xram, 0x2000, 0x212000);
    cmp_blk("VREGS", rc_vregs, m_vregs, 0x1000, 0x218000);
}
#endif

void rc_vblank(void)
{
    /* vblanks pasados desde la IRQ anterior (con la IRQ enmascarada mas de un cuadro, MAME los cuenta todos) */
    long n = 1 + (RC_REL > 0 ? RC_REL / CYC_FRAME : 0);
    static long dsum;
    frame += n; rc_cuadro = frame;
#ifdef ROMSTAT
    rc_romstat_cuadro(frame);
#endif
    if (getenv("VIRQ") && frame < atol(getenv("VIRQ"))) fprintf(stderr, "irq cuadro %ld (+%ld) pc %06X cyr %d fe8118 %08X sp %06X\n", frame, n, rc_r.pc, RC_REL, (W(0x8118) << 16) | W(0x811A), rc_r.a[7]);
    rc_snd_vblank();
    mcu_cuadro(rc_ram, &rc_mcu);
#ifdef RCDBG
    mcu_cuadro(m_ram, &mcu_ref);
    cmp_mem();
#endif
    /* con la IRQ atendida tarde (enmascarada en el vblank) el estado no es el del vblank: sin volcado */
    if (dump_a >= 0 && frame >= dump_a && frame <= dump_b && (frame - dump_a) % dump_p == 0 && rc_r.cyrel < 2000) dump_state();
    if (logf || getenv("LOG")) log_line();
    memcpy(vram_vb, rc_vram, 0x10000);
    /* las del guion de este cuadro: MAME las fija justo tras el frame_done y la IRQ6 ya las ve (el volcado y el
       registro de este cuadro llevan las de antes, como los suyos) */
    inputs(frame); rc_set_io(in0, in1, in2, in3);
    if (getenv("V") && frame % 100 < n) { fprintf(stderr, "cuadro %ld pc %06X ciclos/cuadro %ld\n", frame, rc_r.pc, dsum / 100); dsum = 0; }
    dsum += CYC_FRAME + RC_REL;
    if (frame >= nframes) {
        if (dumpf) fclose(dumpf);
        if (logf) fclose(logf);
        fprintf(stderr, "fin: %ld cuadros\n", frame);
        { extern unsigned mcu_movc_fuera; if (mcu_movc_fuera) fprintf(stderr, "MOVC fuera de las tablas: %u\n", mcu_movc_fuera); }
#ifdef HPROF
        hprof_fin();
#endif
#ifdef ROMSTAT
        rc_romstat_fin();
#endif
#ifdef RCDBG
        fprintf(stderr, "instrucciones comparadas: %lu\n", nck);
        mcu_chk_fin();
#endif
        exit(0);
    }
}

int main(int argc, char **argv)
{
    int i;
    const char *q;
    if (argc > 1) nframes = atol(argv[1]);
    if (argc > 2) outdir = argv[2];
    mkdir(outdir, 0755);
    if ((q = getenv("DUMP"))) {
        dump_a = atol(q); dump_b = strchr(q, '-') ? atol(strchr(q, '-') + 1) : dump_a;
        if (strchr(q, '/')) dump_p = atol(strchr(q, '/') + 1);
    }
    if (getenv("SNDLOG")) { sndlog = fopen(getenv("SNDLOG"), "w"); gae1_suceso = snd_suceso; }
    load(WORKF("prog.bin"), rombe, 0x100000);
    for (i = 0; i < 0x100000; i++) rc_rom[i] = rombe[i ^ 1];
    load(WORKF("mcu.bin"), mcurom, 0x8000);
    rc_mem_init();
    if (getenv("SNDLOG")) gae1_suceso = snd_suceso;
    mcu_boot(rc_ram, mcurom, &rc_mcu);
    memset(&rc_r, 0, sizeof(rc_r));
    rc_r.a[7] = (rombe[0] << 24) | (rombe[1] << 16) | (rombe[2] << 8) | rombe[3];
    rc_r.imask = 7;
    /* el primer vblank al 98 % del cuadro, como en MAME (medido con el bucle de borrado del arranque: mismos cuadros
       que su log); FASE = % del cuadro */
    fase0 = (s32)((long long)CYC_FRAME * (getenv("FASE") ? atol(getenv("FASE")) : 98) / 100);
    rc_r.cyrel = -fase0; rc_dl = 0;
    if (getenv("MCU")) mcu_modelo = atoi(getenv("MCU"));
    if (getenv("MCUV")) mcu_traza = mcu_tr;
    if (getenv("MCUVA")) mcu_vigila = strtoul(getenv("MCUVA"), 0, 16);
#ifdef RCDBG
    mcu_modelo = 0;      /* Musashi hace el trabajo al momento: los dos lados igual */
#endif
#ifdef RCDBG
    mcu_boot(m_ram, mcurom, &mcu_ref);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(irq_ack);
    m68k_pulse_reset();
    m68k_execute(1);     /* consume los ciclos de reset */
#endif
    inputs(0); rc_set_io(in0, in1, in2, in3);
    rc_run((rombe[4] << 24) | (rombe[5] << 16) | (rombe[6] << 8) | rombe[7]);
    return 1;
}

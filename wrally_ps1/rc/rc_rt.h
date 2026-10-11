/* Entorno de ejecucion del codigo 68000 recompilado (World Rally). Memoria en palabras de 16 bits
   en orden nativo: el byte de la direccion a esta en a^1. */
#ifndef RC_RT_H
#define RC_RT_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef short s16; typedef int s32;
typedef unsigned long long u64; typedef long long s64;
typedef struct { u32 pc; void *l; } rc_lab;

extern u8 *rc_rpg[4096], *rc_wpg[4096];
extern u8 rc_rom[0x100000 + 4], rc_ram[0x4000 + 4], rc_pal[0x4000 + 4], rc_spr[0x1000 + 4], rc_vram[0x4000 + 4], rc_vregs[0x1000 + 4];
extern u8 rc_iopg[0x1000 + 4], rc_ffpg[0x1000 + 4];
u32 rc_rd8(u32 a), rc_rd16(u32 a);
void rc_wr8(u32 a, u32 v, u32 pc), rc_wr16(u32 a, u32 v, u32 pc);
void rc_vblank(void);
void rc_bad(u32 pc);
void rc_mcu(void);
void rc_run(u32 start_pc);
extern const u32 rc_ntaken;

/* estado visible desde fuera (depuracion / reanudar) */
typedef struct { u32 d[8], a[8], fX, fN, fZ, fV, fC, imask, pc; s32 cyc, cyc_next; } rc_regs_t;
extern rc_regs_t rc_r;

#if defined(__mips__) && defined(RC_GEN) && !defined(RCDBG)
#define RC_GREGS 1
/* registros del 68000 fijos en registros del MIPS: d0-d7, a7 en s0-s8 (se conservan en las llamadas);
   a0-a6 y Z, C, N en t0-t9 (se guardan a mano alrededor de las llamadas); el contador de ciclos en gp */
register u32 d0 asm("$16"); register u32 d1 asm("$17"); register u32 d2 asm("$18"); register u32 d3 asm("$19");
register u32 d4 asm("$20"); register u32 d5 asm("$21"); register u32 d6 asm("$22"); register u32 d7 asm("$23");
register u32 a7 asm("$30");
register u32 a0 asm("$8"); register u32 a1 asm("$9"); register u32 a2 asm("$10"); register u32 a3 asm("$11");
register u32 a4 asm("$12"); register u32 a5 asm("$13"); register u32 a6 asm("$14");
register u32 fZ asm("$15"); register u32 fC asm("$24"); register u32 fN asm("$25");
register s32 cyc asm("$28");
#define RC_TSAVE do { rc_r.a[0]=a0; rc_r.a[1]=a1; rc_r.a[2]=a2; rc_r.a[3]=a3; rc_r.a[4]=a4; rc_r.a[5]=a5; rc_r.a[6]=a6; \
    rc_r.fZ=fZ; rc_r.fC=fC; rc_r.fN=fN; rc_r.cyc=cyc; } while (0)
#define RC_TLOAD do { a0=rc_r.a[0]; a1=rc_r.a[1]; a2=rc_r.a[2]; a3=rc_r.a[3]; a4=rc_r.a[4]; a5=rc_r.a[5]; a6=rc_r.a[6]; \
    fZ=rc_r.fZ; fC=rc_r.fC; fN=rc_r.fN; cyc=rc_r.cyc; } while (0)
#else
#define RC_TSAVE
#define RC_TLOAD
#endif
#define RC_CALL(x) do { RC_TSAVE; x; RC_TLOAD; } while (0)

#define rom rc_rom
#define ram rc_ram
#define pal rc_pal
#define spr rc_spr
#define MW(r, o) (*(u16 *)((r) + (o)))
#define MB(r, o) ((r)[(o) ^ 1])
#define ML(r, o) (((u32)MW(r, o) << 16) | MW(r, (o) + 2))
#define SETL(r, o, v) do { u32 _v = (v); MW(r, o) = (u16)(_v >> 16); MW(r, (o) + 2) = (u16)_v; } while (0)

/* lectura: todas las paginas apuntan a algo (E/S en una pagina que se rellena cada cuadro, sin mapear =
   pagina de 0xFF); las regiones tienen 4 bytes de margen al final para leer un long sin mirar el cruce */
static inline __attribute__((always_inline)) u32 RD8(u32 a) { return rc_rpg[(a >> 12) & 0xFFF][(a & 0xFFF) ^ 1]; }
static inline __attribute__((always_inline)) u32 RD16(u32 a) { return *(u16 *)(rc_rpg[(a >> 12) & 0xFFF] + (a & 0xFFF)); }
static inline __attribute__((always_inline)) u32 RD32(u32 a) { u16 *q = (u16 *)(rc_rpg[(a >> 12) & 0xFFF] + (a & 0xFFF)); return ((u32)q[0] << 16) | q[1]; }
/* caminos lentos fuera de linea (guardan los registros del 68000 que viven en registros temporales) */
#ifdef RC_GEN
static __attribute__((noinline)) void rc_wr8_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr8(a, v & 0xFF, pc)); }
static __attribute__((noinline)) void rc_wr16_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr16(a, v & 0xFFFF, pc)); }
static __attribute__((noinline)) void rc_wr32_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr16(a, (v >> 16) & 0xFFFF, pc); rc_wr16(a + 2, v & 0xFFFF, pc)); }
static inline __attribute__((always_inline)) void WR8(u32 a, u32 v, u32 pc) { u8 *p = rc_wpg[(a >> 12) & 0xFFF]; if (p) p[(a & 0xFFF) ^ 1] = (u8)v; else rc_wr8_s(a, v, pc); }
static inline __attribute__((always_inline)) void WR16(u32 a, u32 v, u32 pc) { u8 *p = rc_wpg[(a >> 12) & 0xFFF]; if (p) *(u16 *)(p + (a & 0xFFF)) = (u16)v; else rc_wr16_s(a, v, pc); }
static inline __attribute__((always_inline)) void WR32(u32 a, u32 v, u32 pc)
{
    u8 *p = rc_wpg[(a >> 12) & 0xFFF];
    if (p) { u16 *q = (u16 *)(p + (a & 0xFFF)); q[0] = (u16)(v >> 16); q[1] = (u16)v; return; }
    rc_wr32_s(a, v, pc);
}
#endif

static inline void *rc_find(const rc_lab *t, u32 n, u32 pc)
{
    u32 lo = 0, hi = n;
    while (lo < hi) { u32 m = (lo + hi) >> 1; if (t[m].pc < pc) lo = m + 1; else hi = m; }
    return (lo < n && t[lo].pc == pc) ? t[lo].l : 0;
}

/* desplazamientos con cuenta en registro (semantica 68000 / Musashi), sin aritmetica de 64 bits */
#define MSK(b) ((b) == 32 ? 0xFFFFFFFFu : ((1u << (b)) - 1))
static inline u32 SHL(u32 v, u32 c) { return c >= 32 ? 0 : v << c; }
static inline u32 SHR(u32 v, u32 c) { return c >= 32 ? 0 : v >> c; }
static inline u32 sh_lsl(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf)
{
    *vf = 0;
    if (!c) { *cf = 0; return v; }
    if (c <= (u32)b) { *cf = *x = SHR(v, b - c) & 1; return SHL(v, c) & MSK(b); }
    *cf = *x = 0; return 0;
}
static inline u32 sh_lsr(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf)
{
    *vf = 0;
    if (!c) { *cf = 0; return v; }
    if (c <= (u32)b) { *cf = *x = (v >> (c - 1)) & 1; return SHR(v, c); }
    *cf = *x = 0; return 0;
}
static inline u32 sh_asr(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf)
{
    s32 sv = b == 8 ? (s8)v : b == 16 ? (s16)v : (s32)v;
    *vf = 0;
    if (!c) { *cf = 0; return v; }
    if (c < (u32)b) { *cf = *x = (u32)(sv >> (c - 1)) & 1; return (u32)(sv >> c) & MSK(b); }
    *cf = *x = sv < 0; return sv < 0 ? MSK(b) : 0;
}
static inline u32 sh_asl(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf)
{
    if (!c) { *cf = 0; *vf = 0; return v; }
    if (c < (u32)b) {
        u32 mk = (SHL(1u, c + 1) - 1) << (b - c - 1), t = v & mk;
        *vf = t != 0 && t != mk;
        *cf = *x = SHR(v, b - c) & 1; return SHL(v, c) & MSK(b);
    }
    *vf = v != 0;
    *cf = *x = (c == (u32)b) ? (v & 1) : 0; return 0;
}
static inline u32 sh_rol(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf)
{
    u32 cc = c % b, r = v;
    *vf = 0;
    if (!c) { *cf = 0; return v; }
    if (cc) r = ((v << cc) | (v >> (b - cc))) & MSK(b);
    *cf = r & 1; return r;
}
static inline u32 sh_ror(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf)
{
    u32 cc = c % b, r = v;
    *vf = 0;
    if (!c) { *cf = 0; return v; }
    if (cc) r = ((v >> cc) | (v << (b - cc))) & MSK(b);
    *cf = (r >> (b - 1)) & 1; return r;
}
static inline u32 roxl_n(u32 v, u32 c, int b, u32 *x)
{
    u32 i;
    for (i = 0; i < c; i++) { u32 o = (v >> (b - 1)) & 1; v = ((v << 1) | *x) & MSK(b); *x = o; }
    return v;
}
static inline u32 roxr_n(u32 v, u32 c, int b, u32 *x)
{
    u32 i;
    for (i = 0; i < c; i++) { u32 o = v & 1; v = (v >> 1) | (*x << (b - 1)); *x = o; }
    return v;
}
static inline u32 sh_roxl(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf) { *vf = 0; v = roxl_n(v, c, b, x); *cf = *x; return v; }
static inline u32 sh_roxr(u32 v, u32 c, int b, u32 *x, u32 *cf, u32 *vf) { *vf = 0; v = roxr_n(v, c, b, x); *cf = *x; return v; }

#define GETSR() (0x2000u | (imask << 8) | (fX << 4) | (fN << 3) | (fZ << 2) | (fV << 1) | fC)
#define SETSR(v) do { u32 _s = (v); imask = (_s >> 8) & 7; fX = (_s >> 4) & 1; fN = (_s >> 3) & 1; fZ = (_s >> 2) & 1; fV = (_s >> 1) & 1; fC = _s & 1; } while (0)
#define SETCCR(v) do { u32 _s = (v); fX = (_s >> 4) & 1; fN = (_s >> 3) & 1; fZ = (_s >> 2) & 1; fV = (_s >> 1) & 1; fC = _s & 1; } while (0)

#define RC_SYNC do { rc_r.d[0]=d0; rc_r.d[1]=d1; rc_r.d[2]=d2; rc_r.d[3]=d3; rc_r.d[4]=d4; rc_r.d[5]=d5; rc_r.d[6]=d6; rc_r.d[7]=d7; \
    rc_r.a[0]=a0; rc_r.a[1]=a1; rc_r.a[2]=a2; rc_r.a[3]=a3; rc_r.a[4]=a4; rc_r.a[5]=a5; rc_r.a[6]=a6; rc_r.a[7]=a7; \
    rc_r.fX=fX; rc_r.fN=fN; rc_r.fZ=fZ; rc_r.fV=fV; rc_r.fC=fC; rc_r.imask=imask; rc_r.cyc=cyc; rc_r.cyc_next=cyc_next; } while (0)

#ifdef RC_GREGS
#define RC_LOCALS u32 fX = rc_r.fX, fV = rc_r.fV, imask = rc_r.imask; s32 cyc_next = rc_r.cyc_next; \
    u32 vs, vd, vr, eas, ead, dpc; void *dl;
#define RC_ENTER d0 = rc_r.d[0]; d1 = rc_r.d[1]; d2 = rc_r.d[2]; d3 = rc_r.d[3]; d4 = rc_r.d[4]; d5 = rc_r.d[5]; \
    d6 = rc_r.d[6]; d7 = rc_r.d[7]; a7 = rc_r.a[7]; cyc = rc_r.cyc; RC_TLOAD;
#else
#define RC_LOCALS u32 d0 = rc_r.d[0], d1 = rc_r.d[1], d2 = rc_r.d[2], d3 = rc_r.d[3], d4 = rc_r.d[4], d5 = rc_r.d[5], d6 = rc_r.d[6], d7 = rc_r.d[7]; \
    u32 a0 = rc_r.a[0], a1 = rc_r.a[1], a2 = rc_r.a[2], a3 = rc_r.a[3], a4 = rc_r.a[4], a5 = rc_r.a[5], a6 = rc_r.a[6], a7 = rc_r.a[7]; \
    u32 fX = rc_r.fX, fN = rc_r.fN, fZ = rc_r.fZ, fV = rc_r.fV, fC = rc_r.fC, imask = rc_r.imask; \
    s32 cyc = rc_r.cyc, cyc_next = rc_r.cyc_next; u32 vs, vd, vr, eas, ead, dpc; void *dl;
#define RC_ENTER
#endif
#define DISPATCH(x) do { dpc = (x); dl = rc_find(tab, sizeof(tab) / sizeof(tab[0]), dpc); if (!dl) goto bad_dispatch; goto *dl; } while (0)
#define FELL(x) DISPATCH(x)
#define CYC(n) cyc += (n)
#define CYC_FRAME 200000
#define IDLEHINT(cond) do { if ((cond) && cyc < cyc_next) cyc = cyc_next; } while (0)
#define IRQCHK(t) do { if (cyc >= cyc_next && imask < 6) { dpc = (t); goto irq_take; } } while (0)
#define DIVZERO(pc) do { RC_SYNC; rc_divzero(pc); } while (0)
void rc_divzero(u32 pc);

#ifdef RCDBG
void rc_ck(u32 pc, u32 live);
void rc_dbg_irq(void);
#define CK(pc, lv) do { RC_SYNC; rc_ck(pc, lv); } while (0)
#define IRQ_DBG() rc_dbg_irq()
#else
#define CK(pc, lv)
#define IRQ_DBG()
#endif

#define RC_EXIT_CODE \
irq_take: \
    RC_SYNC; rc_vblank(); RC_TLOAD; \
    cyc -= cyc_next; cyc_next = CYC_FRAME; while (cyc_next <= cyc) cyc_next += CYC_FRAME; \
    { u32 sr = GETSR(); a7 -= 4; WR32(a7, dpc, 0); a7 -= 2; WR16(a7, sr, 0); imask = 6; } \
    IRQ_DBG(); \
    DISPATCH(RC_IRQ6); \
bad_dispatch: \
    RC_SYNC; rc_r.pc = dpc; rc_bad(dpc); return;
#endif

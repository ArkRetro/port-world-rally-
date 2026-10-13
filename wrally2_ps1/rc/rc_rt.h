/* Entorno de ejecucion del codigo 68000 recompilado (World Rally 2). Memoria en palabras de 16 bits
   en orden nativo: el byte de la direccion a esta en a^1. Copia del de World Rally 1 (wrally_ps1/rc/rc_rt.h) sin el
   cifrado de la VRAM y con el mapa del WR2:
     0x000000-0x0FFFFF ROM          0x200000-0x20FFFF VRAM (64 KB; 0x202890-0x2028FF registros del GAE1)
     0x210000-0x211FFF paleta       0x212000-0x213FFF RAM extra     0x218000 registros de video (0x218004-9)
     0x300000-0x300007 entradas     0x400000-0x40003F latch         0xFE0000-0xFEFFFF RAM (0xFE8000+ con la MCU) */
#ifndef RC_RT_H
#define RC_RT_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef short s16; typedef int s32;
typedef unsigned long long u64; typedef long long s64;
typedef struct { u32 pc; void *l; } rc_lab;

extern u8 *rc_rpg[4096], *rc_wpg[4096];
/* alineadas tambien en la declaracion: si no, gcc hace los accesos de 16 bits a direccion fija byte a byte */
#define RC_AL __attribute__((aligned(4)))
extern u8 rc_rom[0x100000 + 4] RC_AL, rc_ram[0x10000 + 4] RC_AL, rc_pal[0x2000 + 4] RC_AL, rc_xram[0x2000 + 4] RC_AL;
extern u8 rc_vram[0x10000 + 4] RC_AL, rc_vregs[0x1000 + 4] RC_AL, rc_iopg[0x1000 + 4] RC_AL, rc_ffpg[0x1000 + 4] RC_AL;
u32 rc_rd8(u32 a), rc_rd16(u32 a);
void rc_wr8(u32 a, u32 v, u32 pc), rc_wr16(u32 a, u32 v, u32 pc), rc_wr32(u32 a, u32 v, u32 pc);
void rc_vblank(void);
void rc_bad(u32 pc);
void rc_mcu_a(void), rc_mcu_b(void), rc_mcu_eco(void);
void rc_run(u32 start_pc);
extern const u32 rc_ntaken;

/* estado visible desde fuera (depuracion / reanudar). Dentro de rc_run el contador va relativo al proximo plazo
   (cyr = ahora - plazo): la comprobacion de IRQ y sucesos es un signo. El plazo es el primero de: el vblank (si la IRQ
   no esta enmascarada) y el proximo suceso (la MCU con su tiempo); rc_dl = plazo - proximo vblank. Fuera de rc_run
   (tras RC_SYNC), RC_REL = ciclos desde el proximo vblank (>= 0: ya ha pasado) */
typedef struct { u32 d[8], a[8], fX, fN, fZ, fV, fC, imask, pc; s32 cyc, cyc_next, cyrel; } rc_regs_t;
extern rc_regs_t rc_r;
extern s32 rc_dl;
#define RC_REL (rc_r.cyrel + rc_dl)
void rc_plazo(void);        /* rc_mem.c: recalcula el plazo (con rc_r.imask) */
int rc_sucesos(void);       /* rc_mem.c: hace los sucesos vencidos; 1 = toca la IRQ del vblank */
void rc_vb_cuadro(void);    /* rc_mem.c: tomada la IRQ, el contador pasa al vblank siguiente */

#if defined(__mips__) && defined(RC_GEN) && !defined(RCDBG)
#define RC_GREGS 1
/* registros del 68000 fijos en registros del MIPS: d0-d7, a7 en s0-s8 (se conservan en las llamadas);
   a0-a5, Z y C en t0-t5, t7, t8 (se guardan a mano alrededor de las llamadas); el contador de ciclos en gp.
   En t9 la base de la RAM del 68000 (los accesos a direccion fija son una sola instruccion) y en t6 la de la
   ROM; a6 y N, que el juego apenas usa (menos de 100 veces por cuadro), van en variables locales. X, N y V en la
   pila siempre (volatile): si no, gcc los deja en registros a lo largo de los bucles y saca a la pila otros valores
   que se usan en cada vuelta */
register u32 d0 asm("$16"); register u32 d1 asm("$17"); register u32 d2 asm("$18"); register u32 d3 asm("$19");
register u32 d4 asm("$20"); register u32 d5 asm("$21"); register u32 d6 asm("$22"); register u32 d7 asm("$23");
register u32 a7 asm("$30");
register u32 a0 asm("$8"); register u32 a1 asm("$9"); register u32 a2 asm("$10"); register u32 a3 asm("$11");
register u32 a4 asm("$12"); register u32 a5 asm("$13"); register u8 *rc_rbr asm("$14");
register u32 fZ asm("$15"); register u32 fC asm("$24"); register u8 *rc_rb asm("$25");
register s32 cyr asm("$28");
#define RC_TSAVE do { rc_r.a[0]=a0; rc_r.a[1]=a1; rc_r.a[2]=a2; rc_r.a[3]=a3; rc_r.a[4]=a4; rc_r.a[5]=a5; \
    rc_r.fZ=fZ; rc_r.fC=fC; rc_r.cyrel=cyr; } while (0)
#define RC_TLOAD do { a0=rc_r.a[0]; a1=rc_r.a[1]; a2=rc_r.a[2]; a3=rc_r.a[3]; a4=rc_r.a[4]; a5=rc_r.a[5]; \
    fZ=rc_r.fZ; fC=rc_r.fC; cyr=rc_r.cyrel; rc_rb = rc_ram; rc_rbr = rc_rom; } while (0)
#define RAMB rc_rb
#define ROMB rc_rbr
#else
/* el contador de ciclos en rc_r, a la vista de lo que se llama (rc_mem.c: momento de las escrituras al GAE1) */
#define cyr rc_r.cyrel
#define RC_TSAVE
#define RC_TLOAD
#define RAMB rc_ram
#define ROMB rc_rom
#endif
#define RC_CALL(x) do { RC_TSAVE; x; RC_TLOAD; } while (0)

#define rom rc_rom
#define ram RAMB
#define pal rc_pal
#define vram rc_vram
#define vregs rc_vregs
#define MW(r, o) (*(u16 *)((r) + (o)))
#define MB(r, o) ((r)[(o) ^ 1])
#define ML(r, o) (((u32)MW(r, o) << 16) | MW(r, (o) + 2))
/* long en desplazamiento multiplo de 4: una sola lectura de 32 bits y cambio de palabras */
static inline __attribute__((always_inline)) u32 RC_SW16(u32 x) { return (x << 16) | (x >> 16); }
#define MLA(r, o) RC_SW16(*(u32 *)((r) + (o)))
/* base opaca para gcc: si no, calcula base+2, base+4... como constantes aparte y repite la suma del
   desplazamiento en cada palabra */
static inline __attribute__((always_inline)) u8 *RC_OPQ(u8 *p) { __asm__("" : "+r"(p)); return p; }
#define SETL(r, o, v) do { u32 _v = (v); MW(r, o) = (u16)(_v >> 16); MW(r, (o) + 2) = (u16)_v; } while (0)

#ifdef RCREG
/* perfil de regiones (rc/regiones.sh): por instruccion, las regiones que lee (bits 0-6) y escribe (8-14); los
   bits 7 y 15, un acceso de 32 bits no alineado a 4. Con el byte alto distinto de 0 no hay region prevista.
   rc_regck: veces que se ejecuta cada instruccion, en total y como mucho en un cuadro (vueltas de los bucles, para
   elegir los bucles rapidos); rc_regbk: saltos atras tomados (para saber cuantas veces se entra en cada bucle) */
extern u32 rc_cur; extern u16 rc_regmask[0x10000];
void rc_regck(u32 pc), rc_regbk(u32 pc);
#define REGBK(pc) rc_regbk(pc)
static inline u32 rc_regbit(u32 a)
{
    if (a >> 24) return 64;
    if (a < 0x100000) return 1;                   /* rom */
    if ((a >> 16) == 0xFE) return 2;              /* ram */
    if ((a >> 16) == 0x20) return (a & 0xFF80) == 0x2880 ? 64 : 4;   /* vram (los registros del GAE1 aparte) */
    if ((a >> 13) == 0x108) return 8;             /* pal */
    if ((a >> 12) == 0x218) return 32;            /* vregs */
    return 64;
}
#define REGREC(a, w) (rc_regmask[(rc_cur >> 1) & 0xFFFF] |= rc_regbit(a) << ((w) * 8))
#define REGREC32(a, w) (rc_regmask[(rc_cur >> 1) & 0xFFFF] |= (rc_regbit(a) | ((a) & 2 ? 0x80 : 0)) << ((w) * 8))
#else
#define REGREC(a, w) ((void)0)
#define REGREC32(a, w) ((void)0)
#define REGBK(pc)
#endif
/* lectura: todas las paginas apuntan a algo (E/S en una pagina que se rellena cada cuadro, sin mapear =
   pagina de 0xFF); las regiones tienen 4 bytes de margen al final para leer un long sin mirar el cruce */
static inline __attribute__((always_inline)) u32 RD8(u32 a) { REGREC(a, 0); return rc_rpg[(a >> 12) & 0xFFF][(a & 0xFFF) ^ 1]; }
static inline __attribute__((always_inline)) u32 RD16(u32 a) { REGREC(a, 0); return *(u16 *)(rc_rpg[(a >> 12) & 0xFFF] + (a & 0xFFF)); }
static inline __attribute__((always_inline)) u32 RD32(u32 a) { u16 *q; REGREC32(a, 0); q = (u16 *)(rc_rpg[(a >> 12) & 0xFFF] + (a & 0xFFF)); return ((u32)q[0] << 16) | q[1]; }
/* caminos lentos fuera de linea: escrituras a paginas sin puntero (la de la VRAM con los registros del GAE1, latch,
   lo que no esta mapeado), en rc_mem.c (guardan los registros del 68000 que viven en temporales) */
#ifdef RC_GEN
static __attribute__((noinline)) void rc_wr8_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr8(a, v & 0xFF, pc)); }
static __attribute__((noinline)) void rc_wr16_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr16(a, v & 0xFFFF, pc)); }
static __attribute__((noinline)) void rc_wr32_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr32(a, v, pc)); }
static inline __attribute__((always_inline)) void WR8(u32 a, u32 v, u32 pc) { u8 *p; REGREC(a, 1); p = rc_wpg[(a >> 12) & 0xFFF]; if (p) p[(a & 0xFFF) ^ 1] = (u8)v; else rc_wr8_s(a, v, pc); }
static inline __attribute__((always_inline)) void WR16(u32 a, u32 v, u32 pc) { u8 *p; REGREC(a, 1); p = rc_wpg[(a >> 12) & 0xFFF]; if (p) *(u16 *)(p + (a & 0xFFF)) = (u16)v; else rc_wr16_s(a, v, pc); }
static inline __attribute__((always_inline)) void WR32(u32 a, u32 v, u32 pc)
{
    u8 *p; REGREC32(a, 1); p = rc_wpg[(a >> 12) & 0xFFF];
    if (p) { u16 *q = (u16 *)(p + (a & 0xFFF)); q[0] = (u16)(v >> 16); q[1] = (u16)v; return; }
    rc_wr32_s(a, v, pc);
}
/* accesos generales fuera de linea (para cuando falla la region prevista) */
static __attribute__((noinline, cold)) u32 rc_rdg8(u32 a) { return RD8(a); }
static __attribute__((noinline, cold)) u32 rc_rdg16(u32 a) { return RD16(a); }
static __attribute__((noinline, cold)) u32 rc_rdg32(u32 a) { return RD32(a); }
static __attribute__((noinline, cold)) void rc_wrg8(u32 a, u32 v, u32 pc) { WR8(a, v, pc); }
static __attribute__((noinline, cold)) void rc_wrg16(u32 a, u32 v, u32 pc) { WR16(a, v, pc); }
static __attribute__((noinline, cold)) void rc_wrg32(u32 a, u32 v, u32 pc) { WR32(a, v, pc); }
/* accesos con region prevista para cada instruccion (perfil en rc/regiones.txt): comprobacion de rango en linea
   y, si falla, el acceso general (exacto siempre). El camino rapido pide ademas el byte alto de la direccion a 0
   (el juego no usa otra cosa; si no, solo se pierde velocidad): la comprobacion es un desplazamiento y una
   comparacion, y en la ROM la direccion es ya el desplazamiento. RG = esta en la region, RO = desplazamiento,
   RB = memoria, RBO = memoria con la base opaca para gcc (punteros), RS = tamano (los bloques de movem
   comprueban que caben enteros). La VRAM deja fuera 0x202880-0x2028FF (registros del GAE1 desde 0x202890): esas
   escrituras van por el acceso general */
#define RG_ram(a) (((a) >> 16) == 0xFE)
#define RG_rom(a) (((a) >> 20) == 0)
#define RG_pal(a) (((a) >> 13) == 0x108)
#define RG_vram(a) (((a) >> 16) == 0x20 && ((a) & 0xFF80) != 0x2880)
#define RG_vregs(a) (((a) >> 12) == 0x218)
#define RO_ram(a) ((a) & 0xFFFF)
#define RO_rom(a) (a)
#define RO_pal(a) ((a) & 0x1FFF)
#define RO_vram(a) ((a) & 0xFFFF)
#define RO_vregs(a) ((a) & 0xFFF)
#define RB_ram RAMB
#define RB_rom ROMB
#define RB_pal rc_pal
#define RB_vram rc_vram
#define RB_vregs rc_vregs
#define RBO_ram RAMB
#define RBO_rom ROMB
#define RBO_pal RC_OPQ(rc_pal)
#define RBO_vram RC_OPQ(rc_vram)
#define RBO_vregs RC_OPQ(rc_vregs)
#define RS_ram 0x10000
#define RS_rom 0x100000
#define RS_pal 0x2000
#define RS_vram 0x10000
#define RS_vregs 0x1000
#ifdef RCREG
#define RDP8(r, a) RD8(a)
#define RDP16(r, a) RD16(a)
#define RDP32(r, a) RD32(a)
#define WRP8(r, a, v, pc) WR8(a, v, pc)
#define WRP16(r, a, v, pc) WR16(a, v, pc)
#define WRP32(r, a, v, pc) WR32(a, v, pc)
#define RDA32(r, a) RD32(a)
#define RBLK(r, a, n) 0
#else
#define RDP8(r, a) (RG_##r(a) ? (u32)MB(RB_##r, RO_##r(a)) : rc_rdg8(a))
#define RDP16(r, a) (RG_##r(a) ? (u32)MW(RB_##r, RO_##r(a)) : rc_rdg16(a))
#define RDP32(r, a) (RG_##r(a) ? ML(RBO_##r + RO_##r(a), 0) : rc_rdg32(a))
#define WRP8(r, a, v, pc) do { if (RG_##r(a)) MB(RB_##r, RO_##r(a)) = (u8)(v); else rc_wrg8(a, v, pc); } while (0)
#define WRP16(r, a, v, pc) do { if (RG_##r(a)) MW(RB_##r, RO_##r(a)) = (u16)(v); else rc_wrg16(a, v, pc); } while (0)
#define WRP32(r, a, v, pc) do { if (RG_##r(a)) SETL(RBO_##r + RO_##r(a), 0, v); else rc_wrg32(a, v, pc); } while (0)
/* el final del bloque tambien en la region (la VRAM tiene el hueco del GAE1 dentro) */
#define RBLK(r, a, n) (RG_##r(a) && RO_##r(a) <= RS_##r - (n) && RG_##r((a) + (n) - 1))
/* long con lecturas siempre alineadas a 4 en el perfil: una sola lectura de 32 bits (si no, el acceso general) */
#define RDA32(r, a) ((RG_##r(a) && !((a) & 2)) ? MLA(RBO_##r + RO_##r(a), 0) : rc_rdg32(a))
#endif
#define RPTR(r, a) (RBO_##r + RO_##r(a))
/* copia rapida de un bucle (rc68.py, bucles rapidos): accesos por An de induccion ya comprobados a la entrada del
   bucle para todas las vueltas, sin mirar la region */
#ifdef RCREG
#define RDU8(r, a) RD8(a)
#define RDU16(r, a) RD16(a)
#define RDU32(r, a) RD32(a)
#define RDU32A(r, a) RD32(a)
#define WRU8(r, a, v) WR8(a, v, 0)
#define WRU16(r, a, v) WR16(a, v, 0)
#define WRU32(r, a, v) WR32(a, v, 0)
#else
#define RDU8(r, a) ((u32)MB(RB_##r, RO_##r(a)))
#define RDU16(r, a) ((u32)MW(RB_##r, RO_##r(a)))
#define RDU32(r, a) ML(RBO_##r + RO_##r(a), 0)
#define RDU32A(r, a) MLA(RBO_##r + RO_##r(a), 0)
#define WRU8(r, a, v) (MB(RB_##r, RO_##r(a)) = (u8)(v))
#define WRU16(r, a, v) (MW(RB_##r, RO_##r(a)) = (u16)(v))
#define WRU32(r, a, v) SETL(RBO_##r + RO_##r(a), 0, v)
#endif
#endif

#ifdef RC_GEN
/* despacho general (rts, rte, jmp indirecto): tabla hash de etiquetas por el pc con las palabras cambiadas
   (k), sondeo lineal hasta una entrada vacia (clave 0). La genera rc68.py (RC_HN, RC_HS). rts mira la primera
   entrada en linea y solo llama aqui si no esta */
/* RC_HPROF(k): perfil de destinos del despacho en el banco (rc_host -DHPROF, fichero $HPROF o work/hprof.txt) */
#ifdef HPROF
void rc_hprof(u32 k);
#define RC_HPROF(k) rc_hprof(k)
#else
#define RC_HPROF(k)
#endif
static __attribute__((noinline)) void *rc_hfind(const rc_lab *e, u32 k)
{
    e += (k ^ (k >> RC_HS)) & (RC_HN - 1);
    while (e->pc != k) { if (!e->pc) return 0; e++; }
    return e->l;
}
/* direccion de vuelta de bsr/jsr: con a7 en registro, una funcion comun (caliente en la cache de instrucciones) */
#ifdef RC_GREGS
static __attribute__((noinline)) void rc_push(u32 r) { a7 -= 4; WRP32(ram, a7, r, 0); }
#define RC_PUSH(r, pc) rc_push(r)
#else
#define RC_PUSH(r, pc) do { a7 -= 4; WRP32(ram, a7, r, pc); } while (0)
#endif
/* long de la RAM del 68000 tal como esta en memoria (palabras cambiadas): alineado a 4, de una vez */
#define RAW32(a) (*(u32 *)(RAMB + ((a) & 0xFFFC)))
#endif

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
    rc_r.fX=fX; rc_r.fN=fN; rc_r.fZ=fZ; rc_r.fV=fV; rc_r.fC=fC; rc_r.imask=imask; \
    rc_r.cyrel=cyr; } while (0)

/* copia rapida de bucles: desplazamiento del contador de ciclos y parte alta de los Dn partidos (rc68.py). Solo se
   usan al entrar y al salir: en la pila (volatile), sin quitar registros al cuerpo del bucle */
#define RC_FLOCALS volatile s32 fb = 0; volatile u32 dh0 = 0, dh1 = 0, dh2 = 0, dh3 = 0, dh4 = 0, dh5 = 0, dh6 = 0, dh7 = 0;
#ifdef RC_GREGS
#define RC_LOCALS volatile u32 fX = rc_r.fX, fV = rc_r.fV, fN = rc_r.fN; u32 a6 = rc_r.a[6], imask = rc_r.imask; \
    u32 vs, vd, vr, eas, ead, dpc; void *dl; RC_FLOCALS
#define RC_ENTER d0 = rc_r.d[0]; d1 = rc_r.d[1]; d2 = rc_r.d[2]; d3 = rc_r.d[3]; d4 = rc_r.d[4]; d5 = rc_r.d[5]; \
    d6 = rc_r.d[6]; d7 = rc_r.d[7]; a7 = rc_r.a[7]; RC_TLOAD;
#else
#define RC_LOCALS u32 d0 = rc_r.d[0], d1 = rc_r.d[1], d2 = rc_r.d[2], d3 = rc_r.d[3], d4 = rc_r.d[4], d5 = rc_r.d[5], d6 = rc_r.d[6], d7 = rc_r.d[7]; \
    u32 a0 = rc_r.a[0], a1 = rc_r.a[1], a2 = rc_r.a[2], a3 = rc_r.a[3], a4 = rc_r.a[4], a5 = rc_r.a[5], a6 = rc_r.a[6], a7 = rc_r.a[7]; \
    u32 fX = rc_r.fX, fN = rc_r.fN, fZ = rc_r.fZ, fV = rc_r.fV, fC = rc_r.fC, imask = rc_r.imask; \
    u32 vs, vd, vr, eas, ead, dpc; void *dl; RC_FLOCALS
#define RC_ENTER
#endif
/* sin sacar invariantes de los bucles: en las copias rapidas (bucles sin etiquetas con direccion tomada) gcc sacaria
   las direcciones constantes (paginas, regiones) y, sin registros libres, las dejaria en la pila: una lectura de la
   RAM en cada uso en vez de rehacerlas */
#if defined(__GNUC__) && !defined(__clang__)
#define RC_RUN_ATTR __attribute__((optimize("no-tree-loop-im", "no-move-loop-invariants")))
#else
#define RC_RUN_ATTR
#endif
/* despacho indirecto y rts: bloques comunes al final de rc_run (un salto en cada sitio) */
#define DISPATCH(x) do { dpc = (x); goto rc_disp; } while (0)
#define FELL(x) DISPATCH(x)
#define RTS() goto rc_rts
#define CYC(n) cyr += (n)
/* justo antes de la comprobacion de IRQ: gcc lo haria en otro registro y lo copiaria a gp */
#ifdef RC_GREGS
#define CYCB(n) __asm__ ("addiu %0, %0, %1" : "+r"(cyr) : "i"(n))
#else
#define CYCB(n) CYC(n)
#endif
/* ciclos del 68000 por cuadro: 13 MHz / 59,1 Hz (en ciclos del modelo de rc68.py, cycles()) */
#ifndef CYC_FRAME
#define CYC_FRAME 219966
#endif
#define IDLEHINT(cond) do { if ((cond) && cyr < 0) cyr = 0; } while (0)
/* espera con contador (rc68.py, ESPERAS): las vueltas que faltan hasta la IRQ de una vez, cada una suma 1 al long de
   la RAM en o y n ciclos; queda igual que dandolas */
#ifdef RCDBG
void rc_dbg_idle(u32 o, u32 v);     /* la referencia (Musashi) da las vueltas de una vez como el recompilado */
#define IDLEDBG(o, v) rc_dbg_idle(o, v)
#else
#define IDLEDBG(o, v)
#endif
#define IDLECNT(cond, o, n) do { if ((cond) && cyr < 0) { u32 _iv = (u32)(-cyr + (n) - 1) / (n); \
    SETL(RAMB, o, ML(RAMB, o) + _iv); cyr += (s32)(_iv * (n)); IDLEDBG(o, _iv); } } while (0)
#define IRQCHK(t) do { if (cyr >= 0) { dpc = (t); goto irq_take; } } while (0)
/* tras cambiar la mascara (move to SR, andi/eori SR, rte): el plazo cambia (la IRQ pendiente se atiende ya) */
#define RC_PLAZO(t) do { rc_r.imask = imask; RC_CALL(rc_plazo()); IRQCHK(t); } while (0)
#define DIVZERO(pc) do { RC_SYNC; rc_divzero(pc); } while (0)
void rc_divzero(u32 pc);

#ifdef RCDBG
void rc_ck(u32 pc, u32 live);
void rc_dbg_irq(void);
#define CK(pc, lv) do { RC_SYNC; rc_ck(pc, lv); } while (0)
#define IRQ_DBG() rc_dbg_irq()
/* en la copia rapida de un bucle, la comprobacion ve los Dn partidos (mascara m) enteros: RC_FJ(m); CK(...); RC_FS(m) */
#define RC_FJOIN(m) do { if ((m) & 1) d0 |= dh0; if ((m) & 2) d1 |= dh1; if ((m) & 4) d2 |= dh2; if ((m) & 8) d3 |= dh3; \
    if ((m) & 16) d4 |= dh4; if ((m) & 32) d5 |= dh5; if ((m) & 64) d6 |= dh6; if ((m) & 128) d7 |= dh7; } while (0)
#define RC_FSPLIT(m) do { if ((m) & 1) d0 &= 0xFFFFu; if ((m) & 2) d1 &= 0xFFFFu; if ((m) & 4) d2 &= 0xFFFFu; if ((m) & 8) d3 &= 0xFFFFu; \
    if ((m) & 16) d4 &= 0xFFFFu; if ((m) & 32) d5 &= 0xFFFFu; if ((m) & 64) d6 &= 0xFFFFu; if ((m) & 128) d7 &= 0xFFFFu; } while (0)
#define RC_FJ(m) RC_FJOIN(m)
#define RC_FS(m) RC_FSPLIT(m)
#else
#ifdef RCREG
#define CK(pc, lv) rc_regck(pc)
#else
#define CK(pc, lv)
#endif
#define RC_FJ(m)
#define RC_FS(m)
#define IRQ_DBG()
#endif

/* irq_take: vencido el plazo, los sucesos; si toca la IRQ del vblank, rc_vblank y el contador pasa al vblank siguiente;
   si no, se sigue donde se estaba */
#define RC_EXIT_CODE \
irq_take: \
    RC_SYNC; rc_r.pc = dpc; \
    if (!rc_sucesos()) { RC_TLOAD; DISPATCH(dpc); } \
    rc_vblank(); rc_vb_cuadro(); RC_TLOAD; \
    { u32 sr = GETSR(); a7 -= 4; WRP32(ram, a7, dpc, 0); a7 -= 2; WRP16(ram, a7, sr, 0); imask = 6; } \
    rc_r.imask = 6; RC_CALL(rc_plazo()); \
    IRQ_DBG(); \
    DISPATCH(RC_IRQ6); \
rc_rts: \
    { u32 _k; \
      if (RG_ram(a7)) { if (!(a7 & 2)) _k = RAW32(a7); else { u8 *_q = RAMB + RO_ram(a7); _k = ((u32)MW(_q, 2) << 16) | MW(_q, 0); } } \
      else _k = RC_SW16(rc_rdg32(a7)); \
      a7 += 4; RC_HPROF(_k); \
      { const rc_lab *_e = ht + ((_k ^ (_k >> RC_HS)) & (RC_HN - 1)); if (_e->pc == _k) goto *_e->l; } \
      dl = rc_hfind(ht, _k); if (dl) goto *dl; dpc = RC_SW16(_k); goto bad_dispatch; } \
rc_disp: \
    RC_HPROF(RC_SW16(dpc)); dl = rc_hfind(ht, RC_SW16(dpc)); if (!dl) goto bad_dispatch; goto *dl; \
bad_dispatch: \
    RC_SYNC; rc_r.pc = dpc; rc_bad(dpc); return;
#endif

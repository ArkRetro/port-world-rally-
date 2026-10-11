/* Entorno de ejecucion del codigo 68000 recompilado (World Rally). Memoria en palabras de 16 bits
   en orden nativo: el byte de la direccion a esta en a^1. */
#ifndef RC_RT_H
#define RC_RT_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef short s16; typedef int s32;
typedef unsigned long long u64; typedef long long s64;
typedef struct { u32 pc; void *l; } rc_lab;

extern u8 *rc_rpg[4096], *rc_wpg[4096];
/* alineadas tambien en la declaracion: si no, gcc hace los accesos de 16 bits a direccion fija byte a byte */
#define RC_AL __attribute__((aligned(4)))
extern u8 rc_rom[0x100000 + 4] RC_AL, rc_ram[0x4000 + 4] RC_AL, rc_pal[0x4000 + 4] RC_AL, rc_spr[0x1000 + 4] RC_AL;
extern u8 rc_vram[0x4000 + 4] RC_AL, rc_vregs[0x1000 + 4] RC_AL, rc_iopg[0x1000 + 4] RC_AL, rc_ffpg[0x1000 + 4] RC_AL;
u32 rc_rd8(u32 a), rc_rd16(u32 a);
void rc_wr8(u32 a, u32 v, u32 pc), rc_wr16(u32 a, u32 v, u32 pc);
void rc_vblank(void);
void rc_bad(u32 pc);
void rc_mcu(void);
void rc_run(u32 start_pc);
extern const u32 rc_ntaken;

/* estado visible desde fuera (depuracion / reanudar). Dentro de rc_run el contador va relativo al proximo
   vblank (cyr = cyc - cyc_next): la comprobacion de IRQ es un signo, sin leer cyc_next */
typedef struct { u32 d[8], a[8], fX, fN, fZ, fV, fC, imask, pc; s32 cyc, cyc_next, cyr; } rc_regs_t;
extern rc_regs_t rc_r;

#if defined(__mips__) && defined(RC_GEN) && !defined(RCDBG)
#define RC_GREGS 1
/* registros del 68000 fijos en registros del MIPS: d0-d7, a7 en s0-s8 (se conservan en las llamadas);
   a0-a6, Z y C en t0-t8 (se guardan a mano alrededor de las llamadas); el contador de ciclos en gp. En t9 la
   base de la RAM del 68000 (los accesos a direccion fija son una sola instruccion); N, que se usa poco, va en
   una variable local */
register u32 d0 asm("$16"); register u32 d1 asm("$17"); register u32 d2 asm("$18"); register u32 d3 asm("$19");
register u32 d4 asm("$20"); register u32 d5 asm("$21"); register u32 d6 asm("$22"); register u32 d7 asm("$23");
register u32 a7 asm("$30");
register u32 a0 asm("$8"); register u32 a1 asm("$9"); register u32 a2 asm("$10"); register u32 a3 asm("$11");
register u32 a4 asm("$12"); register u32 a5 asm("$13"); register u32 a6 asm("$14");
register u32 fZ asm("$15"); register u32 fC asm("$24"); register u8 *rc_rb asm("$25");
register s32 cyr asm("$28");
#define RC_TSAVE do { rc_r.a[0]=a0; rc_r.a[1]=a1; rc_r.a[2]=a2; rc_r.a[3]=a3; rc_r.a[4]=a4; rc_r.a[5]=a5; rc_r.a[6]=a6; \
    rc_r.fZ=fZ; rc_r.fC=fC; rc_r.cyr=cyr; } while (0)
#define RC_TLOAD do { a0=rc_r.a[0]; a1=rc_r.a[1]; a2=rc_r.a[2]; a3=rc_r.a[3]; a4=rc_r.a[4]; a5=rc_r.a[5]; a6=rc_r.a[6]; \
    fZ=rc_r.fZ; fC=rc_r.fC; cyr=rc_r.cyr; rc_rb = rc_ram; } while (0)
#define RAMB rc_rb
#else
#define RC_TSAVE
#define RC_TLOAD
#define RAMB rc_ram
#endif
#define RC_CALL(x) do { RC_TSAVE; x; RC_TLOAD; } while (0)

#define rom rc_rom
#define ram RAMB
#define pal rc_pal
#define spr rc_spr
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
   bits 7 y 15, un acceso de 32 bits no alineado a 4. Con el byte alto distinto de 0 no hay region prevista */
extern u32 rc_cur; extern u16 rc_regmask[0x10000];
static inline u32 rc_regbit(u32 a)
{
    if (a >> 24) return 64;
    if (a < 0x100000) return 1;                   /* rom */
    if (a >= 0xFEC000) return 2;                  /* ram */
    if (a < 0x104000) return 4;                   /* vram */
    if (a >= 0x200000 && a < 0x204000) return 8;  /* pal */
    if ((a >> 12) == 0x440) return 16;            /* spr */
    if ((a >> 12) == 0x108) return 32;            /* vregs */
    return 64;
}
#define REGREC(a, w) (rc_regmask[(rc_cur >> 1) & 0xFFFF] |= rc_regbit(a) << ((w) * 8))
#define REGREC32(a, w) (rc_regmask[(rc_cur >> 1) & 0xFFFF] |= (rc_regbit(a) | ((a) & 2 ? 0x80 : 0)) << ((w) * 8))
#else
#define REGREC(a, w) ((void)0)
#define REGREC32(a, w) ((void)0)
#endif
/* lectura: todas las paginas apuntan a algo (E/S en una pagina que se rellena cada cuadro, sin mapear =
   pagina de 0xFF); las regiones tienen 4 bytes de margen al final para leer un long sin mirar el cruce */
static inline __attribute__((always_inline)) u32 RD8(u32 a) { REGREC(a, 0); return rc_rpg[(a >> 12) & 0xFFF][(a & 0xFFF) ^ 1]; }
static inline __attribute__((always_inline)) u32 RD16(u32 a) { REGREC(a, 0); return *(u16 *)(rc_rpg[(a >> 12) & 0xFFF] + (a & 0xFFF)); }
static inline __attribute__((always_inline)) u32 RD32(u32 a) { u16 *q; REGREC32(a, 0); q = (u16 *)(rc_rpg[(a >> 12) & 0xFFF] + (a & 0xFFF)); return ((u32)q[0] << 16) | q[1]; }
/* ---- VRAM cifrada (algoritmo de MAME, tablas en gcrypt.c). Dos escrituras seguidas de la misma instruccion a
   palabras consecutivas forman un par: la segunda se descifra con la primera (cifrada y descifrada) */
extern u16 rc_gsw[4][2][256], rc_gfin[2][256];
/* par abierto: rc_vl_key = pc de la instruccion y palabra que lo cerraria (0xFFFFFFFF = ninguno) */
extern u32 rc_vl_key, rc_vram_writes; extern u16 rc_vl_enc, rc_vl_dec;
#define RC_VLK(pc, off) (((pc) << 14) | (off))
#define GB(x, n) (((x) >> (n)) & 1)
#define GC_P1 0x1f
#define GC_P2 0x522a
static inline __attribute__((always_inline)) u32 gc_bs(u32 v, u32 s) { const u16 *t = rc_gsw[s][0]; return t[v >> 8] | t[256 + (v & 0xFF)]; }
/* suma de k en los 6 bits bajos */
static inline __attribute__((always_inline)) u32 gc_lo(u32 res, u32 k) { return ((res & 0xffc0) | ((res + (k ^ GC_P1)) & 0x3f)) ^ GC_P1; }
/* final comun: k de 5 bits sumado a los bits 6-10 y 11-15, y permutacion de salida */
static inline __attribute__((always_inline)) u32 gc_end(u32 res, u32 k)
{
    k ^= GC_P1;
    res = ((res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800)) ^ 0xffc0;
    return rc_gfin[0][res >> 8] | rc_gfin[1][res & 0xFF];
}
/* primera palabra (ep = dp = 0: permutacion 0, tipo 0) */
static inline __attribute__((always_inline)) u32 gc_dec0(u32 e)
{
    u32 res = gc_lo(gc_bs(e, 0) ^ GC_P2, 0x3a);
    return gc_end(res, GB(e, 9) | (GB(res, 2) << 1) | (GB(e, 5) << 2) | (GB(res, 5) << 3) | (GB(res, 4) << 4));
}
/* general (ep, dp: la palabra anterior del par, cifrada y descifrada); cada tipo en un solo tramo */
static inline __attribute__((always_inline)) u32 gc_dec(u32 ep, u32 dp, u32 e)
{
    u32 res = gc_bs(e, (dp >> 7) & 3) ^ GC_P2, k;
    switch ((GB(dp, 12) << 1) | GB(dp, 2)) {
    case 0:
        res = gc_lo(res, 0x3a);
        k = GB(e,9) | (GB(res,2)<<1) | (GB(e,5)<<2) | (GB(res,5)<<3) | (GB(res,4)<<4); break;
    case 1:
        res = gc_lo(res, GB(dp,0) | (GB(dp,1)<<1) | (GB(dp,1)<<2) | (GB(ep,3)<<3) | (GB(ep,8)<<4) | (GB(ep,15)<<5));
        k = GB(dp,2) | (GB(ep,4)<<1) | (GB(dp,14)<<2) | (GB(res,1)<<3) | (GB(dp,12)<<4); break;
    case 2:
        res = gc_lo(res, GB(ep,5) | (GB(dp,5)<<1) | (GB(ep,7)<<2) | (GB(ep,3)<<3) | (GB(ep,13)<<4) | (GB(ep,14)<<5));
        k = GB(ep,6) | (GB(dp,6)<<1) | (GB(dp,15)<<2) | (GB(res,0)<<3) | (GB(dp,7)<<4); break;
    default:
        res = gc_lo(res, GB(ep,0) | (GB(ep,9)<<1) | (GB(ep,6)<<2) | (GB(dp,4)<<3) | (GB(ep,2)<<4) | (GB(dp,11)<<5));
        k = GB(dp,2) | (GB(dp,9)<<1) | (GB(ep,5)<<2) | (GB(dp,1)<<3) | (GB(ep,10)<<4); break;
    }
    return gc_end(res, k);
}
/* en el codigo recompilado, funciones hoja fuera de linea (pocos registros libres: mejor sin nada alrededor) */
#ifdef RC_GEN
static __attribute__((noinline)) u32 rc_gdec0(u32 e) { return gc_dec0(e); }
static __attribute__((noinline)) u32 rc_gdec(u32 ep, u32 dp, u32 e) { return gc_dec(ep, dp, e); }
#else
#define rc_gdec0 gc_dec0
#define rc_gdec gc_dec
#endif
/* escritura de 16 bits a la VRAM (a dentro de 0x100000-0x103FFF) */
static inline void rc_vw16(u32 a, u32 v, u32 pc)
{
    u32 off = (a & 0x3FFF) >> 1;
#ifndef RC_GREGS
    rc_vram_writes++;
#endif
    if (rc_vl_key == RC_VLK(pc, off)) { rc_vl_key = 0xFFFFFFFF; v = rc_gdec(rc_vl_enc, rc_vl_dec, v); }
    else { rc_vl_enc = v; rc_vl_key = RC_VLK(pc, off + 1); v = rc_vl_dec = rc_gdec0(v); }
    MW(rc_vram, a & 0x3FFE) = v;
}

#ifdef RC_GEN
/* las dos palabras de un long (a y a+2 dentro de la VRAM), mismo estado final que dos rc_vw16. Lo normal: la
   primera abre un par y la segunda lo cierra; el resultado depende solo del long escrito y se guarda en una cache
   (los mapas repiten mucho los codigos: con 4096 entradas falla unas 7 veces por cuadro en carrera). Tras cerrar
   el par, enc/dec no se vuelven a mirar hasta otra escritura suelta, que los pone: no hace falta guardarlos */
#define RC_VCN 4096
typedef struct { u32 k, d; } rc_vce;
static rc_vce rc_vc[RC_VCN];   /* clave 0 con su valor (rc_vc_init): nunca da un acierto falso */
static __attribute__((noinline)) u32 rc_gpair(u32 v) { u32 e1 = v >> 16, d1 = gc_dec0(e1); return (d1 << 16) | gc_dec(e1, d1, v & 0xFFFF); }
static void rc_vc_init(void) { int i; u32 d = rc_gpair(0); for (i = 0; i < RC_VCN; i++) { rc_vc[i].k = 0; rc_vc[i].d = d; } }
static inline void rc_vw32(u32 a, u32 v, u32 pc)
{
    u32 o = a & 0x3FFF, d;
#ifndef RC_GREGS
    rc_vram_writes += 2;
#endif
    if (rc_vl_key != RC_VLK(pc, o >> 1)) {
        rc_vce *c = &rc_vc[(v ^ (v >> 16) ^ (v >> 7)) & (RC_VCN - 1)];
        if (c->k == v) d = c->d;
        else { d = rc_gpair(v); c->k = v; c->d = d; }
        rc_vl_key = 0xFFFFFFFF;
    } else {   /* la primera cierra un par anterior y la segunda queda suelta */
        u32 e2 = v & 0xFFFF, d2 = rc_gdec0(e2);
        d = (rc_gdec(rc_vl_enc, rc_vl_dec, v >> 16) << 16) | d2;
        rc_vl_enc = e2; rc_vl_key = RC_VLK(pc, (o >> 1) + 2); rc_vl_dec = d2;
    }
    MW(rc_vram, o & 0x3FFE) = d >> 16; MW(rc_vram, (o + 2) & 0x3FFE) = d;
}
/* escrituras con region prevista en la VRAM (rc68.py: WRV16/WRV32). El long con acierto en la cache y sin par
   abierto va sin llamadas (sin pila); lo demas, por rc_vw32 */
static __attribute__((noinline)) void rc_vww(u32 a, u32 v, u32 pc) { rc_vw16(a, v & 0xFFFF, pc); }
static __attribute__((noinline)) void rc_vwl_s(u32 a, u32 v, u32 pc) { rc_vw32(a, v, pc); }
static __attribute__((noinline)) void rc_vwl(u32 a, u32 v, u32 pc)
{
    u32 o = a & 0x3FFE;
    rc_vce *c = &rc_vc[(v ^ (v >> 16) ^ (v >> 7)) & (RC_VCN - 1)];
    if (rc_vl_key != RC_VLK(pc, o >> 1) && c->k == v) {
        u32 d = c->d;
        rc_vl_key = 0xFFFFFFFF;
        MW(rc_vram, o) = d >> 16; MW(rc_vram, o + 2) = d;
    } else rc_vwl_s(a, v, pc);
}
#endif

/* caminos lentos fuera de linea. Las escrituras a paginas sin puntero solo hacen algo en la VRAM (aqui mismo,
   sin guardar registros) y en la E/S de 8 bits (rc_wr8: guarda los registros del 68000 que viven en temporales) */
#ifdef RC_GEN
static __attribute__((noinline)) void rc_wr8_s(u32 a, u32 v, u32 pc) { RC_CALL(rc_wr8(a, v & 0xFF, pc)); }
static __attribute__((noinline)) void rc_wr16_s(u32 a, u32 v, u32 pc) { if (((a & 0xFFFFFF) - 0x100000) < 0x4000) rc_vw16(a, v & 0xFFFF, pc); }
static __attribute__((noinline)) void rc_wr32_s(u32 a, u32 v, u32 pc)
{
    u32 o = (a & 0xFFFFFF) - 0x100000;
    if (o < 0x3FFE) { rc_vw32(a, v, pc); return; }
    if (o < 0x4000) rc_vw16(a, v >> 16, pc);
    if (((a + 2) & 0xFFFFFF) - 0x100000 < 0x4000) rc_vw16(a + 2, v & 0xFFFF, pc);
}
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
   comprueban que caben enteros) */
#define RG_ram(a) (((a) >> 14) == 0x3FB)
#define RG_rom(a) (((a) >> 20) == 0)
#define RG_pal(a) (((a) >> 14) == 0x080)
#define RG_vram(a) (((a) >> 14) == 0x040)
#define RG_spr(a) (((a) >> 12) == 0x440)
#define RG_vregs(a) (((a) >> 12) == 0x108)
#define RO_ram(a) ((a) & 0x3FFF)
#define RO_rom(a) (a)
#define RO_pal(a) ((a) & 0x3FFF)
#define RO_vram(a) ((a) & 0x3FFF)
#define RO_spr(a) ((a) & 0xFFF)
#define RO_vregs(a) ((a) & 0xFFF)
#define RB_ram RAMB
#define RB_rom rc_rom
#define RB_pal rc_pal
#define RB_vram rc_vram
#define RB_spr rc_spr
#define RB_vregs rc_vregs
#define RBO_ram RAMB
#define RBO_rom RC_OPQ(rc_rom)
#define RBO_pal RC_OPQ(rc_pal)
#define RBO_vram RC_OPQ(rc_vram)
#define RBO_spr RC_OPQ(rc_spr)
#define RBO_vregs RC_OPQ(rc_vregs)
#define RS_ram 0x4000
#define RS_rom 0x100000
#define RS_pal 0x4000
#define RS_vram 0x4000
#define RS_spr 0x1000
#define RS_vregs 0x1000
#ifdef RCREG
#define RDP8(r, a) RD8(a)
#define RDP16(r, a) RD16(a)
#define RDP32(r, a) RD32(a)
#define WRP8(r, a, v, pc) WR8(a, v, pc)
#define WRP16(r, a, v, pc) WR16(a, v, pc)
#define WRP32(r, a, v, pc) WR32(a, v, pc)
#define RDA32(r, a) RD32(a)
#define WRV16(a, v, pc) WR16(a, v, pc)
#define WRV32(a, v, pc) WR32(a, v, pc)
#define RBLK(r, a, n) 0
#else
#define RDP8(r, a) (RG_##r(a) ? (u32)MB(RB_##r, RO_##r(a)) : rc_rdg8(a))
#define RDP16(r, a) (RG_##r(a) ? (u32)MW(RB_##r, RO_##r(a)) : rc_rdg16(a))
#define RDP32(r, a) (RG_##r(a) ? ML(RBO_##r + RO_##r(a), 0) : rc_rdg32(a))
#define WRP8(r, a, v, pc) do { if (RG_##r(a)) MB(RB_##r, RO_##r(a)) = (u8)(v); else rc_wrg8(a, v, pc); } while (0)
#define WRP16(r, a, v, pc) do { if (RG_##r(a)) MW(RB_##r, RO_##r(a)) = (u16)(v); else rc_wrg16(a, v, pc); } while (0)
#define WRP32(r, a, v, pc) do { if (RG_##r(a)) SETL(RBO_##r + RO_##r(a), 0, v); else rc_wrg32(a, v, pc); } while (0)
#define RBLK(r, a, n) (RG_##r(a) && RO_##r(a) <= RS_##r - (n))
/* long con lecturas siempre alineadas a 4 en el perfil: una sola lectura de 32 bits (si no, el acceso general) */
#define RDA32(r, a) ((RG_##r(a) && !((a) & 2)) ? MLA(RBO_##r + RO_##r(a), 0) : rc_rdg32(a))
/* VRAM: descifrado fuera de linea sin pasar por la tabla de paginas; un long que se sale de la VRAM, por el general */
#define WRV16(a, v, pc) do { if (RG_vram(a)) rc_vww(a, v, pc); else rc_wrg16(a, v, pc); } while (0)
#define WRV32(a, v, pc) do { if (RBLK(vram, a, 4)) rc_vwl(a, v, pc); else rc_wrg32(a, v, pc); } while (0)
#endif
#define RPTR(r, a) (RBO_##r + RO_##r(a))
#endif

#ifdef RC_GEN
/* despacho general (rts, rte, jmp indirecto): tabla hash de etiquetas por el pc con las palabras cambiadas
   (k), sondeo lineal hasta una entrada vacia (clave 0). La genera rc68.py (RC_HN, RC_HS) */
static inline void *rc_hfind(const rc_lab *e, u32 k)
{
    e += (k ^ (k >> RC_HS)) & (RC_HN - 1);
    while (e->pc != k) { if (!e->pc) return 0; e++; }
    return e->l;
}
/* long de la RAM del 68000 tal como esta en memoria (palabras cambiadas): alineado a 4, de una vez */
#define RAW32(a) (*(u32 *)(RAMB + ((a) & 0x3FFC)))
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
    rc_r.fX=fX; rc_r.fN=fN; rc_r.fZ=fZ; rc_r.fV=fV; rc_r.fC=fC; rc_r.imask=imask; rc_r.cyc=cyr+cyc_next; rc_r.cyc_next=cyc_next; \
    rc_r.cyr=cyr; } while (0)

#ifdef RC_GREGS
#define RC_LOCALS u32 fX = rc_r.fX, fV = rc_r.fV, fN = rc_r.fN, imask = rc_r.imask; s32 cyc_next = rc_r.cyc_next; \
    u32 vs, vd, vr, eas, ead, dpc; void *dl;
#define RC_ENTER d0 = rc_r.d[0]; d1 = rc_r.d[1]; d2 = rc_r.d[2]; d3 = rc_r.d[3]; d4 = rc_r.d[4]; d5 = rc_r.d[5]; \
    d6 = rc_r.d[6]; d7 = rc_r.d[7]; a7 = rc_r.a[7]; RC_TLOAD; cyr = rc_r.cyc - rc_r.cyc_next; rc_vc_init();
#else
#define RC_LOCALS u32 d0 = rc_r.d[0], d1 = rc_r.d[1], d2 = rc_r.d[2], d3 = rc_r.d[3], d4 = rc_r.d[4], d5 = rc_r.d[5], d6 = rc_r.d[6], d7 = rc_r.d[7]; \
    u32 a0 = rc_r.a[0], a1 = rc_r.a[1], a2 = rc_r.a[2], a3 = rc_r.a[3], a4 = rc_r.a[4], a5 = rc_r.a[5], a6 = rc_r.a[6], a7 = rc_r.a[7]; \
    u32 fX = rc_r.fX, fN = rc_r.fN, fZ = rc_r.fZ, fV = rc_r.fV, fC = rc_r.fC, imask = rc_r.imask; \
    s32 cyr = rc_r.cyc - rc_r.cyc_next, cyc_next = rc_r.cyc_next; u32 vs, vd, vr, eas, ead, dpc; void *dl;
#define RC_ENTER rc_vc_init();
#endif
/* despacho indirecto y rts: bloques comunes al final de rc_run (un salto en cada sitio) */
#define DISPATCH(x) do { dpc = (x); goto rc_disp; } while (0)
#define FELL(x) DISPATCH(x)
#define RTS() goto rc_rts
#define CYC(n) cyr += (n)
#define CYC_FRAME 200000
#define IDLEHINT(cond) do { if ((cond) && cyr < 0) cyr = 0; } while (0)
#define IRQCHK(t) do { if (cyr >= 0 && imask < 6) { dpc = (t); goto irq_take; } } while (0)
#define DIVZERO(pc) do { RC_SYNC; rc_divzero(pc); } while (0)
void rc_divzero(u32 pc);

#ifdef RCDBG
void rc_ck(u32 pc, u32 live);
void rc_dbg_irq(void);
#define CK(pc, lv) do { RC_SYNC; rc_ck(pc, lv); } while (0)
#define IRQ_DBG() rc_dbg_irq()
#else
#ifdef RCREG
#define CK(pc, lv) (rc_cur = (pc))
#else
#define CK(pc, lv)
#endif
#define IRQ_DBG()
#endif

/* irq_take: el contador absoluto pasa a contar desde este vblank (cyc -= cyc_next) y cyc_next se recalcula */
#define RC_EXIT_CODE \
irq_take: \
    RC_SYNC; rc_vblank(); RC_TLOAD; \
    { s32 _c = cyr; cyc_next = CYC_FRAME; while (cyc_next <= _c) cyc_next += CYC_FRAME; cyr = _c - cyc_next; } \
    { u32 sr = GETSR(); a7 -= 4; WRP32(ram, a7, dpc, 0); a7 -= 2; WRP16(ram, a7, sr, 0); imask = 6; } \
    IRQ_DBG(); \
    DISPATCH(RC_IRQ6); \
rc_rts: \
    { u32 _k; \
      if (RG_ram(a7)) { if (!(a7 & 2)) _k = RAW32(a7); else { u8 *_q = RAMB + RO_ram(a7); _k = ((u32)MW(_q, 2) << 16) | MW(_q, 0); } } \
      else _k = RC_SW16(rc_rdg32(a7)); \
      a7 += 4; dl = rc_hfind(ht, _k); if (dl) goto *dl; dpc = RC_SW16(_k); goto bad_dispatch; } \
rc_disp: \
    dl = rc_hfind(ht, RC_SW16(dpc)); if (!dl) goto bad_dispatch; goto *dl; \
bad_dispatch: \
    RC_SYNC; rc_r.pc = dpc; rc_bad(dpc); return;
#endif

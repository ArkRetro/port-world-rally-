#include "mcs51.h"
typedef unsigned char u8; typedef unsigned short u16;
#define ACC  c->sfr[0x60]
#define B    c->sfr[0x70]
#define PSW  c->sfr[0x50]
#define SP   c->sfr[0x01]
#define DPL  c->sfr[0x02]
#define DPH  c->sfr[0x03]
#define P2   c->sfr[0x20]
#define CY   (PSW >> 7)
#define DPTR ((u16)((DPH << 8) | DPL))
#define SETDPTR(v) do { u16 _v = (v); DPH = _v >> 8; DPL = (u8)_v; } while (0)
#define BANK (PSW & 0x18)
#define R(n) c->iram[BANK + (n)]
#define CODE(a) c->code[(u16)(a) & 0x7fff]
#ifndef MCS51_XOR
#define MCS51_XOR 0
#endif
#define XRAM(a) c->xram[((u16)(a) & 0x3fff) ^ MCS51_XOR]

static u8 parity(u8 v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1; }
static u8 rd_dir(mcs51_t *c, u8 a) {
    if (a < 0x80) return c->iram[a];
    if (a == 0xD0) return (PSW & 0xFE) | parity(ACC);
    return c->sfr[a - 0x80];
}
static void wr_dir(mcs51_t *c, u8 a, u8 v) {
    if (a < 0x80) c->iram[a] = v; else c->sfr[a - 0x80] = v;
}
static u8 rd_bit(mcs51_t *c, u8 b) {
    u8 a = b < 0x80 ? 0x20 + (b >> 3) : (b & 0xF8);
    return (rd_dir(c, a) >> (b & 7)) & 1;
}
static void wr_bit(mcs51_t *c, u8 b, u8 v) {
    u8 a = b < 0x80 ? 0x20 + (b >> 3) : (b & 0xF8);
    u8 x = a < 0x80 ? c->iram[a] : c->sfr[a - 0x80];
    x = v ? (x | (1 << (b & 7))) : (x & ~(1 << (b & 7)));
    wr_dir(c, a, x);
}
static void push(mcs51_t *c, u8 v) { SP++; c->iram[SP] = v; }
static u8 pop(mcs51_t *c) { u8 v = c->iram[SP]; SP--; return v; }
static void setcy(mcs51_t *c, int v) { PSW = (PSW & 0x7F) | (v ? 0x80 : 0); }

static void add(mcs51_t *c, u8 v, int carry) {
    u8 a = ACC; int cin = carry ? CY : 0;
    unsigned r = a + v + cin;
    int ac = ((a & 15) + (v & 15) + cin) > 15;
    int ov = ((~(a ^ v)) & (a ^ r) & 0x80) != 0;
    PSW = (PSW & 0x3B) | ((r > 255) ? 0x80 : 0) | (ac ? 0x40 : 0) | (ov ? 0x04 : 0);
    ACC = (u8)r;
}
static void subb(mcs51_t *c, u8 v) {
    u8 a = ACC; int cin = CY;
    int r = a - v - cin;
    int ac = ((a & 15) - (v & 15) - cin) < 0;
    int ov = (((a ^ v) & (a ^ (u8)r)) & 0x80) != 0;
    PSW = (PSW & 0x3B) | ((r < 0) ? 0x80 : 0) | (ac ? 0x40 : 0) | (ov ? 0x04 : 0);
    ACC = (u8)r;
}

void mcs51_reset(mcs51_t *c) {
    int i;
    for (i = 0; i < 256; i++) c->iram[i] = 0;
    for (i = 0; i < 128; i++) c->sfr[i] = 0;
    SP = 7; c->sfr[0x00] = 0xFF; c->sfr[0x10] = 0xFF; c->sfr[0x20] = 0xFF; c->sfr[0x30] = 0xFF;
    c->pc = 0; c->steps = 0;
}

long mcs51_run(mcs51_t *c, unsigned short stop_pc, long max) {
    long n = 0;
    u16 pc = c->pc;
    while (n < max) {
        u8 op = CODE(pc);
        u8 o1 = CODE(pc + 1), o2 = CODE(pc + 2);
        u8 t;
        int rr = op & 7;           /* para operaciones con Rn: op&7 en 8..f */
        n++;
        if (!(n & 0x7FF)) *(volatile unsigned char *)0x300001 = 0;   /* watchdog de la Neo Geo */
        switch (op) {
        case 0x00: pc += 1; break;                                   /* nop */
        case 0x01: case 0x21: case 0x41: case 0x61: case 0x81: case 0xA1: case 0xC1: case 0xE1:
            pc = ((pc + 2) & 0xF800) | ((op & 0xE0) << 3) | o1; break; /* ajmp */
        case 0x11: case 0x31: case 0x51: case 0x71: case 0x91: case 0xB1: case 0xD1: case 0xF1:
            pc += 2; push(c, (u8)pc); push(c, pc >> 8);
            pc = (pc & 0xF800) | ((op & 0xE0) << 3) | o1; break;     /* acall */
        case 0x02: pc = (o1 << 8) | o2; break;                       /* ljmp */
        case 0x12: pc += 3; push(c, (u8)pc); push(c, pc >> 8); pc = (o1 << 8) | o2; break; /* lcall */
        case 0x22: case 0x32: t = pop(c); pc = (t << 8) | pop(c); break; /* ret/reti */
        case 0x03: ACC = (ACC >> 1) | (ACC << 7); pc += 1; break;    /* rr a */
        case 0x13: t = ACC & 1; ACC = (ACC >> 1) | (CY << 7); setcy(c, t); pc += 1; break; /* rrc */
        case 0x23: ACC = (ACC << 1) | (ACC >> 7); pc += 1; break;    /* rl a */
        case 0x33: t = ACC >> 7; ACC = (ACC << 1) | CY; setcy(c, t); pc += 1; break; /* rlc */
        case 0x04: ACC++; pc += 1; break;
        case 0x05: wr_dir(c, o1, rd_dir(c, o1) + 1); pc += 2; break;
        case 0x06: case 0x07: c->iram[R(op & 1)]++; pc += 1; break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: R(rr)++; pc += 1; break;
        case 0x14: ACC--; pc += 1; break;
        case 0x15: wr_dir(c, o1, rd_dir(c, o1) - 1); pc += 2; break;
        case 0x16: case 0x17: c->iram[R(op & 1)]--; pc += 1; break;
        case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: R(rr)--; pc += 1; break;
        case 0x10: pc += 3; if (rd_bit(c, o1)) { wr_bit(c, o1, 0); pc += (signed char)o2; } break; /* jbc */
        case 0x20: pc += 3; if (rd_bit(c, o1)) pc += (signed char)o2; break;  /* jb */
        case 0x30: pc += 3; if (!rd_bit(c, o1)) pc += (signed char)o2; break; /* jnb */
        case 0x40: pc += 2; if (CY) pc += (signed char)o1; break;    /* jc */
        case 0x50: pc += 2; if (!CY) pc += (signed char)o1; break;   /* jnc */
        case 0x60: pc += 2; if (!ACC) pc += (signed char)o1; break;  /* jz */
        case 0x70: pc += 2; if (ACC) pc += (signed char)o1; break;   /* jnz */
        case 0x80: pc += 2 + (signed char)o1; break;                 /* sjmp */
        case 0x73: pc = (u16)(ACC + DPTR); break;                    /* jmp @a+dptr */
        case 0x24: add(c, o1, 0); pc += 2; break;
        case 0x25: add(c, rd_dir(c, o1), 0); pc += 2; break;
        case 0x26: case 0x27: add(c, c->iram[R(op & 1)], 0); pc += 1; break;
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: add(c, R(rr), 0); pc += 1; break;
        case 0x34: add(c, o1, 1); pc += 2; break;
        case 0x35: add(c, rd_dir(c, o1), 1); pc += 2; break;
        case 0x36: case 0x37: add(c, c->iram[R(op & 1)], 1); pc += 1; break;
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: add(c, R(rr), 1); pc += 1; break;
        case 0x94: subb(c, o1); pc += 2; break;
        case 0x95: subb(c, rd_dir(c, o1)); pc += 2; break;
        case 0x96: case 0x97: subb(c, c->iram[R(op & 1)]); pc += 1; break;
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: subb(c, R(rr)); pc += 1; break;
        /* orl */
        case 0x42: wr_dir(c, o1, rd_dir(c, o1) | ACC); pc += 2; break;
        case 0x43: wr_dir(c, o1, rd_dir(c, o1) | o2); pc += 3; break;
        case 0x44: ACC |= o1; pc += 2; break;
        case 0x45: ACC |= rd_dir(c, o1); pc += 2; break;
        case 0x46: case 0x47: ACC |= c->iram[R(op & 1)]; pc += 1; break;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: ACC |= R(rr); pc += 1; break;
        /* anl */
        case 0x52: wr_dir(c, o1, rd_dir(c, o1) & ACC); pc += 2; break;
        case 0x53: wr_dir(c, o1, rd_dir(c, o1) & o2); pc += 3; break;
        case 0x54: ACC &= o1; pc += 2; break;
        case 0x55: ACC &= rd_dir(c, o1); pc += 2; break;
        case 0x56: case 0x57: ACC &= c->iram[R(op & 1)]; pc += 1; break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: ACC &= R(rr); pc += 1; break;
        /* xrl */
        case 0x62: wr_dir(c, o1, rd_dir(c, o1) ^ ACC); pc += 2; break;
        case 0x63: wr_dir(c, o1, rd_dir(c, o1) ^ o2); pc += 3; break;
        case 0x64: ACC ^= o1; pc += 2; break;
        case 0x65: ACC ^= rd_dir(c, o1); pc += 2; break;
        case 0x66: case 0x67: ACC ^= c->iram[R(op & 1)]; pc += 1; break;
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F: ACC ^= R(rr); pc += 1; break;
        /* bits / carry */
        case 0x72: setcy(c, CY | rd_bit(c, o1)); pc += 2; break;
        case 0xA0: setcy(c, CY | !rd_bit(c, o1)); pc += 2; break;
        case 0x82: setcy(c, CY & rd_bit(c, o1)); pc += 2; break;
        case 0xB0: setcy(c, CY & !rd_bit(c, o1)); pc += 2; break;
        case 0xA2: setcy(c, rd_bit(c, o1)); pc += 2; break;
        case 0x92: wr_bit(c, o1, CY); pc += 2; break;
        case 0xB2: wr_bit(c, o1, !rd_bit(c, o1)); pc += 2; break;
        case 0xB3: setcy(c, !CY); pc += 1; break;
        case 0xC2: wr_bit(c, o1, 0); pc += 2; break;
        case 0xC3: setcy(c, 0); pc += 1; break;
        case 0xD2: wr_bit(c, o1, 1); pc += 2; break;
        case 0xD3: setcy(c, 1); pc += 1; break;
        /* mov */
        case 0x74: ACC = o1; pc += 2; break;
        case 0x75: wr_dir(c, o1, o2); pc += 3; break;
        case 0x76: case 0x77: c->iram[R(op & 1)] = o1; pc += 2; break;
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: R(rr) = o1; pc += 2; break;
        case 0x85: wr_dir(c, o2, rd_dir(c, o1)); pc += 3; break;     /* mov dir,dir (src=o1, dst=o2) */
        case 0x86: case 0x87: wr_dir(c, o1, c->iram[R(op & 1)]); pc += 2; break;
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: wr_dir(c, o1, R(rr)); pc += 2; break;
        case 0x90: DPH = o1; DPL = o2; pc += 3; break;
        case 0xA6: case 0xA7: c->iram[R(op & 1)] = rd_dir(c, o1); pc += 2; break;
        case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: R(rr) = rd_dir(c, o1); pc += 2; break;
        case 0xE5: ACC = rd_dir(c, o1); pc += 2; break;
        case 0xE6: case 0xE7: ACC = c->iram[R(op & 1)]; pc += 1; break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF: ACC = R(rr); pc += 1; break;
        case 0xF5: wr_dir(c, o1, ACC); pc += 2; break;
        case 0xF6: case 0xF7: c->iram[R(op & 1)] = ACC; pc += 1; break;
        case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF: R(rr) = ACC; pc += 1; break;
        case 0x83: pc += 1; ACC = CODE(ACC + pc); break;             /* movc a,@a+pc */
        case 0x93: ACC = CODE(ACC + DPTR); pc += 1; break;           /* movc a,@a+dptr */
        case 0xE0: ACC = XRAM(DPTR); pc += 1; break;                 /* movx a,@dptr */
        case 0xF0: XRAM(DPTR) = ACC; pc += 1; break;
        case 0xE2: case 0xE3: ACC = XRAM((P2 << 8) | R(op & 1)); pc += 1; break;
        case 0xF2: case 0xF3: XRAM((P2 << 8) | R(op & 1)) = ACC; pc += 1; break;
        case 0xA3: SETDPTR(DPTR + 1); pc += 1; break;
        case 0xC0: push(c, rd_dir(c, o1)); pc += 2; break;
        case 0xD0: wr_dir(c, o1, pop(c)); pc += 2; break;
        case 0xC4: ACC = (ACC << 4) | (ACC >> 4); pc += 1; break;    /* swap */
        case 0xC5: t = rd_dir(c, o1); wr_dir(c, o1, ACC); ACC = t; pc += 2; break;
        case 0xC6: case 0xC7: t = c->iram[R(op & 1)]; c->iram[R(op & 1)] = ACC; ACC = t; pc += 1; break;
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: t = R(rr); R(rr) = ACC; ACC = t; pc += 1; break;
        case 0xD6: case 0xD7: { u8 *p = &c->iram[R(op & 1)]; t = *p; *p = (t & 0xF0) | (ACC & 15); ACC = (ACC & 0xF0) | (t & 15); pc += 1; break; } /* xchd */
        case 0xE4: ACC = 0; pc += 1; break;
        case 0xF4: ACC = ~ACC; pc += 1; break;
        case 0xA4: { unsigned r = ACC * B; ACC = (u8)r; B = r >> 8; PSW &= 0x7B; if (r > 255) PSW |= 4; pc += 1; break; } /* mul */
        case 0x84: if (B == 0) { PSW = (PSW & 0x7B) | 4; } else { u8 q = ACC / B, m = ACC % B; ACC = q; B = m; PSW &= 0x7B; } pc += 1; break; /* div */
        case 0xD4: { unsigned a = ACC; if ((a & 15) > 9 || (PSW & 0x40)) a += 6; if (a > 0xFF) PSW |= 0x80;
                     if (((a >> 4) & 0x1F) > 9 || CY) a += 0x60; if (a > 0xFF) PSW |= 0x80; ACC = (u8)a; pc += 1; break; } /* da */
        case 0xB4: pc += 3; setcy(c, ACC < o1); if (ACC != o1) pc += (signed char)o2; break;
        case 0xB5: pc += 3; t = rd_dir(c, o1); setcy(c, ACC < t); if (ACC != t) pc += (signed char)o2; break;
        case 0xB6: case 0xB7: pc += 3; t = c->iram[R(op & 1)]; setcy(c, t < o1); if (t != o1) pc += (signed char)o2; break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            pc += 3; t = R(rr); setcy(c, t < o1); if (t != o1) pc += (signed char)o2; break;
        case 0xD5: pc += 3; t = rd_dir(c, o1) - 1; wr_dir(c, o1, t); if (t) pc += (signed char)o2; break; /* djnz dir */
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            pc += 2; if (--R(rr)) pc += (signed char)o1; break;
        default: pc += 1; break;                                     /* 0xA5 reservado */
        }
        if (pc == stop_pc) break;
    }
    c->pc = pc; c->steps += n;
    return n;
}

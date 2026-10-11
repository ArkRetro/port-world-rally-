/* psxprof: emulador PS1 minimo para perfilar el port de World Rally (EXE sin BIOS, todo por sondeo).
   Modelo de tiempos aproximado (constantes de DuckStation): CPU 33,8688 MHz, cache de instrucciones de 4 KB,
   lecturas de RAM de 6 ciclos, GPU con coste por pixel, CD con busqueda/lectura/pausa, mando SIO0.
   Uso: psxprof imagen.cue wr2.elf.bin(PS-EXE) cuadros_juego [salida_dir]
   Entradas (botones) segun el cuadro del juego (llamadas a rc_vblank), como rc_host. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64; typedef int64_t s64;
typedef int16_t s16; typedef int8_t s8;

#define CLK 33868800.0
static double LINE_CYC = 2152.0;      /* NTSC: 3413 ciclos de video por linea */
static int LINES = 263;
static u64 frame_cyc;
static int RAM_LD = 6, ICF_BASE = 0, ICF_WORD = 1, IO_GPU = 2, IO_CD = 8, IO_SPU = 18, IO_MISC = 2;

static u8 ram[0x200000], scratch[0x400];
static u32 R[32], PC, NPC, HI, LO, SR, CAUSE, EPC;
static int ld_r; static u32 ld_v;
static u64 cyc, muldiv_ready, stall_cyc;
static u32 icache_tag[256], icache_valid[256];
static u32 cache_ctrl;
static int halted;
static const char *outdir = ".";

/* ---- perfil */
static u64 *prof;                     /* ciclos por palabra de RAM (PC) */
static u64 *prof_n;                   /* instrucciones ejecutadas */
static u32 sym_vblank, sym_rc_run, sym_rc_run_end;
static u64 prof_from_cyc = 0;
static int prof_on = 1;

/* ---- interrupciones */
static u32 I_STAT, I_MASK;
/* ---- temporizadores */
static u32 t1_mode; static u64 t1_base;
static u32 t0_mode, t2_mode; static u64 t0_base, t2_base;
/* ---- video */
static u64 next_vbl; static u64 vbl_count;
/* ---- DMA */
static u32 DPCR = 0x07654321, DICR;
static u32 dma_madr[7], dma_bcr[7], dma_chcr[7];
static u64 dma_done[7];
/* ---- GPU */
static u16 vram[512][1024];
static u64 gpu_busy_until;
static u32 gpu_fifo[64]; static int gpu_fn, gpu_need;
static u32 gpu_upload_x, gpu_upload_y, gpu_upload_w, gpu_upload_h, gpu_upload_left, gpu_upload_pos;
static u32 tpage = 0, twin = 0, da_x0, da_y0, da_x1 = 1023, da_y1 = 511; static s32 off_x, off_y;
static u32 disp_x, disp_y, disp_mode, disp_on;
static u64 gpu_ticks_total, gpu_prims;
/* ---- CD */
static FILE *cdf; static long cd_sectors;
static int ntracks; static u32 trk_lba[100];
static u8 cd_idx, cd_ie = 0x1F, cd_if;
static u8 cd_param[16]; static int cd_np;
static u8 cd_resp[16]; static int cd_nresp, cd_rpos;
static u8 cd_data[2352]; static int cd_dpos, cd_dlen;
static u8 cd_req;
static int cd_stat = 0x02;          /* motor encendido */
static u32 cd_loc, cd_cur; static int cd_mode;
typedef struct { u64 t; u8 irq; u8 resp[8]; u8 n; int kind; } cdev_t;
#define K_RESP 0
#define K_SECTOR 1
static cdev_t cdq[16]; static int cdqn;
static int cd_reading, cd_playing; static u64 cd_next_sector;
static u8 cd_sector_buf[2352]; static int cd_sector_ready;
static u64 cd_busy_until;
static FILE *evf;
/* ---- SPU */
static u16 spu[0x200];
static u8 spuram[0x80000];
static u32 spu_addr;
/* ---- mando */
static int joy_state, joy_byte_pending; static u64 joy_rx_at, joy_ack_at; static u8 joy_rx; static int joy_rx_valid;
static u64 joy_tx_busy_until; static u16 joy_ctrl, joy_mode, joy_baud; static int joy_seq; static u16 pad_buttons = 0xFFFF;
/* ---- juego */
static long game_frame;               /* llamadas a rc_vblank */
static long max_game_frames = 3000;
static FILE *framef;
static u64 last_vblank_call;
static long shot_every = 0, prof_from_frame = 0;
static u32 sym_ram, sym_vram, sym_pal, sym_spr, sym_vregs;
static u32 fnv(u32 h, u32 a, u32 n) { u32 i; a &= 0x1FFFFF; for (i = 0; i < n; i++) { h ^= ram[(a + i) & 0x1FFFFF]; h *= 16777619u; } return h; }
static u32 state_hash(void) { u32 h = 2166136261u; if (!sym_ram) return 0; h = fnv(h, sym_ram, 0x4000); h = fnv(h, sym_vram, 0x4000); h = fnv(h, sym_pal, 0x4000); h = fnv(h, sym_spr, 0x1000); h = fnv(h, sym_vregs, 0x10); return h; }

static void eventf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void eventf(const char *fmt, ...)
{
    va_list ap;
    if (!evf) return;
    fprintf(evf, "%llu %ld %llu ", (unsigned long long)cyc, game_frame, (unsigned long long)vbl_count);
    va_start(ap, fmt); vfprintf(evf, fmt, ap); va_end(ap);
    fputc('\n', evf);
}

/* ---------------------------------------------------------------- entradas */
static u16 inputs_for(long fr)
{
    u16 in = 0xFFFF;   /* botones del mando, activos a 0 */
    if (fr >= 600 && fr < 606) in &= ~0x0001;                       /* SELECT: moneda */
    if ((fr >= 800 && fr < 810) || (fr >= 1000 && fr < 1010) || (fr >= 1200 && fr < 1210)) in &= ~0x0008;   /* START */
    if (fr >= 1300) in &= ~0x4000;                                   /* X: acelerar */
    {   /* direccion: oscilar un poco para que el coche no vaya recto siempre */
        long k = fr - 1300;
        if (fr >= 1300 && getenv("STEER")) { long ph = (k / 90) % 4; if (ph == 1) in &= ~0x0020; else if (ph == 3) in &= ~0x0080; }
    }
    return in;
}

/* ---------------------------------------------------------------- GPU */
static u16 c24to15(u32 c) { return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10); }
static void plot(int x, int y, u16 c, int semi, int mode)
{
    if (x < (int)da_x0 || x > (int)da_x1 || y < (int)da_y0 || y > (int)da_y1) return;
    if (x < 0 || x >= 1024 || y < 0 || y >= 512) return;
    if (semi) {
        u16 b = vram[y][x]; int br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31, fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31, r, g, bl;
        switch (mode) {
        case 0: r = (br + fr) / 2; g = (bg + fg) / 2; bl = (bb + fb) / 2; break;
        case 1: r = br + fr; g = bg + fg; bl = bb + fb; break;
        case 2: r = br - fr; g = bg - fg; bl = bb - fb; break;
        default: r = br + fr / 4; g = bg + fg / 4; bl = bb + fb / 4; break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 31 ? 31 : g; bl = bl < 0 ? 0 : bl > 31 ? 31 : bl;
        c = (c & 0x8000) | r | (g << 5) | (bl << 10);
    }
    vram[y][x] = c;
}
static int texel(u32 tp, u32 clut, int u, int v, u16 *out)
{
    int tx = (tp & 15) * 64, ty = ((tp >> 4) & 1) * 256, depth = (tp >> 7) & 3;
    int mx = (twin & 31) * 8, my = ((twin >> 5) & 31) * 8, ox = ((twin >> 10) & 31) * 8, oy = ((twin >> 15) & 31) * 8;
    u16 t;
    u &= 255; v &= 255;
    u = (u & ~mx) | (ox & mx); v = (v & ~my) | (oy & my);
    if (depth == 0) {
        u16 w = vram[(ty + v) & 511][(tx + u / 4) & 1023];
        int idx = (w >> ((u & 3) * 4)) & 15;
        t = vram[(clut >> 6) & 511][((clut & 63) * 16 + idx) & 1023];
    } else if (depth == 1) {
        u16 w = vram[(ty + v) & 511][(tx + u / 2) & 1023];
        int idx = (w >> ((u & 1) * 8)) & 255;
        t = vram[(clut >> 6) & 511][((clut & 63) * 16 + idx) & 1023];
    } else t = vram[(ty + v) & 511][(tx + u) & 1023];
    if (!t) return 0;
    *out = t; return 1;
}
static void draw_rect(int x, int y, int w, int h, int textured, int semi, u32 col, u32 clut, int u0, int v0)
{
    int i, j, mode = (tpage >> 5) & 3;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
        if (textured) {
            u16 t;
            if (!texel(tpage, clut, u0 + i, v0 + j, &t)) continue;
            plot(x + i, y + j, t, semi && (t & 0x8000), mode);
        } else plot(x + i, y + j, c24to15(col), semi, mode);
    }
}
static void draw_quad_tex(s32 *xs, s32 *ys, int *us, int *vs, u32 clut, u32 tp, int semi)
{
    /* solo cuadrilateros alineados a los ejes (los que usa el port): interpolacion lineal de u/v */
    int x0 = xs[0], y0 = ys[0], x1 = xs[3], y1 = ys[3], i, j, mode = (tp >> 5) & 3;
    u32 savetp = tpage; tpage = (tpage & ~0x9FF) | (tp & 0x9FF);
    if (x1 <= x0 || y1 <= y0) { tpage = savetp; return; }
    for (j = y0; j < y1; j++) for (i = x0; i < x1; i++) {
        int u = us[0] + (us[1] - us[0]) * (i - x0) / (x1 - x0);
        int v = vs[0] + (vs[2] - vs[0]) * (j - y0) / (y1 - y0);
        u16 t;
        if (!texel(tp, clut, u, v, &t)) continue;
        plot(i, j, t, semi && (t & 0x8000), mode);
    }
    tpage = savetp;
}
static void gpu_add_ticks(u64 gt)   /* gt en ciclos de GPU (2 por ciclo de CPU) */
{
    u64 c = (gt + 1) / 2;
    if (gpu_busy_until < cyc) gpu_busy_until = cyc;
    gpu_busy_until += c;
    gpu_ticks_total += gt;
}
static void rect_ticks(int x, int y, int w, int h, int textured, int semi)
{
    int x0 = x < (int)da_x0 ? da_x0 : x, y0 = y < (int)da_y0 ? da_y0 : y, x1 = x + w - 1 > (int)da_x1 ? da_x1 : x + w - 1, y1 = y + h - 1 > (int)da_y1 ? da_y1 : y + h - 1;
    int dw = x1 - x0 + 1, dh = y1 - y0 + 1, tpr;
    if (dw <= 0 || dh <= 0) { gpu_add_ticks(16); return; }
    tpr = dw; if (textured) tpr += dw; if (semi) tpr += (dw + 1) / 2;
    gpu_add_ticks((u64)tpr * dh + 16);
}
static int gp0_len(u32 cmd)
{
    u32 op = cmd >> 24;
    if (op == 0x02) return 3;
    if (op >= 0x20 && op < 0x40) {
        int quad = (op >> 3) & 1, tex = (op >> 2) & 1, gour = (op >> 4) & 1, nv = quad ? 4 : 3;
        return 1 + nv * (1 + tex) + (gour ? nv - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) return (op & 0x10) ? 4 : 3;   /* lineas simples (polilineas no se usan) */
    if (op >= 0x60 && op < 0x80) {
        int size = (op >> 3) & 3, tex = (op >> 2) & 1;
        return 2 + tex + (size == 0 ? 1 : 0);
    }
    if (op >= 0x80 && op < 0xA0) return 4;
    if (op >= 0xA0 && op < 0xC0) return 3;
    if (op >= 0xC0 && op < 0xE0) return 3;
    return 1;
}
static void gp0_exec(void)
{
    u32 *f = gpu_fifo, op = f[0] >> 24;
    gpu_prims++;
    if (op == 0x02) {
        int x = f[1] & 0x3F0, y = (f[1] >> 16) & 0x1FF, w = ((f[2] & 0x3FF) + 15) & ~15, h = (f[2] >> 16) & 0x1FF, i, j;
        u16 c = c24to15(f[0] & 0xFFFFFF);
        for (j = 0; j < h; j++) for (i = 0; i < w; i++) vram[(y + j) & 511][(x + i) & 1023] = c;
        gpu_add_ticks(46 + ((w / 8) + 9) * h);
        return;
    }
    if (op >= 0x60 && op < 0x80) {
        int size = (op >> 3) & 3, tex = (op >> 2) & 1, semi = (op >> 1) & 1, k = 1;
        s32 x = (s16)(f[k] << 5) >> 5, y = (s16)((f[k] >> 16) << 5) >> 5; u32 clut = 0; int u = 0, v = 0, w, h;
        k++;
        if (tex) { u = f[k] & 255; v = (f[k] >> 8) & 255; clut = f[k] >> 16; k++; }
        if (size == 0) { w = f[k] & 0x3FF; h = (f[k] >> 16) & 0x1FF; } else if (size == 1) w = h = 1; else if (size == 2) w = h = 8; else w = h = 16;
        x += off_x; y += off_y;
        draw_rect(x, y, w, h, tex, semi, f[0] & 0xFFFFFF, clut, u, v);
        rect_ticks(x, y, w, h, tex, semi);
        return;
    }
    if (op >= 0x20 && op < 0x40) {
        int quad = (op >> 3) & 1, tex = (op >> 2) & 1, semi = (op >> 1) & 1, gour = (op >> 4) & 1, nv = quad ? 4 : 3, k = 1, i;
        s32 xs[4], ys[4]; int us[4], vs[4]; u32 clut = 0, tp = tpage;
        for (i = 0; i < nv; i++) {
            if (gour && i) k++;
            xs[i] = ((s16)(f[k] << 5) >> 5) + off_x; ys[i] = ((s16)((f[k] >> 16) << 5) >> 5) + off_y; k++;
            if (tex) { us[i] = f[k] & 255; vs[i] = (f[k] >> 8) & 255; if (i == 0) clut = f[k] >> 16; if (i == 1) tp = f[k] >> 16; k++; }
        }
        if (tex && quad) { draw_quad_tex(xs, ys, us, vs, clut, tp, semi); tpage = (tpage & ~0x9FF) | (tp & 0x9FF); }
        {   /* coste: dos triangulos */
            s64 a = llabs((s64)(xs[1] - xs[0]) * (ys[2] - ys[0]) - (s64)(xs[2] - xs[0]) * (ys[1] - ys[0])) / 2, px = a * (quad ? 2 : 1);
            if (tex) px += px; if (semi) px += (px + 1) / 2;
            gpu_add_ticks(px + 64);
        }
        return;
    }
    if (op >= 0xA0 && op < 0xC0) {
        gpu_upload_x = f[1] & 0x3FF; gpu_upload_y = (f[1] >> 16) & 0x1FF;
        gpu_upload_w = ((f[2] & 0x3FF) - 1) & 0x3FF; gpu_upload_w++; gpu_upload_h = (((f[2] >> 16) & 0x1FF) - 1) & 0x1FF; gpu_upload_h++;
        gpu_upload_left = (gpu_upload_w * gpu_upload_h + 1) / 2; gpu_upload_pos = 0;
        return;
    }
    switch (op) {
    case 0xE1: tpage = f[0] & 0xFFFFFF; break;
    case 0xE2: twin = f[0] & 0xFFFFF; break;
    case 0xE3: da_x0 = f[0] & 0x3FF; da_y0 = (f[0] >> 10) & 0x1FF; break;
    case 0xE4: da_x1 = f[0] & 0x3FF; da_y1 = (f[0] >> 10) & 0x1FF; break;
    case 0xE5: off_x = (s32)(f[0] << 21) >> 21; off_y = (s32)(f[0] << 10) >> 21; break;
    default: break;
    }
    gpu_add_ticks(2);
}
static void gp0_write(u32 v)
{
    if (gpu_upload_left) {
        int k;
        for (k = 0; k < 2; k++) {
            u32 p = gpu_upload_pos++;
            if (p < gpu_upload_w * gpu_upload_h) vram[(gpu_upload_y + p / gpu_upload_w) & 511][(gpu_upload_x + p % gpu_upload_w) & 1023] = (u16)(k ? v >> 16 : v);
        }
        gpu_upload_left--;
        if (!gpu_upload_left) gpu_add_ticks((gpu_upload_w * gpu_upload_h) / 2 + 16);
        return;
    }
    if (gpu_fn == 0) { gpu_need = gp0_len(v); }
    gpu_fifo[gpu_fn++] = v;
    if (gpu_fn >= gpu_need) { gp0_exec(); gpu_fn = 0; }
}
static void gp1_write(u32 v)
{
    switch (v >> 24) {
    case 0x00: gpu_fn = 0; gpu_upload_left = 0; disp_on = 0; break;
    case 0x01: gpu_fn = 0; gpu_upload_left = 0; break;
    case 0x03: disp_on = !(v & 1); break;
    case 0x05: disp_x = v & 0x3FF; disp_y = (v >> 10) & 0x1FF; break;
    case 0x08: disp_mode = v & 0xFF; break;
    default: break;
    }
}
static u32 gpustat(void)
{
    u32 s = 0x14802000;
    if (cyc >= gpu_busy_until && !gpu_upload_left) s |= 1 << 26;
    s |= 1 << 28;  /* listo para DMA */
    s |= 1 << 27;
    if ((vbl_count & 1)) s |= 1u << 31;
    return s;
}
static void screenshot(const char *name)
{
    char fn[600]; FILE *f; int x, y, w = 368, h = 240;
    snprintf(fn, sizeof fn, "%s/%s.ppm", outdir, name);
    f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        u16 c = vram[(disp_y + y) & 511][(disp_x + x) & 1023];
        u8 rgb[3] = { (u8)((c & 31) << 3), (u8)(((c >> 5) & 31) << 3), (u8)(((c >> 10) & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* ---------------------------------------------------------------- DMA */
static u32 rd32ram(u32 a) { a &= 0x1FFFFC; return ram[a] | (ram[a + 1] << 8) | (ram[a + 2] << 16) | ((u32)ram[a + 3] << 24); }
static void wr32ram(u32 a, u32 v) { a &= 0x1FFFFC; ram[a] = v; ram[a + 1] = v >> 8; ram[a + 2] = v >> 16; ram[a + 3] = v >> 24; }
static void dma_start(int ch)
{
    u32 chcr = dma_chcr[ch], madr = dma_madr[ch] & 0x1FFFFC, mode = (chcr >> 9) & 3, words = 0;
    if (ch == 2) {
        if (mode == 2) {   /* lista enlazada */
            u32 a = madr; int guard = 0;
            u64 gstart = gpu_busy_until > cyc ? gpu_busy_until : cyc;
            (void)gstart;
            for (;;) {
                u32 hd = rd32ram(a), n = hd >> 24, i;
                for (i = 0; i < n; i++) gp0_write(rd32ram(a + 4 + i * 4));
                words += n + 1;
                if ((hd & 0x800000) || ++guard > 100000) break;
                a = hd & 0x1FFFFC;
            }
            stall_cyc += words;                    /* el bus lo ocupa la DMA */
            dma_done[2] = (gpu_busy_until > cyc + words ? gpu_busy_until : cyc + words);
        } else {
            u32 bs = dma_bcr[ch] & 0xFFFF, nb = dma_bcr[ch] >> 16, i;
            if (!bs) bs = 0x10000;
            if (mode == 0) nb = 1;
            words = bs * nb;
            if (chcr & 1) for (i = 0; i < words; i++) gp0_write(rd32ram(madr + i * 4));
            stall_cyc += words;
            dma_done[2] = cyc + words;
        }
    } else if (ch == 4) {
        u32 bs = dma_bcr[ch] & 0xFFFF, nb = dma_bcr[ch] >> 16, i;
        words = bs * nb;
        for (i = 0; i < words; i++) { u32 v = rd32ram(madr + i * 4); u32 p = spu_addr & 0x7FFFF; spuram[p] = v; spuram[(p + 1) & 0x7FFFF] = v >> 8; spuram[(p + 2) & 0x7FFFF] = v >> 16; spuram[(p + 3) & 0x7FFFF] = v >> 24; spu_addr += 4; }
        stall_cyc += words * 4;
        dma_done[4] = cyc + words * 4;
    } else if (ch == 6) {   /* OTC */
        u32 n = dma_bcr[ch] & 0xFFFF, i, a = madr;
        for (i = 0; i < n; i++) { wr32ram(a, i == n - 1 ? 0xFFFFFF : (a - 4) & 0x1FFFFF); a -= 4; }
        stall_cyc += n; dma_done[6] = cyc + n;
    } else dma_done[ch] = cyc;
}

/* ---------------------------------------------------------------- CD */
static int bcd2i(u8 v) { return (v >> 4) * 10 + (v & 15); }
static u8 i2bcd(int v) { return (u8)(((v / 10) << 4) | (v % 10)); }
static void cd_push(u64 dt, u8 irq, const u8 *r, int n, int kind)
{
    cdev_t *e;
    if (cdqn >= 16) return;
    e = &cdq[cdqn++]; e->t = cyc + dt; e->irq = irq; e->n = n; e->kind = kind; memcpy(e->resp, r, n);
}
static u64 seek_ticks(u32 from, u32 to)
{
    u32 diff = from > to ? from - to : to - from, spt = 9;
    double s;
    u64 tps = 225792;   /* doble velocidad */
    if (from < to && diff <= spt) return tps * (diff < 2 ? 2 : diff);
    if (from >= to && (from >= spt ? from - spt : 0) <= to) return tps * ((to - (from >= spt ? from - spt : 0)) < 1 ? 1 : (to - (from >= spt ? from - spt : 0)));
    if (diff < 7200) {
        double sw = 330.0 - 63.1333 * log(fmin(fmax(from / 4500.0, 1.0), 72.0));
        s = diff < sw ? 0.05 : 0.1;
    } else {
        double lmax = 72 * 4500.0;
        s = 0.05 + (0.85 * (log((double)diff) / log(lmax))) * 0.4 + (0.85 * (diff / lmax)) * 0.6;
    }
    return (u64)(s * CLK) + (u64)(CLK / 1500);
}
static void cd_read_sector(u32 lba)
{
    if ((long)lba >= cd_sectors) { memset(cd_sector_buf, 0, sizeof cd_sector_buf); return; }
    fseek(cdf, (long)lba * 2352, SEEK_SET);
    if (fread(cd_sector_buf, 1, 2352, cdf) != 2352) memset(cd_sector_buf, 0, 2352);
}
static void cd_stop_activity(const char *why)
{
    if (cd_playing) eventf("CDDA_STOP %s lba %u", why, cd_cur);
    cd_reading = 0; cd_playing = 0; cdqn = 0;
    {   int i, j = 0; for (i = 0; i < cdqn; i++) if (cdq[i].kind != K_SECTOR) cdq[j++] = cdq[i]; cdqn = j; }
}
static void cd_command(u8 c)
{
    u8 r[8]; int n = 0;
    u64 ack = 25000;
    r[0] = cd_stat;
    switch (c) {
    case 0x01: cd_push(ack, 3, r, 1, K_RESP); break;                               /* GetStat */
    case 0x02: cd_loc = (bcd2i(cd_param[0]) * 60 + bcd2i(cd_param[1])) * 75 + bcd2i(cd_param[2]) - 150; cd_push(ack, 3, r, 1, K_RESP); break;
    case 0x03: {                                                                    /* Play */
        u64 st = seek_ticks(cd_cur, cd_loc);
        cd_stop_activity("play");
        cd_push(ack, 3, r, 1, K_RESP);
        cd_playing = 1; cd_cur = cd_loc;
        eventf("CDDA_PLAY lba %u seek_ms %.1f", cd_loc, st * 1000.0 / CLK);
        cd_busy_until = cyc + st;
        break; }
    case 0x06: case 0x1B: {                                                         /* ReadN / ReadS */
        u64 st = seek_ticks(cd_cur, cd_loc);
        if (cd_playing) eventf("CDDA_STOP read lba %u", cd_cur);
        cd_playing = 0;
        cd_push(ack, 3, r, 1, K_RESP);
        cd_reading = 1; cd_cur = cd_loc;
        cd_next_sector = cyc + ack + st;
        eventf("CD_READ lba %u seek_ms %.1f", cd_loc, st * 1000.0 / CLK);
        break; }
    case 0x09: {                                                                    /* Pause */
        u64 pt = (cd_reading || cd_playing) ? 9 * 225792ULL : 7400;
        if (pt < 1000000 && (cd_reading || cd_playing)) pt = 1000000;
        if (cd_playing) eventf("CDDA_STOP pause lba %u", cd_cur);
        cd_reading = 0; cd_playing = 0;
        { int i, j = 0; for (i = 0; i < cdqn; i++) if (cdq[i].kind != K_SECTOR) cdq[j++] = cdq[i]; cdqn = j; }
        cd_push(ack, 3, r, 1, K_RESP);
        cd_push(ack + pt, 2, r, 1, K_RESP);
        eventf("CD_PAUSE ms %.1f", pt * 1000.0 / CLK);
        break; }
    case 0x0A: cd_push(80000, 3, r, 1, K_RESP); cd_push(80000 + 4000000, 2, r, 1, K_RESP); cd_mode = 0; break;  /* Init */
    case 0x0C: case 0x0D: case 0x0B: cd_push(ack, 3, r, 1, K_RESP); break;         /* Demute / Setfilter / Mute */
    case 0x0E: cd_mode = cd_param[0]; cd_push(ack, 3, r, 1, K_RESP); break;        /* Setmode */
    case 0x13: r[1] = 1; r[2] = i2bcd(ntracks); cd_push(ack, 3, r, 3, K_RESP); break;
    case 0x14: {                                                                    /* GetTD */
        int t = bcd2i(cd_param[0]); u32 l;
        if (t == 0) l = (u32)cd_sectors; else if (t <= ntracks) l = trk_lba[t]; else { r[0] |= 1; r[1] = 0x10; cd_push(ack, 5, r, 2, K_RESP); break; }
        l += 150; r[1] = i2bcd(l / 4500); r[2] = i2bcd((l / 75) % 60); cd_push(ack, 3, r, 3, K_RESP); break; }
    case 0x1E: cd_push(ack, 3, r, 1, K_RESP); cd_push(ack + (u64)(CLK / 2), 2, r, 1, K_RESP); break;   /* ReadTOC */
    case 0x19: r[0] = 0x20; cd_push(ack, 3, r, 1, K_RESP); break;
    default: cd_push(ack, 3, r, 1, K_RESP); break;
    }
    (void)n;
    cd_np = 0;
}
static void cd_tick(void)
{
    int i;
    /* entregar el siguiente evento si la bandera esta libre */
    if (cd_reading && cyc >= cd_next_sector) {
        u8 r[1]; r[0] = cd_stat | 0x20;
        cd_read_sector(cd_cur);
        if ((cd_if & 7) == 0 && cdqn < 15) {
            memcpy(cd_data, cd_sector_buf + 24, 2048); cd_dlen = 2048; cd_dpos = 2048; /* hasta pedir BFRD */
            cd_push(0, 1, r, 1, K_SECTOR);
        } else eventf("CD_SECTOR_LOST lba %u", cd_cur);
        cd_cur++;
        cd_next_sector += 225792;
    }
    if (cd_playing && cyc >= cd_busy_until) {
        /* avanza al ritmo del audio (1x: 75 sectores/s) */
        static u64 last; if (!last || cyc - last > 451584) { last = cyc; cd_cur++; }
    }
    if ((cd_if & 7) == 0 && cdqn) {
        int best = -1;
        for (i = 0; i < cdqn; i++) if (cdq[i].t <= cyc && (best < 0 || cdq[i].t < cdq[best].t)) best = i;
        if (best >= 0) {
            cdev_t e = cdq[best];
            memmove(&cdq[best], &cdq[best + 1], (cdqn - best - 1) * sizeof(cdev_t)); cdqn--;
            cd_if = (cd_if & ~7) | e.irq;
            memcpy(cd_resp, e.resp, e.n); cd_nresp = e.n; cd_rpos = 0;
            if (e.irq == 1) cd_sector_ready = 1;
        }
    }
}
static u8 cd_rd(u32 r)
{
    switch (r) {
    case 0: {
        u8 s = cd_idx;
        if (cd_np == 0) s |= 0x08;
        if (cd_np < 16) s |= 0x10;
        if (cd_rpos < cd_nresp) s |= 0x20;
        if (cd_dpos < cd_dlen) s |= 0x40;
        return s; }
    case 1: return cd_rpos < cd_nresp ? cd_resp[cd_rpos++] : 0;
    case 2: return cd_dpos < cd_dlen ? cd_data[cd_dpos++] : 0;
    case 3: return (cd_idx & 1) ? (0xE0 | cd_if) : (0xE0 | cd_ie);
    }
    return 0;
}
static void cd_wr(u32 r, u8 v)
{
    if (r == 0) { cd_idx = v & 3; return; }
    switch (cd_idx * 4 + r) {
    case 1: cd_command(v); break;
    case 2: if (cd_np < 16) cd_param[cd_np++] = v; break;
    case 3: if (v & 0x80) { if (cd_sector_ready) { cd_dpos = 0; cd_dlen = 2048; cd_sector_ready = 0; } } else { cd_dlen = 0; cd_dpos = 0; } break;
    case 6: cd_ie = v & 0x1F; break;
    case 7: cd_if &= ~(v & 0x1F); if (v & 0x40) cd_np = 0; if (v & 7) { cd_nresp = 0; cd_rpos = 0; } break;
    default: break;
    }
}
static int load_cue(const char *cue)
{
    char line[512], bin[512] = "", dir[512]; FILE *f = fopen(cue, "r"); int t = 0; const char *sl;
    if (!f) return -1;
    strcpy(dir, cue); sl = strrchr(dir, '/'); if (sl) dir[sl - dir + 1] = 0; else dir[0] = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = strstr(line, "FILE \"");
        if (p) { char *q = strchr(p + 6, '"'); *q = 0; snprintf(bin, sizeof bin, "%s%s", dir, p + 6); }
        if (strstr(line, "TRACK")) sscanf(strstr(line, "TRACK") + 5, "%d", &t);
        if (strstr(line, "INDEX 01")) { int m, s, fr; sscanf(strstr(line, "INDEX 01") + 8, "%d:%d:%d", &m, &s, &fr); trk_lba[t] = (m * 60 + s) * 75 + fr; if (t > ntracks) ntracks = t; }
    }
    fclose(f);
    cdf = fopen(bin, "rb"); if (!cdf) return -2;
    fseek(cdf, 0, SEEK_END); cd_sectors = ftell(cdf) / 2352;
    return 0;
}

/* ---------------------------------------------------------------- mando (SIO0) */
static void joy_data_write(u8 v)
{
    u8 resp = 0xFF; int ack = 0;
    if (!(joy_ctrl & 2)) { joy_seq = -1; }
    if (joy_seq <= 0) {
        if (v == 0x01) { resp = 0xFF; ack = 1; joy_seq = 1; } else { resp = 0xFF; joy_seq = -1; }
    } else {
        switch (joy_seq) {
        case 1: resp = 0x41; ack = 1; break;
        case 2: resp = 0x5A; ack = 1; break;
        case 3: resp = pad_buttons & 0xFF; ack = 1; break;
        case 4: resp = pad_buttons >> 8; ack = 0; break;
        default: resp = 0xFF; ack = 0; break;
        }
        joy_seq++;
    }
    joy_rx = resp; joy_rx_valid = 0;
    joy_rx_at = cyc + (u64)(joy_baud ? joy_baud : 0x88) * 8;
    joy_tx_busy_until = cyc + 16;
    joy_ack_at = ack ? joy_rx_at + 100 : 0;
}
static u16 joy_stat(void)
{
    u16 s = 0;
    /* bit0: listo para enviar (salvo mientras se transmite el byte), bit2: envio terminado, bit1: hay byte recibido */
    if (!joy_tx_busy_until || cyc >= joy_tx_busy_until) s |= 1 | 4;
    if (joy_rx_at != (u64)-1 && cyc >= joy_rx_at) s |= 2;
    if (joy_ack_at && cyc >= joy_ack_at && cyc < joy_ack_at + 200) s |= 0x80;
    return s;
}
/* ---------------------------------------------------------------- bus */
static u32 io_read(u32 a, int sz, int *cost)
{
    u32 o = a - 0x1F801000;
    *cost = IO_MISC;
    if (o >= 0x800 && o < 0x804) { *cost = IO_CD; return cd_rd(o & 3); }
    if (o == 0x810) { *cost = IO_GPU; return 0; }
    if (o == 0x814) { *cost = IO_GPU; return gpustat(); }
    if (o >= 0xC00 && o < 0xE00) {
        u32 r = (o - 0xC00) / 2;
        *cost = IO_SPU;
        if (o == 0xDAE) return spu[(0xDAA - 0xC00) / 2] & 0x3F;
        if (o == 0xD9C || o == 0xD9E) return 0xFFFF;   /* ENDX: todas terminadas */
        return spu[r];
    }
    switch (o) {
    case 0x070: return I_STAT;
    case 0x074: return I_MASK;
    case 0x040: { u8 v = cyc >= joy_rx_at ? joy_rx : 0xFF; joy_rx_at = cyc >= joy_rx_at ? (u64)-1 : joy_rx_at; return v; }
    case 0x044: return joy_stat();
    case 0x048: return joy_mode;
    case 0x04A: return joy_ctrl;
    case 0x04E: return joy_baud;
    case 0x0F0: return DPCR;
    case 0x0F4: return DICR;
    case 0x100: return (u32)(((cyc - t0_base)) & 0xFFFF);
    case 0x110: return (u32)((u64)((cyc - t1_base) / LINE_CYC) & 0xFFFF);
    case 0x114: return t1_mode;
    case 0x120: return (u32)(((cyc - t2_base) / ((t2_mode & 0x200) ? 8 : 1)) & 0xFFFF);
    }
    if (o >= 0x080 && o < 0x0F0) {
        int ch = (o - 0x080) / 16, r = (o & 15) / 4;
        if (r == 0) return dma_madr[ch];
        if (r == 1) return dma_bcr[ch];
        if (r == 2) { if ((dma_chcr[ch] & 0x01000000) && cyc >= dma_done[ch]) dma_chcr[ch] &= ~0x11000000; return dma_chcr[ch]; }
    }
    (void)sz;
    return 0;
}
static void io_write(u32 a, u32 v, int sz, int *cost)
{
    u32 o = a - 0x1F801000;
    *cost = IO_MISC;
    if (o >= 0x800 && o < 0x804) { *cost = IO_CD; cd_wr(o & 3, (u8)v); return; }
    if (o == 0x810) { *cost = IO_GPU; gp0_write(v); return; }
    if (o == 0x814) { *cost = IO_GPU; gp1_write(v); return; }
    if (o >= 0xC00 && o < 0xE00) {
        u32 r = (o - 0xC00) / 2;
        *cost = IO_SPU;
        spu[r] = (u16)v;
        if (o == 0xDA6) spu_addr = (v & 0xFFFF) * 8;
        if (o == 0xD88 || o == 0xD8A) {   /* KON */
            int vb = o == 0xD88 ? 0 : 16, k;
            for (k = 0; k < 16; k++) if (v & (1 << k)) eventf("SPU_KON voz %d addr %u", vb + k, spu[(vb + k) * 8 + 3] * 8);
        }
        if (o == 0xD8C) { int k; for (k = 0; k < 16; k++) if (v & (1 << k)) eventf("SPU_KOFF voz %d", k); }
        return;
    }
    switch (o) {
    case 0x070: I_STAT &= v; return;
    case 0x074: I_MASK = v; return;
    case 0x040: joy_data_write((u8)v); return;
    case 0x048: joy_mode = v; return;
    case 0x04A: joy_ctrl = v; if (!(v & 2)) joy_seq = 0; if (v & 0x40) { joy_seq = 0; } return;
    case 0x04E: joy_baud = v; return;
    case 0x0F0: DPCR = v; return;
    case 0x0F4: DICR = v; return;
    case 0x100: t0_base = cyc; return;
    case 0x104: t0_mode = v; t0_base = cyc; return;
    case 0x110: t1_base = cyc; return;
    case 0x114: t1_mode = v; t1_base = cyc; return;
    case 0x120: t2_base = cyc; return;
    case 0x124: t2_mode = v; t2_base = cyc; return;
    }
    if (o >= 0x080 && o < 0x0F0) {
        int ch = (o - 0x080) / 16, r = (o & 15) / 4;
        if (r == 0) dma_madr[ch] = v & 0xFFFFFF;
        if (r == 1) dma_bcr[ch] = v;
        if (r == 2) { dma_chcr[ch] = v; if (v & 0x01000000) dma_start(ch); }
    }
    (void)sz;
}
static u32 mem_read(u32 a, int sz)
{
    u32 p = a & 0x1FFFFFFF;
    int c = 0; u32 v;
    if (a == 0xFFFE0130) return cache_ctrl;
    if (p < 0x800000) {
        p &= 0x1FFFFF;
        stall_cyc += RAM_LD;
        if (sz == 1) return ram[p];
        if (sz == 2) return ram[p] | (ram[p + 1] << 8);
        return ram[p] | (ram[p + 1] << 8) | (ram[p + 2] << 16) | ((u32)ram[p + 3] << 24);
    }
    if (p >= 0x1F800000 && p < 0x1F800400) {
        p &= 0x3FF;
        if (sz == 1) return scratch[p];
        if (sz == 2) return scratch[p] | (scratch[p + 1] << 8);
        return scratch[p] | (scratch[p + 1] << 8) | (scratch[p + 2] << 16) | ((u32)scratch[p + 3] << 24);
    }
    if (p >= 0x1F801000 && p < 0x1F803000) {
        u32 al = p & ~3;
        v = io_read(sz == 1 ? p : (sz == 2 ? p & ~1 : al), sz, &c);
        stall_cyc += c;
        if (sz == 1 && !(p >= 0x1F801800 && p < 0x1F801804) && p != 0x1F801040) v >>= (p & 3) * 8;
        return sz == 1 ? v & 0xFF : sz == 2 ? v & 0xFFFF : v;
    }
    return 0;
}
static void mem_write(u32 a, u32 v, int sz)
{
    u32 p = a & 0x1FFFFFFF;
    int c = 0;
    if (a == 0xFFFE0130) { cache_ctrl = v; return; }
    if (SR & 0x10000) return;   /* cache aislada */
    if (p < 0x800000) {
        p &= 0x1FFFFF;
        ram[p] = v; if (sz >= 2) ram[p + 1] = v >> 8; if (sz == 4) { ram[p + 2] = v >> 16; ram[p + 3] = v >> 24; }
        return;
    }
    if (p >= 0x1F800000 && p < 0x1F800400) {
        p &= 0x3FF;
        scratch[p] = v; if (sz >= 2) scratch[p + 1] = v >> 8; if (sz == 4) { scratch[p + 2] = v >> 16; scratch[p + 3] = v >> 24; }
        return;
    }
    if (p >= 0x1F801000 && p < 0x1F803000) { io_write(p, v, sz, &c); stall_cyc += c; }
}

/* ---------------------------------------------------------------- CPU */
static u32 fetch(u32 a)
{
    u32 p = a & 0x1FFFFF;
    if ((a >> 29) == 0 || (a >> 29) == 4) {
        u32 line = (a >> 4) & 255, tag = a & 0x1FFFF000 & ~0xFFF, w = (a >> 2) & 3;
        tag = (a & 0x1FFFFFF0) >> 12;
        if (icache_tag[line] != tag || !(icache_valid[line] & (1u << w))) {
            int n = 4 - w;
            stall_cyc += ICF_BASE + ICF_WORD * n;
            if (icache_tag[line] != tag) icache_valid[line] = 0;
            icache_tag[line] = tag;
            icache_valid[line] |= (0xF << w) & 0xF;
        }
    } else stall_cyc += RAM_LD;
    return ram[p] | (ram[p + 1] << 8) | (ram[p + 2] << 16) | ((u32)ram[p + 3] << 24);
}
#define RS ((op >> 21) & 31)
#define RT ((op >> 16) & 31)
#define RD ((op >> 11) & 31)
#define IMM ((s32)(s16)op)
#define IMMU (op & 0xFFFF)
static int wr_reg;
static void setr(int r, u32 v) { if (r) { R[r] = v; wr_reg = r; } }
static void load(int r, u32 v) { if (r) { ld_r = r; ld_v = v; } }
static void branch(u32 t) { NPC = t; }
static void muldiv_wait(void) { if (muldiv_ready > cyc + stall_cyc) stall_cyc = muldiv_ready - cyc; }
static void exception(int code)
{
    fprintf(stderr, "excepcion %d en %08X (ciclo %llu, cuadro %ld)\n", code, PC - 4, (unsigned long long)cyc, game_frame);
    if (code == 9) fprintf(stderr, "  break %x\n", (fetch(PC - 4) >> 6) & 0xFFFFF);
    halted = 1;
}
static void step(void)
{
    u32 pc = PC, op, s, t;
    int pend_r = ld_r; u32 pend_v = ld_v;
    stall_cyc = 0;
    op = fetch(pc);
    PC = NPC; NPC += 4;
    ld_r = 0; wr_reg = 0;
    s = R[RS]; t = R[RT];
    switch (op >> 26) {
    case 0x00:
        switch (op & 63) {
        case 0x00: setr(RD, t << ((op >> 6) & 31)); break;
        case 0x02: setr(RD, t >> ((op >> 6) & 31)); break;
        case 0x03: setr(RD, (u32)((s32)t >> ((op >> 6) & 31))); break;
        case 0x04: setr(RD, t << (s & 31)); break;
        case 0x06: setr(RD, t >> (s & 31)); break;
        case 0x07: setr(RD, (u32)((s32)t >> (s & 31))); break;
        case 0x08: branch(s); break;
        case 0x09: setr(RD, pc + 8); branch(s); break;
        case 0x0C: exception(8); break;
        case 0x0D: exception(9); break;
        case 0x10: muldiv_wait(); setr(RD, HI); break;
        case 0x11: HI = s; break;
        case 0x12: muldiv_wait(); setr(RD, LO); break;
        case 0x13: LO = s; break;
        case 0x18: { s64 r = (s64)(s32)s * (s32)t; LO = (u32)r; HI = (u32)(r >> 32);
            u32 a = (s32)s < 0 ? ~s : s; muldiv_ready = cyc + (a < 0x800 ? 6 : a < 0x100000 ? 9 : 13); break; }
        case 0x19: { u64 r = (u64)s * t; LO = (u32)r; HI = (u32)(r >> 32);
            muldiv_ready = cyc + (s < 0x800 ? 6 : s < 0x100000 ? 9 : 13); break; }
        case 0x1A: if (t == 0) { LO = (s32)s < 0 ? 1 : 0xFFFFFFFF; HI = s; } else if (s == 0x80000000 && t == 0xFFFFFFFF) { LO = s; HI = 0; } else { LO = (u32)((s32)s / (s32)t); HI = (u32)((s32)s % (s32)t); }
            muldiv_ready = cyc + 36; break;
        case 0x1B: if (t == 0) { LO = 0xFFFFFFFF; HI = s; } else { LO = s / t; HI = s % t; } muldiv_ready = cyc + 36; break;
        case 0x20: case 0x21: setr(RD, s + t); break;
        case 0x22: case 0x23: setr(RD, s - t); break;
        case 0x24: setr(RD, s & t); break;
        case 0x25: setr(RD, s | t); break;
        case 0x26: setr(RD, s ^ t); break;
        case 0x27: setr(RD, ~(s | t)); break;
        case 0x2A: setr(RD, (s32)s < (s32)t); break;
        case 0x2B: setr(RD, s < t); break;
        default: exception(10); break;
        }
        break;
    case 0x01: {
        int rt = RT, cond = (rt & 1) ? (s32)s >= 0 : (s32)s < 0;
        if ((rt & 0x1E) == 0x10) setr(31, pc + 8);
        if (cond) branch(pc + 4 + (IMM << 2));
        break; }
    case 0x02: branch((pc & 0xF0000000) | ((op & 0x3FFFFFF) << 2)); break;
    case 0x03: setr(31, pc + 8); branch((pc & 0xF0000000) | ((op & 0x3FFFFFF) << 2)); break;
    case 0x04: if (s == t) branch(pc + 4 + (IMM << 2)); break;
    case 0x05: if (s != t) branch(pc + 4 + (IMM << 2)); break;
    case 0x06: if ((s32)s <= 0) branch(pc + 4 + (IMM << 2)); break;
    case 0x07: if ((s32)s > 0) branch(pc + 4 + (IMM << 2)); break;
    case 0x08: case 0x09: setr(RT, s + IMM); break;
    case 0x0A: setr(RT, (s32)s < IMM); break;
    case 0x0B: setr(RT, s < (u32)IMM); break;
    case 0x0C: setr(RT, s & IMMU); break;
    case 0x0D: setr(RT, s | IMMU); break;
    case 0x0E: setr(RT, s ^ IMMU); break;
    case 0x0F: setr(RT, IMMU << 16); break;
    case 0x10: {
        u32 rs = RS;
        if (rs == 0) { u32 v = 0; int rd = RD; if (rd == 12) v = SR; else if (rd == 13) v = CAUSE; else if (rd == 14) v = EPC; else if (rd == 15) v = 2; load(RT, v); }
        else if (rs == 4) { int rd = RD; if (rd == 12) SR = t; else if (rd == 13) CAUSE = t; }
        else if (rs == 16 && (op & 63) == 0x10) SR = (SR & ~15) | ((SR >> 2) & 15);
        break; }
    case 0x20: load(RT, (u32)(s32)(s8)mem_read(s + IMM, 1)); break;
    case 0x21: { u32 a = s + IMM; if (a & 1) { exception(4); break; } load(RT, (u32)(s32)(s16)mem_read(a, 2)); break; }
    case 0x22: { u32 a = s + IMM, w = mem_read(a & ~3, 4), cur = (pend_r == (int)RT) ? pend_v : t, sh = a & 3;
        u32 v = (cur & (0x00FFFFFF >> (sh * 8))) | (w << (24 - sh * 8)); load(RT, v); break; }
    case 0x23: { u32 a = s + IMM; if (a & 3) { exception(4); break; } load(RT, mem_read(a, 4)); break; }
    case 0x24: load(RT, mem_read(s + IMM, 1) & 0xFF); break;
    case 0x25: { u32 a = s + IMM; if (a & 1) { exception(4); break; } load(RT, mem_read(a, 2) & 0xFFFF); break; }
    case 0x26: { u32 a = s + IMM, w = mem_read(a & ~3, 4), cur = (pend_r == (int)RT) ? pend_v : t, sh = a & 3;
        u32 v = (cur & (0xFFFFFF00u << ((3 - sh) * 8))) | (w >> (sh * 8)); load(RT, v); break; }
    case 0x28: mem_write(s + IMM, t, 1); break;
    case 0x29: { u32 a = s + IMM; if (a & 1) { exception(5); break; } mem_write(a, t, 2); break; }
    case 0x2A: { u32 a = s + IMM, w = mem_read(a & ~3, 4), sh = a & 3; stall_cyc -= RAM_LD;
        w = (w & (0xFFFFFF00u << (sh * 8))) | (t >> (24 - sh * 8)); mem_write(a & ~3, w, 4); break; }
    case 0x2B: { u32 a = s + IMM; if (a & 3) { exception(5); break; } mem_write(a, t, 4); break; }
    case 0x2E: { u32 a = s + IMM, w = mem_read(a & ~3, 4), sh = a & 3; stall_cyc -= RAM_LD;
        w = (w & (0x00FFFFFF >> ((3 - sh) * 8))) | (t << (sh * 8)); mem_write(a & ~3, w, 4); break; }
    case 0x12: case 0x32: case 0x3A: break;   /* GTE: no se usa */
    default: exception(10); break;
    }
    if (pend_r && pend_r != wr_reg) R[pend_r] = pend_v;
    R[0] = 0;
    {
        u64 c = 1 + stall_cyc;
        cyc += c;
        if (prof_on && cyc >= prof_from_cyc && (pc & 0x1FFFFFFF) < 0x200000) { prof[(pc & 0x1FFFFF) >> 2] += c; prof_n[(pc & 0x1FFFFF) >> 2]++; }
    }
}

/* ---------------------------------------------------------------- simbolos */
static u32 sym_lookup(const char *mapfile, const char *name)
{
    char line[512]; FILE *f = fopen(mapfile, "r"); u32 v = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        unsigned a; char ty, nm[400];
        if (sscanf(line, "%x %c %399s", &a, &ty, nm) == 3 && !strcmp(nm, name)) { v = a; break; }
    }
    fclose(f);
    return v;
}

int main(int argc, char **argv)
{
    FILE *f; u8 hdr[0x800]; u32 pc0, taddr, tsize, sp;
    long maxcyc_frames;
    u64 next_cd = 0;
    const char *nmfile;
    if (argc < 5) { fprintf(stderr, "uso: psxprof imagen.cue exe nm.txt cuadros [dir]\n"); return 1; }
    if (load_cue(argv[1])) { fprintf(stderr, "no se pudo abrir %s\n", argv[1]); return 1; }
    f = fopen(argv[2], "rb"); if (!f) return 1;
    if (fread(hdr, 1, 0x800, f) != 0x800) return 1;
    pc0 = hdr[0x10] | hdr[0x11] << 8 | hdr[0x12] << 16 | (u32)hdr[0x13] << 24;
    taddr = hdr[0x18] | hdr[0x19] << 8 | hdr[0x1A] << 16 | (u32)hdr[0x1B] << 24;
    tsize = hdr[0x1C] | hdr[0x1D] << 8 | hdr[0x1E] << 16 | (u32)hdr[0x1F] << 24;
    if (fread(ram + (taddr & 0x1FFFFF), 1, tsize, f) != tsize) { fprintf(stderr, "exe corto\n"); return 1; }
    fclose(f);
    nmfile = argv[3];
    max_game_frames = atol(argv[4]);
    if (argc > 5) outdir = argv[5];
    if (getenv("RAM_LD")) RAM_LD = atoi(getenv("RAM_LD"));
    if (getenv("ICF_BASE")) ICF_BASE = atoi(getenv("ICF_BASE"));
    if (getenv("ICF_WORD")) ICF_WORD = atoi(getenv("ICF_WORD"));
    if (getenv("SHOT")) shot_every = atol(getenv("SHOT"));
    if (getenv("PROF_FROM")) prof_from_frame = atol(getenv("PROF_FROM"));
    sym_vblank = sym_lookup(nmfile, "rc_vblank");
    if (!sym_vblank) { fprintf(stderr, "sin rc_vblank en %s\n", nmfile); return 1; }
    sym_ram = sym_lookup(nmfile, "rc_ram"); sym_vram = sym_lookup(nmfile, "rc_vram"); sym_pal = sym_lookup(nmfile, "rc_pal"); sym_spr = sym_lookup(nmfile, "rc_spr"); sym_vregs = sym_lookup(nmfile, "rc_vregs");
    prof = calloc(0x80000, 8); prof_n = calloc(0x80000, 8);
    { char fn[600]; snprintf(fn, sizeof fn, "%s/eventos.txt", outdir); evf = fopen(fn, "w");
      snprintf(fn, sizeof fn, "%s/cuadros.csv", outdir); framef = fopen(fn, "w"); }
    if (framef) fprintf(framef, "cuadro_juego,ciclo,vblank,ciclos_desde_anterior,gpu_ticks,estado\n");
    sp = 0x801FFFF0;
    R[29] = sp; R[30] = sp;
    PC = pc0; NPC = pc0 + 4;
    SR = 0;
    frame_cyc = (u64)(LINE_CYC * LINES);
    next_vbl = frame_cyc;
    joy_rx_at = (u64)-1;
    maxcyc_frames = 0; (void)maxcyc_frames;
    while (!halted) {
        if ((PC & 0x1FFFFFFF) == (sym_vblank & 0x1FFFFFFF) && PC == sym_vblank) {
            u64 d = cyc - last_vblank_call;
            if (framef) fprintf(framef, "%ld,%llu,%llu,%llu,%llu,%08x\n", game_frame, (unsigned long long)cyc, (unsigned long long)vbl_count, (unsigned long long)d, (unsigned long long)gpu_ticks_total, state_hash());
            last_vblank_call = cyc;
            game_frame++;
            if (game_frame == prof_from_frame) { memset(prof, 0, 0x80000 * 8); memset(prof_n, 0, 0x80000 * 8); prof_from_cyc = cyc; }
            pad_buttons = inputs_for(game_frame);
            if (shot_every && game_frame % shot_every == 0) { char nm[64]; snprintf(nm, sizeof nm, "shot_%05ld", game_frame); screenshot(nm); }
            if (game_frame >= max_game_frames) break;
        }
        step();
        while (cyc >= next_vbl) { I_STAT |= 1; next_vbl += frame_cyc; vbl_count++; }
        if (cyc >= next_cd) { cd_tick(); next_cd = cyc + 64; }
    }
    {
        char fn[600];
        snprintf(fn, sizeof fn, "%s/prof.bin", outdir); f = fopen(fn, "wb"); fwrite(prof, 8, 0x80000, f); fclose(f);
        snprintf(fn, sizeof fn, "%s/profn.bin", outdir); f = fopen(fn, "wb"); fwrite(prof_n, 8, 0x80000, f); fclose(f);
        screenshot("final");
    }
    if (evf) fclose(evf);
    if (framef) fclose(framef);
    fprintf(stderr, "perfil desde ciclo %llu (%.2f s)\n", (unsigned long long)prof_from_cyc, (cyc - prof_from_cyc) / CLK);
    fprintf(stderr, "fin: %ld cuadros de juego, %llu vblanks, %.2f s emulados, %.1f cuadros de juego/s\n", game_frame, (unsigned long long)vbl_count,
            cyc / CLK, game_frame / (cyc / CLK));
    return 0;
}

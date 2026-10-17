/* Emulacion del OKI M6295 sobre el YM2610 (ADPCM-A) mediante el controlador Z80 propio */
#include "ng.h"
extern const u16 snd_dur[128];
#define OKI_STATUS (*(vu8 *)0x10A00F)
static u8 oki_wait, oki_phrase, oki_busy;
static u16 oki_cnt[4];
u16 snd_timeouts; u8 snd_lastfail, snd_lastreply;

/* cola de bytes hacia el Z80: se envia un byte cuando el anterior ha sido confirmado.
   Se vacia desde oki_write y desde la IRQ de temporizador (8 veces por cuadro), sin esperas largas */
#define QN 64
static u8 q[QN], qh, qt, last, waiting;
static u16 wait_ticks;
void snd_pump(void)
{
    u16 i;
    for (i = 0; i < 64; i++) {
        if (waiting) {
            /* el Z80 responde con un contador que cambia con cada byte recibido */
            if (REG_SOUND == last) {
                if (++wait_ticks > 40000) { waiting = 0; snd_timeouts++; snd_lastfail = last; snd_lastreply = REG_SOUND; }
                else return;
            } else waiting = 0;
        }
        if (qh == qt) return;
        last = REG_SOUND;
        REG_SOUND = q[qt];
        qt = (qt + 1) & (QN - 1);
        waiting = 1; wait_ticks = 0;
    }
}
static void snd_send(u8 b)
{
    u8 nh = (qh + 1) & (QN - 1);
    if (nh == qt) { snd_timeouts++; return; }      /* cola llena: se pierde */
    q[qh] = b; qh = nh;
}

void oki_write(u32 v)
{
    u16 ch;
    if (oki_wait) {
        oki_wait = 0;
        for (ch = 0; ch < 4; ch++) {
            if (!(v & (0x10 << ch))) continue;
            if (oki_busy & (1 << ch)) continue;          /* el OKI ignora la orden si el canal suena */
            if (!snd_dur[oki_phrase]) continue;
            snd_send(0x40 | (ch << 4) | (v & 15));
            snd_send(0x80 | oki_phrase);
            oki_busy |= 1 << ch;
            oki_cnt[ch] = snd_dur[oki_phrase];
        }
    } else if (v & 0x80) {
        oki_phrase = v & 0x7F;
        oki_wait = 1;
    } else {
        u8 m = (v >> 3) & 15;
        if (m) {
            snd_send(0x10 | m);
            oki_busy &= ~m;
        }
    }
    OKI_STATUS = oki_busy;
    snd_pump();
}

void sound_frame(void)
{
    u16 ch;
    for (ch = 0; ch < 4; ch++)
        if (oki_cnt[ch] && !--oki_cnt[ch]) oki_busy &= ~(1 << ch);
    OKI_STATUS = oki_busy;
}

void sound_init(void)
{
    oki_wait = 0; oki_busy = 0; qh = qt = 0; waiting = 0;
    for (oki_phrase = 0; oki_phrase < 4; oki_phrase++) oki_cnt[oki_phrase] = 0;
    oki_phrase = 0;
    OKI_STATUS = 0;
}

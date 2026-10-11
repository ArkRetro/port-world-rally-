/* perfil: cuenta instrucciones por PC en una ventana de cuadros */
#include "rc_rt.h"
unsigned int pc_hist[0x80000]; unsigned long fr_cnt[100000]; extern long prof_frame;
long prof_frame; long prof_lo = 100, prof_hi = 500;
void rc_ck(u32 pc, u32 live) { (void)live; if (prof_frame >= prof_lo && prof_frame < prof_hi) pc_hist[(pc & 0xFFFFF) >> 1]++; fr_cnt[prof_frame]++; }
void rc_dbg_irq(void) {}

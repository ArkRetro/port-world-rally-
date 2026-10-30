#ifndef NG_H
#define NG_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef signed short s16; typedef signed int s32;
typedef volatile u8 vu8; typedef volatile u16 vu16; typedef volatile u32 vu32;
#define REG_VRAMADDR (*(vu16*)0x3C0000)
#define REG_VRAMRW   (*(vu16*)0x3C0002)
#define REG_VRAMMOD  (*(vu16*)0x3C0004)
#define REG_LSPCMODE (*(vu16*)0x3C0006)
#define REG_IRQACK   (*(vu16*)0x3C000C)
#define REG_WATCHDOG (*(vu8*)0x300001)
#define REG_P1CNT    (*(vu8*)0x300000)
#define REG_P2CNT    (*(vu8*)0x340000)
#define REG_STATUS_A (*(vu8*)0x320001)
#define REG_STATUS_B (*(vu8*)0x380000)
#define REG_DIPSW    (*(vu8*)0x300001)
#define REG_SOUND    (*(vu8*)0x320000)
#define PALRAM ((vu16*)0x400000)
#define BACKDROP (*(vu16*)0x401FFE)
static inline void vram_set(u16 a, u16 v){ REG_VRAMADDR=a; REG_VRAMRW=v; }
#endif

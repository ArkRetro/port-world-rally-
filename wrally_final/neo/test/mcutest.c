#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/mcs51.h"
#define REC (1+16384+256+14)
static unsigned char rom[0x8000];
int main(void){
  FILE*f=fopen("/home/claude/wrally/rom/wrdallas.bin","rb"); fread(rom,1,0x8000,f); fclose(f);
  f=fopen("/home/claude/wrally/mame/mcucap.bin","rb");
  static unsigned char b[REC], a[REC]; static unsigned char x[16384];
  int pair=0, bad=0;
  while(fread(b,1,REC,f)==REC){
    if(b[0]!='B') continue;
    if(fread(a,1,REC,f)!=REC) break;
    if(a[0]!='A'){ printf("desincronizado\n"); break; }
    unsigned char hb[2]; fread(hb,1,2,f); int nw=(hb[0]<<8)|hb[1];
    static unsigned char wl[3*65536]; fread(wl,3,nw,f);
    mcs51_t c; memset(&c,0,sizeof c);
    c.code=rom; memcpy(x,b+1,16384); c.xram=x;
    memcpy(c.iram,b+1+16384,256);
    unsigned char *r=b+1+16384+256;
    #define G(i) ((r[2*(i)]<<8)|r[2*(i)+1])
    c.pc=G(0); c.sfr[0x60]=G(1); c.sfr[0x70]=G(2); c.sfr[0x50]=G(3); c.sfr[0x01]=G(4);
    c.sfr[0x03]=G(5)>>8; c.sfr[0x02]=G(5)&255; c.sfr[0x20]=G(6);
    long n=mcs51_run(&c,0x0186,1000000);
    int diff=0, first=-1;
    /* reproducir las escrituras reales de la MCU sobre la copia inicial y comparar */
    static unsigned char ref[16384]; memcpy(ref,b+1,16384);
    for(int k=0;k<nw;k++){ int o=((wl[3*k]<<8)|wl[3*k+1])&0x3fff; if(o!=0x101) ref[o]=wl[3*k+2]; }
    for(int i=0;i<16384;i++) if(i!=0x101 && x[i]!=ref[i]){ if(first<0) first=i; diff++; }
    int idiff=0; for(int i=0;i<128;i++) if(c.iram[i]!=a[1+16384+i]) idiff++;
    printf("par %2d pc0=%04x pasos=%6ld fin=%04x difs_xram=%d (primera %04x) difs_iram=%d C229=%02x escr=%d\n",pair,G(0),n,c.pc,diff,first,idiff,b[1+0x229],nw);
    if(diff) bad++;
    pair++;
  }
  printf("pares %d malos %d\n",pair,bad);
}

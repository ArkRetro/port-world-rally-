/* Codificador ADPCM-A (YM2610) de PCM 16 bits con busqueda voraz con anticipacion de 1 muestra.
   uso: adpcma_enc in.raw(int16 LE) out.bin */
#include <stdio.h>
#include <stdlib.h>
static const int steps[49]={16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552};
static const int inc[8]={-1,-1,-1,-1,2,5,7,9};
static int jedi[49*16];
static void step(int *acc,int *si,int n){
    *acc=(*acc+jedi[*si*16+n])&0xfff; *si+=inc[n&7]; if(*si<0)*si=0; if(*si>48)*si=48;
}
static int sx(int a){ return a&0x800? a-0x1000 : a; }
int main(int argc,char**argv){
    for(int s=0;s<49;s++) for(int n=0;n<16;n++){ int v=(2*(n&7)+1)*steps[s]/8; jedi[s*16+n]=(n&8)?-v:v; }
    FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f)/2; fseek(f,0,SEEK_SET);
    short*in=malloc(n*2+4); fread(in,2,n,f); fclose(f);
    if(n&1){ in[n]=0; n++; }
    unsigned char*out=malloc(n/2+1);
    int acc=0,si=0;
    for(long i=0;i<n;i++){
        int target=in[i]>>4, best=0; long beste=-1;
        for(int c=0;c<16;c++){
            int a=acc,s=si; step(&a,&s,c);
            long e=(long)(sx(a)-target)*(sx(a)-target);
            /* anticipacion: mejor siguiente */
            if(i+1<n){ int t2=in[i+1]>>4; long b2=-1;
                for(int d=0;d<16;d++){ int a2=a,s2=s; step(&a2,&s2,d); long e2=(long)(sx(a2)-t2)*(sx(a2)-t2); if(b2<0||e2<b2)b2=e2; }
                e+=b2; }
            if(beste<0||e<beste){beste=e;best=c;}
        }
        step(&acc,&si,best);
        if(i&1) out[i/2]|=best; else out[i/2]=best<<4;
    }
    f=fopen(argv[2],"wb"); fwrite(out,1,n/2,f); fclose(f); return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char seen[1<<24];
int main(int argc,char**argv){
  char line[512]; long n=0;
  while(fgets(line,sizeof line,stdin)){
    char*e; unsigned long pc=strtoul(line,&e,16);
    if(e!=line && *e==':'){ seen[pc&0xffffff]=1; n++; }
  }
  FILE*f=fopen(argv[1],"w"); unsigned long c=0;
  for(unsigned i=0;i<(1u<<24);i++) if(seen[i]){fprintf(f,"%06x\n",i);c++;}
  fclose(f); fprintf(stderr,"lines %ld unique %lu\n",n,c); return 0;
}

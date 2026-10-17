#include <windows.h>
#include <stdio.h>
#include <stdint.h>
typedef int(*i0)(void); typedef int(*iopen)(int,int,int,void*); typedef int(*irs)(int,void*);
int main(int c,char**v){
  HMODULE m=LoadLibraryA(v[1]); if(!m){printf("load fail %lu\n",GetLastError());return 1;}
  ((i0)GetProcAddress(m,"scePadInit"))();
  int h=((iopen)GetProcAddress(m,"scePadOpen"))(1,0,0,0);
  irs rs=(irs)GetProcAddress(m,"scePadReadState");
  uint8_t d[0x78];
  for(int i=0;i<30;i++){ int r=rs(h,d); printf("r=%x conn=%d btn=%08x LX=%d LY=%d RX=%d RY=%d L2=%d R2=%d\n",r,d[0x4c],*(uint32_t*)d,d[4],d[5],d[6],d[7],d[8],d[9]); Sleep(100);}
  return 0;}

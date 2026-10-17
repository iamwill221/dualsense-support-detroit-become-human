#include <windows.h>
#include <stdio.h>
#include <stdint.h>
typedef int(*i0)(void); typedef int(*iopen)(int,int,int,void*); typedef int(*irs)(int,void*); typedef int(*iv)(int,const void*);
static double ms(LARGE_INTEGER a,LARGE_INTEGER b,LARGE_INTEGER f){return (b.QuadPart-a.QuadPart)*1000.0/f.QuadPart;}
int main(int c,char**v){
  HMODULE m=LoadLibraryA(v[1]); if(!m){printf("load fail\n");return 1;}
  ((i0)GetProcAddress(m,"scePadInit"))();
  int h=((iopen)GetProcAddress(m,"scePadOpen"))(1,0,0,0);
  irs rs=(irs)GetProcAddress(m,"scePadReadState"); iv vib=(iv)GetProcAddress(m,"scePadSetVibration"); iv lb=(iv)GetProcAddress(m,"scePadSetLightBar");
  uint8_t d[0x78]; rs(h,d); Sleep(300);
  LARGE_INTEGER f,a,b; QueryPerformanceFrequency(&f); double worst=0,tot=0;
  for(int i=0;i<500;i++){ uint8_t p[2]={(uint8_t)(i%2?120:0),(uint8_t)(i%2?60:0)}; uint8_t col[4]={0,0,255,0};
    QueryPerformanceCounter(&a); rs(h,d); vib(h,p); lb(h,col); QueryPerformanceCounter(&b);
    double t=ms(a,b,f); tot+=t; if(t>worst)worst=t; Sleep(2);}
  uint8_t z[2]={0,0}; vib(h,z); Sleep(100);
  printf("per-frame pad calls: avg %.3f ms, worst %.3f ms, connected=%d\n",tot/500,worst,d[0x4c]);
  return 0;}

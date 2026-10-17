#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
static BOOL CALLBACK cb(LPCDIDEVICEINSTANCEW d, LPVOID r){ printf("  device %ls product=%08lx\n", d->tszProductName, d->guidProduct.Data1); return DIENUM_CONTINUE; }
static void run(const char*tag){ IDirectInput8W*di; printf("%s:\n",tag);
  if(FAILED(DirectInput8Create(GetModuleHandle(NULL),0x0800,&IID_IDirectInput8W,(void**)&di,NULL))){puts("create fail");return;}
  di->lpVtbl->EnumDevices(di,DI8DEVCLASS_GAMECTRL,cb,NULL,DIEDFL_ATTACHEDONLY); di->lpVtbl->Release(di);}
int main(int c,char**v){ run("before proxy"); if(!LoadLibraryA(v[1])) {puts("load fail");return 1;} run("after proxy"); return 0;}

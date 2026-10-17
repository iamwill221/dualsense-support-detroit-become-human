// Prints the motion fields of ScePadData for player 1 (accel g, gyro rad/s, orientation quaternion).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef int (*i0)(void);
typedef int (*iopen)(int, int, int, void *);
typedef int (*irs)(int, void *);
typedef int (*ih)(int);
int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: motiontest <libScePad.dll> [seconds]\n"); return 1; }
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) { printf("load fail\n"); return 1; }
    int secs = argc > 2 ? atoi(argv[2]) : 5;
    ((i0)GetProcAddress(m, "scePadInit"))();
    int h = ((iopen)GetProcAddress(m, "scePadOpen"))(1, 0, 0, 0);
    irs rs = (irs)GetProcAddress(m, "scePadReadState");
    ((ih)GetProcAddress(m, "scePadResetOrientation"))(h);
    uint8_t d[0x78];
    for (int i = 0; i < secs * 4; i++) {
        rs(h, d);
        float *f = (float *)(d + 0x0c);
        printf("q[% .3f % .3f % .3f % .3f] acc[% .3f % .3f % .3f] gyro[% .3f % .3f % .3f] conn=%d\n",
               f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], d[0x4c]);
        fflush(stdout);
        Sleep(250);
    }
    return 0;
}

// Proxy libScePad.dll for Detroit: Become Human (PC).
// Forwards to libScePad_orig.dll (the game's DS4-only Sony pad library) and, when
// player 1 has no DS4 connected, serves a DualSense (054C:0CE6 / 0DF2) as a DS4.
// Also hides the DualSense from the game's DirectInput so it isn't seen twice.
// Build with -DDS_PROXY_DEBUG to log every button / stick change.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define VID_SONY 0x054C
#define PID_DUALSENSE 0x0CE6
#define PID_DUALSENSE_EDGE 0x0DF2

#define SCE_PAD_ERROR_INVALID_HANDLE 0x80920003

// ---------------------------------------------------------------- logging
static void logf_(const char *fmt, ...) {
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (!n) return;
    strcat(path, "dualsense_proxy.log");
    FILE *f = fopen(path, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
#include <stdarg.h>
#define LOG(...) logf_(__VA_ARGS__)

// ---------------------------------------------------------------- forwarding
static HMODULE g_orig;
typedef INT_PTR (*gen_fn)(INT_PTR, INT_PTR, INT_PTR, INT_PTR, INT_PTR, INT_PTR);
static gen_fn g_fn[19];
static const char *g_names[19] = {
    "scePadClose", "scePadGetControllerInformation", "scePadGetJackState",
    "scePadGetParticularMode", "scePadInit", "scePadIsSupportedAudioFunction",
    "scePadOpen", "scePadRead", "scePadReadState", "scePadResetLightBar",
    "scePadResetOrientation", "scePadSetAngularVelocityDeadbandState",
    "scePadSetAudioOutPath", "scePadSetLightBar", "scePadSetMotionSensorState",
    "scePadSetParticularMode", "scePadSetTiltCorrectionState",
    "scePadSetVibration", "scePadSetVolumeGain"};
enum { F_Close, F_GetControllerInformation, F_GetJackState, F_GetParticularMode,
       F_Init, F_IsSupportedAudioFunction, F_Open, F_Read, F_ReadState,
       F_ResetLightBar, F_ResetOrientation, F_SetAngVelDeadband, F_SetAudioOutPath,
       F_SetLightBar, F_SetMotionSensorState, F_SetParticularMode,
       F_SetTiltCorrectionState, F_SetVibration, F_SetVolumeGain };

static void load_orig(void) {
    if (g_orig) return;
    char path[MAX_PATH];
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&load_orig, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = 0;
    strcat(path, "libScePad_orig.dll");
    g_orig = LoadLibraryA(path);
    LOG("load_orig(%s) -> %p", path, (void *)g_orig);
    if (!g_orig) return;
    for (int i = 0; i < 19; i++) g_fn[i] = (gen_fn)GetProcAddress(g_orig, g_names[i]);
}
static int fwd(int idx, INT_PTR a, INT_PTR b, INT_PTR c, INT_PTR d, INT_PTR e, INT_PTR f) {
    load_orig();
    if (!g_fn[idx]) return (int)0x809200ff;
    return (int)g_fn[idx](a, b, c, d, e, f);
}

// ---------------------------------------------------------------- DualSense
static CRITICAL_SECTION g_cs;
static int g_cs_init;
static HANDLE g_dev = INVALID_HANDLE_VALUE;
static HANDLE g_thread;
static HANDLE g_wdev = INVALID_HANDLE_VALUE; // separate handle so writes never wait on the blocking read
static HANDLE g_wthread, g_wevent;
static uint32_t g_out_sent = 0xFFFFFFFF, g_out_want;
static WCHAR g_path[512];
static int g_is_bt;
static USHORT g_in_len, g_out_len;
static volatile LONG g_alive;
static uint8_t g_report[128];
static int g_have_report;
static uint8_t g_bt_seq;
// last requested output
static uint8_t g_rumble_small, g_rumble_large, g_lr, g_lg, g_lb;
static int g_lb_set;

static uint32_t crc32_bt(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    uint8_t pre = 0xA2;
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t *p = pass ? d : &pre;
        size_t len = pass ? n : 1;
        for (size_t i = 0; i < len; i++) {
            c ^= p[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(int32_t)(c & 1));
        }
    }
    return ~c;
}

static void ds_write_report(void) {
    if (g_wdev == INVALID_HANDLE_VALUE) return;
    uint8_t buf[128];
    memset(buf, 0, sizeof buf);
    uint8_t *c; // common block
    if (g_is_bt) {
        buf[0] = 0x31;
        buf[1] = (uint8_t)((g_bt_seq++ & 0x0F) << 4);
        buf[2] = 0x10;
        c = buf + 3;
    } else {
        buf[0] = 0x02;
        c = buf + 1;
    }
    c[0] = 0x01 | 0x02;              // compat vibration + haptics select
    c[1] = 0x04;                     // lightbar control enable
    c[2] = g_rumble_small;           // right (high freq) motor
    c[3] = g_rumble_large;           // left (low freq) motor
    c[38] = 0x02;                    // valid_flag2: lightbar setup control
    c[41] = 0x02;                    // lightbar_setup: release default fade
    c[42] = 0x00;                    // brightness
    c[43] = 0x04;                    // player LEDs: center = player 1
    if (g_lb_set) { c[44] = g_lr; c[45] = g_lg; c[46] = g_lb; }
    else { c[44] = 0; c[45] = 0; c[46] = 0x40; }
    DWORD len;
    if (g_is_bt) {
        len = 78;
        uint32_t crc = crc32_bt(buf, 74);
        memcpy(buf + 74, &crc, 4);
    } else {
        len = g_out_len ? g_out_len : 64;
    }
    DWORD wr = 0;
    if (!WriteFile(g_wdev, buf, len, &wr, NULL))
        LOG("ds_write_report WriteFile failed err=%lu", GetLastError());
}

// Output goes through a worker thread; the game thread only records the request.
static DWORD WINAPI ds_writer(LPVOID unused) {
    (void)unused;
    while (WaitForSingleObject(g_wevent, INFINITE) == WAIT_OBJECT_0 && g_alive) {
        uint32_t want = g_out_want;
        if (want == g_out_sent) continue;
        g_out_sent = want;
        ds_write_report();
    }
    return 0;
}

static void ds_send_output(void) {
    g_out_want = (uint32_t)g_rumble_small | ((uint32_t)g_rumble_large << 8) |
                 (g_lb_set ? ((uint32_t)((g_lr >> 2) ^ (g_lg << 2) ^ (g_lb << 5)) << 16) | 0x80000000u : 0);
    if (g_out_want != g_out_sent && g_wevent) SetEvent(g_wevent);
}

static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

// ---------------------------------------------------------------- motion
// Factory calibration (feature report 0x05), applied the same way as Linux hid-playstation.
// Output units match what libScePad gives for a DS4: accel in g, gyro in rad/s, plus an
// orientation quaternion (x, y, z, w) integrated from the gyro with gravity tilt correction.
struct calib { float bias, scale; };
static struct calib g_gyro_cal[3], g_acc_cal[3];
static float g_acc[3], g_gyr[3], g_q[4] = {0, 0, 0, 1};
static uint32_t g_last_ts;
static int g_have_ts;
static volatile LONG g_reset_q;

static void calib_defaults(void) {
    for (int i = 0; i < 3; i++) {
        g_gyro_cal[i].bias = 0; g_gyro_cal[i].scale = (3.14159265f / 180.0f) / 16.384f; // +-2000 dps
        g_acc_cal[i].bias = 0;  g_acc_cal[i].scale = 1.0f / 8192.0f;
    }
}

static void calib_read(HANDLE h) {
    calib_defaults();
    uint8_t b[64] = {0x05};
    if (!HidD_GetFeature(h, b, 41)) { LOG("calibration read failed, using defaults"); return; }
    int pb = rd16(b + 1), yb = rd16(b + 3), rb = rd16(b + 5);
    int pp = rd16(b + 7), pm = rd16(b + 9), yp = rd16(b + 11), ym = rd16(b + 13), rp = rd16(b + 15), rm = rd16(b + 17);
    int speed2x = rd16(b + 19) + rd16(b + 21);
    int bias[3] = {pb, yb, rb}, plus[3] = {pp, yp, rp}, minus[3] = {pm, ym, rm};
    for (int i = 0; i < 3; i++) {
        int den = abs(plus[i] - bias[i]) + abs(minus[i] - bias[i]);
        // hid-playstation uses bias 0 for the DualSense gyro; bias only enters the range
        if (den > 0 && speed2x > 0) g_gyro_cal[i].scale = (float)speed2x / den * (3.14159265f / 180.0f);
    }
    for (int i = 0; i < 3; i++) {
        int ap = rd16(b + 23 + i * 4), am = rd16(b + 25 + i * 4);
        int range2g = ap - am;
        if (range2g > 0) {
            g_acc_cal[i].bias = ap - range2g / 2.0f;
            g_acc_cal[i].scale = 2.0f / range2g;
        }
    }
    LOG("calibration: gyro scale %.6g %.6g %.6g, acc bias %.0f %.0f %.0f scale %.6g",
        g_gyro_cal[0].scale, g_gyro_cal[1].scale, g_gyro_cal[2].scale,
        g_acc_cal[0].bias, g_acc_cal[1].bias, g_acc_cal[2].bias, g_acc_cal[0].scale);
}

static void quat_normalize(float *q) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-6f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int i = 0; i < 4; i++) q[i] /= n;
}

// Called by the reader thread for every input report (d = report payload after id/header).
static void motion_update(const uint8_t *d) {
    float acc[3], gyr[3];
    for (int i = 0; i < 3; i++) {
        gyr[i] = (rd16(d + 15 + 2 * i) - g_gyro_cal[i].bias) * g_gyro_cal[i].scale;
        acc[i] = (rd16(d + 21 + 2 * i) - g_acc_cal[i].bias) * g_acc_cal[i].scale;
        if (fabsf(gyr[i]) <= 0.01745329f) gyr[i] = 0; // 1 deg/s deadband, as libScePad does
    }
    uint32_t ts = (uint32_t)(d[27] | (d[28] << 8) | (d[29] << 16) | ((uint32_t)d[30] << 24));
    float dt = g_have_ts ? (uint32_t)(ts - g_last_ts) / 3.0f * 1e-6f : 0; // 0.33 us ticks
    g_last_ts = ts; g_have_ts = 1;
    if (dt <= 0 || dt > 0.1f) dt = 0;

    EnterCriticalSection(&g_cs);
    float *q = g_q;
    if (InterlockedExchange(&g_reset_q, 0)) { q[0] = q[1] = q[2] = 0; q[3] = 1; }
    if (dt > 0) {
        // Mahony-style: nudge the gyro toward the measured gravity direction
        float w[3] = {gyr[0], gyr[1], gyr[2]};
        float an = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        if (an > 0.8f && an < 1.2f) {
            float ax = acc[0] / an, ay = acc[1] / an, az = acc[2] / an;
            float x = q[0], y = q[1], z = q[2], qw = q[3];
            // gravity "up" (+Y at rest) rotated into the sensor frame
            float vx = 2 * (x * y + qw * z), vy = qw * qw - x * x + y * y - z * z, vz = 2 * (y * z - qw * x);
            const float K = 0.5f;
            w[0] += K * (ay * vz - az * vy);
            w[1] += K * (az * vx - ax * vz);
            w[2] += K * (ax * vy - ay * vx);
        }
        float x = q[0], y = q[1], z = q[2], qw = q[3], h = 0.5f * dt;
        q[0] += h * (qw * w[0] + y * w[2] - z * w[1]);
        q[1] += h * (qw * w[1] + z * w[0] - x * w[2]);
        q[2] += h * (qw * w[2] + x * w[1] - y * w[0]);
        q[3] += h * (-x * w[0] - y * w[1] - z * w[2]);
        quat_normalize(q);
    }
    memcpy(g_acc, acc, sizeof acc);
    memcpy(g_gyr, gyr, sizeof gyr);
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI ds_reader(LPVOID unused) {
    (void)unused;
    uint8_t buf[128];
    LOG("reader thread start");
    while (g_alive) {
        DWORD got = 0;
        if (!ReadFile(g_dev, buf, g_in_len ? g_in_len : 64, &got, NULL) || got == 0) {
            LOG("ReadFile failed err=%lu", GetLastError());
            break;
        }
        if (buf[0] == 0x01 || buf[0] == 0x31) {
            EnterCriticalSection(&g_cs);
            memcpy(g_report, buf, got < sizeof g_report ? got : sizeof g_report);
            g_have_report = 1;
            LeaveCriticalSection(&g_cs);
            motion_update(buf + (buf[0] == 0x31 ? 2 : 1));
        }
    }
    EnterCriticalSection(&g_cs);
    g_have_report = 0;
    LeaveCriticalSection(&g_cs);
    g_alive = 0;
    LOG("reader thread end");
    return 0;
}

static int ds_find_and_open(void) {
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    SP_DEVICE_INTERFACE_DATA ifd;
    ifd.cbSize = sizeof ifd;
    int ok = 0;
    for (DWORD i = 0; !ok && SetupDiEnumDeviceInterfaces(set, NULL, &hid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)malloc(need);
        if (!det) continue;
        det->cbSize = sizeof *det;
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, NULL, NULL)) {
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES at;
                at.Size = sizeof at;
                if (HidD_GetAttributes(h, &at) && at.VendorID == VID_SONY &&
                    (at.ProductID == PID_DUALSENSE || at.ProductID == PID_DUALSENSE_EDGE)) {
                    PHIDP_PREPARSED_DATA pp;
                    HIDP_CAPS caps;
                    memset(&caps, 0, sizeof caps);
                    if (HidD_GetPreparsedData(h, &pp)) {
                        HidP_GetCaps(pp, &caps);
                        HidD_FreePreparsedData(pp);
                    }
                    g_in_len = caps.InputReportByteLength;
                    g_out_len = caps.OutputReportByteLength;
                    g_is_bt = (g_in_len > 64);
                    LOG("DualSense found pid=%04X in=%u out=%u bt=%d", at.ProductID, g_in_len, g_out_len, g_is_bt);
                    calib_read(h); // reading feature 0x05 also switches a BT pad to full reports
                    g_dev = h;
                    lstrcpynW(g_path, det->DevicePath, 512);
                    ok = 1;
                } else {
                    CloseHandle(h);
                }
            }
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(set);
    return ok;
}

static int ds_open(void) {
    if (!g_cs_init) { InitializeCriticalSection(&g_cs); g_cs_init = 1; }
    if (g_alive) return 1;
    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
    if (g_thread) { CloseHandle(g_thread); g_thread = NULL; }
    if (!ds_find_and_open()) return 0;
    g_wdev = CreateFileW(g_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (!g_wevent) g_wevent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_alive = 1;
    g_out_sent = 0xFFFFFFFF;
    g_thread = CreateThread(NULL, 0, ds_reader, NULL, 0, NULL);
    g_wthread = CreateThread(NULL, 0, ds_writer, NULL, 0, NULL);
    ds_send_output();
    return 1;
}

static void ds_close(void) {
    g_alive = 0;
    if (g_dev != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_dev, NULL);
        CloseHandle(g_dev);
        g_dev = INVALID_HANDLE_VALUE;
    }
    if (g_thread) { WaitForSingleObject(g_thread, 500); CloseHandle(g_thread); g_thread = NULL; }
    if (g_wevent) SetEvent(g_wevent);
    if (g_wthread) { WaitForSingleObject(g_wthread, 500); CloseHandle(g_wthread); g_wthread = NULL; }
    if (g_wdev != INVALID_HANDLE_VALUE) { CloseHandle(g_wdev); g_wdev = INVALID_HANDLE_VALUE; }
}

// ScePad button bits
#define B_L3 0x2
#define B_R3 0x4
#define B_OPTIONS 0x8
#define B_UP 0x10
#define B_RIGHT 0x20
#define B_DOWN 0x40
#define B_LEFT 0x80
#define B_L2 0x100
#define B_R2 0x200
#define B_L1 0x400
#define B_R1 0x800
#define B_TRIANGLE 0x1000
#define B_CIRCLE 0x2000
#define B_CROSS 0x4000
#define B_SQUARE 0x8000
#define B_TOUCHPAD 0x100000

static void put_f(uint8_t *p, float v) { memcpy(p, &v, 4); }

static int ds_read_state(uint8_t *out /* ScePadData, 0x78 bytes */) {
    memset(out, 0, 0x78);
    uint8_t r[128];
    int have;
    float acc[3], gyr[3], q[4];
    EnterCriticalSection(&g_cs);
    memcpy(r, g_report, sizeof r);
    have = g_have_report;
    memcpy(acc, g_acc, sizeof acc);
    memcpy(gyr, g_gyr, sizeof gyr);
    memcpy(q, g_q, sizeof q);
    LeaveCriticalSection(&g_cs);

    // defaults: neutral sticks, identity orientation, 1g down
    *(uint32_t *)(out + 4) = 0x80808080;
    put_f(out + 0x18, 1.0f);
    put_f(out + 0x20, 1.0f);
    out[0x68] = 1;
    *(uint64_t *)(out + 0x50) = (uint64_t)GetTickCount64() * 1000;
    if (!have || !g_alive) { out[0x4c] = 0; return 0; }
    out[0x4c] = 1;

    const uint8_t *d = r + (r[0] == 0x31 ? 2 : 1);
    out[4] = d[0]; out[5] = d[1]; out[6] = d[2]; out[7] = d[3];
    out[8] = d[4]; out[9] = d[5];
    uint32_t b = 0;
    uint8_t b0 = d[7], b1 = d[8], b2 = d[9];
    switch (b0 & 0x0F) {
        case 0: b |= B_UP; break;
        case 1: b |= B_UP | B_RIGHT; break;
        case 2: b |= B_RIGHT; break;
        case 3: b |= B_RIGHT | B_DOWN; break;
        case 4: b |= B_DOWN; break;
        case 5: b |= B_DOWN | B_LEFT; break;
        case 6: b |= B_LEFT; break;
        case 7: b |= B_LEFT | B_UP; break;
    }
    if (b0 & 0x10) b |= B_SQUARE;
    if (b0 & 0x20) b |= B_CROSS;
    if (b0 & 0x40) b |= B_CIRCLE;
    if (b0 & 0x80) b |= B_TRIANGLE;
    if (b1 & 0x01) b |= B_L1;
    if (b1 & 0x02) b |= B_R1;
    if (b1 & 0x04) b |= B_L2;
    if (b1 & 0x08) b |= B_R2;
    if (b1 & 0x20) b |= B_OPTIONS;
    if (b1 & 0x40) b |= B_L3;
    if (b1 & 0x80) b |= B_R3;
    if (b2 & 0x02) b |= B_TOUCHPAD;
    *(uint32_t *)out = b;

#ifdef DS_PROXY_DEBUG
    { // diagnostics: log button / stick-direction changes
        static uint32_t last = 0xFFFFFFFF;
        int ldir = (d[0] < 64 ? 1 : d[0] > 192 ? 2 : 0) | (d[1] < 64 ? 4 : d[1] > 192 ? 8 : 0);
        int rdir = (d[2] < 64 ? 1 : d[2] > 192 ? 2 : 0) | (d[3] < 64 ? 4 : d[3] > 192 ? 8 : 0);
        uint32_t now = (b & 0xFFFFF) ^ ((uint32_t)ldir << 24) ^ ((uint32_t)rdir << 28);
        if (now != last) {
            static const char *dn[16] = {"-", "L", "R", "?", "U", "UL", "UR", "?", "D", "DL", "DR", "?", "?", "?", "?", "?"};
            LOG("input btn=%05X lstick=%s rstick=%s (raw L %u,%u R %u,%u)", b, dn[ldir], dn[rdir], d[0], d[1], d[2], d[3]);
            last = now;
        }
    }
#endif

    // motion (computed per report by motion_update)
    for (int i = 0; i < 4; i++) put_f(out + 0x0c + 4 * i, q[i]);
    for (int i = 0; i < 3; i++) put_f(out + 0x1c + 4 * i, acc[i]);
    for (int i = 0; i < 3; i++) put_f(out + 0x28 + 4 * i, gyr[i]);

    // touch: points at 32 and 36
    int n = 0;
    for (int t = 0; t < 2; t++) {
        const uint8_t *p = d + 32 + t * 4;
        if (p[0] & 0x80) continue;
        uint16_t x = (uint16_t)(p[1] | ((p[2] & 0x0F) << 8));
        uint16_t y = (uint16_t)((p[2] >> 4) | (p[3] << 4));
        y = (uint16_t)((uint32_t)y * 942u / 1080u);
        uint8_t *dst = out + 0x3c + n * 8;
        memcpy(dst, &x, 2);
        memcpy(dst + 2, &y, 2);
        dst[4] = p[0] & 0x7F;
        n++;
    }
    out[0x34] = (uint8_t)n;
    return 0;
}


// ---------------------------------------------------------------- DirectInput hiding
// Windows also exposes the DualSense as a generic DirectInput gamepad, which the game
// reads with Xbox prompts. Hide it from the game's DirectInput so only scePad sees it.
typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE, DWORD, const GUID *, void **, void *);
typedef BOOL (CALLBACK *EnumCb_t)(const void *inst, void *ref);
typedef HRESULT (STDMETHODCALLTYPE *EnumDevices_t)(void *self, DWORD type, EnumCb_t cb, void *ref, DWORD flags);
typedef HRESULT (STDMETHODCALLTYPE *CreateDevice_t)(void *self, const GUID *g, void **dev, void *outer);
static DI8Create_t g_di_create;
static EnumDevices_t g_enum_orig;
static CreateDevice_t g_create_orig;
static GUID g_hidden[8];
static int g_nhidden;
struct enum_ctx { EnumCb_t cb; void *ref; };

static int is_dualsense_product(const GUID *prod) {
    return prod->Data1 == MAKELONG(VID_SONY, PID_DUALSENSE) || prod->Data1 == MAKELONG(VID_SONY, PID_DUALSENSE_EDGE);
}
static BOOL CALLBACK enum_filter(const void *inst, void *ref) {
    struct enum_ctx *c = (struct enum_ctx *)ref;
    const GUID *instance = (const GUID *)((const uint8_t *)inst + 4); // DIDEVICEINSTANCE: dwSize, guidInstance, guidProduct
    const GUID *product = (const GUID *)((const uint8_t *)inst + 20);
    if (is_dualsense_product(product)) {
        int known = 0;
        for (int i = 0; i < g_nhidden; i++) if (!memcmp(&g_hidden[i], instance, sizeof(GUID))) known = 1;
        if (!known && g_nhidden < 8) { g_hidden[g_nhidden++] = *instance; LOG("DirectInput: hiding DualSense"); }
        return TRUE; // DIENUM_CONTINUE
    }
    return c->cb(inst, c->ref);
}
static HRESULT STDMETHODCALLTYPE enum_hook(void *self, DWORD type, EnumCb_t cb, void *ref, DWORD flags) {
    struct enum_ctx c = {cb, ref};
    return g_enum_orig(self, type, enum_filter, &c, flags);
}
static HRESULT STDMETHODCALLTYPE create_hook(void *self, const GUID *g, void **dev, void *outer) {
    for (int i = 0; g && i < g_nhidden; i++)
        if (!memcmp(&g_hidden[i], g, sizeof(GUID))) { if (dev) *dev = NULL; return (HRESULT)0x80040154; } // DIERR_DEVICENOTREG
    return g_create_orig(self, g, dev, outer);
}
static HRESULT WINAPI di8create_hook(HINSTANCE h, DWORD ver, const GUID *riid, void **out, void *outer) {
    HRESULT hr = g_di_create(h, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = *(void ***)*out; // IDirectInput8: 3 = CreateDevice, 4 = EnumDevices
        if (vt[4] != (void *)enum_hook) {
            DWORD old;
            if (VirtualProtect(&vt[3], 2 * sizeof(void *), PAGE_READWRITE, &old)) {
                g_create_orig = (CreateDevice_t)vt[3];
                g_enum_orig = (EnumDevices_t)vt[4];
                vt[3] = (void *)create_hook;
                vt[4] = (void *)enum_hook;
                VirtualProtect(&vt[3], 2 * sizeof(void *), old, &old);
                LOG("DirectInput hooked");
            }
        }
    }
    return hr;
}
// Redirect the game exe's DirectInput8Create import to our wrapper.
static void hook_dinput_iat(void) {
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((char *)(base + imp->Name), "DINPUT8.dll")) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, "DirectInput8Create")) continue;
            DWORD old;
            if (VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                g_di_create = (DI8Create_t)iat->u1.Function;
                iat->u1.Function = (ULONG_PTR)di8create_hook;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
                LOG("DirectInput8Create import hooked");
            }
        }
    }
}

// ---------------------------------------------------------------- exports
#define EXP __declspec(dllexport)

EXP int scePadInit(void) {
    LOG("scePadInit");
    return fwd(F_Init, 0, 0, 0, 0, 0, 0);
}

static int g_primary = -1;      // handle the original lib gave for user 1
static volatile int g_ds_active; // DualSense currently standing in for g_primary
static ULONGLONG g_last_try;

// Called when the DS4 slot is empty: (re)open a DualSense at most every 2 s.
static int ds_available(void) {
    if (g_alive) return 1;
    ULONGLONG now = GetTickCount64();
    if (now - g_last_try < 2000) return 0;
    g_last_try = now;
    return ds_open();
}

EXP int scePadOpen(int userId, int type, int index, void *param) {
    int r = fwd(F_Open, userId, type, index, (INT_PTR)param, 0, 0);
    LOG("scePadOpen(%d,%d,%d) orig -> 0x%08X", userId, type, index, (unsigned)r);
    if (userId == 1 && r >= 0) g_primary = r;
    return r;
}

EXP int scePadClose(int handle) {
    if (handle == g_primary) { ds_close(); g_ds_active = 0; g_primary = -1; }
    return fwd(F_Close, handle, 0, 0, 0, 0, 0);
}

static void ds_fill(int handle, uint8_t *data) {
    if (handle != g_primary || data[0x4c]) { // real DS4 connected, or not slot 1
        if (handle == g_primary && g_ds_active) { g_ds_active = 0; LOG("DS4 present, DualSense yields"); }
        return;
    }
    if (!ds_available()) { g_ds_active = 0; return; }
    if (!g_ds_active) { g_ds_active = 1; LOG("DualSense active on handle 0x%X", handle); }
    ds_read_state(data);
}

EXP int scePadReadState(int handle, void *data) {
    int r = fwd(F_ReadState, handle, (INT_PTR)data, 0, 0, 0, 0);
    if (r >= 0 && data) ds_fill(handle, (uint8_t *)data);
    return r;
}

EXP int scePadRead(int handle, void *data, int count) {
    int r = fwd(F_Read, handle, (INT_PTR)data, count, 0, 0, 0);
    if (r >= 1 && data) ds_fill(handle, (uint8_t *)data); // newest sample is first
    return r;
}

EXP int scePadSetVibration(int handle, const uint8_t *param) {
    if (handle == g_primary && g_ds_active && param) {
        g_rumble_large = param[0];
        g_rumble_small = param[1];
        ds_send_output();
        return 0;
    }
    return fwd(F_SetVibration, handle, (INT_PTR)param, 0, 0, 0, 0);
}

EXP int scePadSetLightBar(int handle, const uint8_t *param) {
    if (handle == g_primary && param) { g_lr = param[0]; g_lg = param[1]; g_lb = param[2]; g_lb_set = 1; }
    if (handle == g_primary && g_ds_active) { ds_send_output(); return 0; }
    return fwd(F_SetLightBar, handle, (INT_PTR)param, 0, 0, 0, 0);
}

EXP int scePadResetLightBar(int handle) {
    if (handle == g_primary && g_ds_active) { g_lb_set = 0; ds_send_output(); return 0; }
    return fwd(F_ResetLightBar, handle, 0, 0, 0, 0, 0);
}

EXP int scePadResetOrientation(int handle) {
    if (handle == g_primary) InterlockedExchange(&g_reset_q, 1);
    return fwd(F_ResetOrientation, handle, 0, 0, 0, 0, 0);
}

EXP int scePadGetControllerInformation(int handle, uint8_t *info) {
    if (handle == g_primary && g_ds_active) {
        if (!info) return 0x80920001;
        memset(info, 0, 0x1c);
        put_f(info, 44.86f);
        *(uint16_t *)(info + 4) = 1920;
        *(uint16_t *)(info + 6) = 942;
        info[8] = 13; info[9] = 13;
        info[10] = 0; // connection type: local
        info[11] = 1; // connected count
        info[12] = 1; // connected
        return 0;
    }
    return fwd(F_GetControllerInformation, handle, (INT_PTR)info, 0, 0, 0, 0);
}

#define PASS_OK(name, idx)                                                         \
    EXP int name(int handle, INT_PTR a, INT_PTR b) {                              \
\
        return fwd(idx, handle, a, b, 0, 0, 0);                                    \
    }
PASS_OK(scePadGetJackState, F_GetJackState)
PASS_OK(scePadGetParticularMode, F_GetParticularMode)
PASS_OK(scePadIsSupportedAudioFunction, F_IsSupportedAudioFunction)
PASS_OK(scePadSetAngularVelocityDeadbandState, F_SetAngVelDeadband)
PASS_OK(scePadSetAudioOutPath, F_SetAudioOutPath)
PASS_OK(scePadSetMotionSensorState, F_SetMotionSensorState)
PASS_OK(scePadSetParticularMode, F_SetParticularMode)
PASS_OK(scePadSetTiltCorrectionState, F_SetTiltCorrectionState)
PASS_OK(scePadSetVolumeGain, F_SetVolumeGain)

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)h; (void)res;
    if (reason == DLL_PROCESS_ATTACH) { LOG("---- proxy loaded ----"); hook_dinput_iat(); }
    if (reason == DLL_PROCESS_DETACH) ds_close();
    return TRUE;
}

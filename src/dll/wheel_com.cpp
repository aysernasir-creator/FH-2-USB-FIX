// SPDX-License-Identifier: MIT
// Fake Windows.Xbox.Input wheel controller. All objects are plain structs whose first
// member is a vtable pointer; every method uses the Microsoft x64 ABI (the mingw default)
// and returns S_OK except where noted (the game throws on any failure HRESULT).
#include "wheel_com.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>
#include <vector>
#include "../common/car_db.h"
#include "log.h"
#include "steer_lock.h"
#include "wheel_device.h"
#include "wheel_ffb.h"

namespace fh2 {

namespace {

typedef const void* const* Vtbl;
#define P(fn) reinterpret_cast<const void*>(&(fn))

constexpr GUID mkGuid(unsigned long a, unsigned short b, unsigned short c, unsigned char d0, unsigned char d1,
                      unsigned char d2, unsigned char d3, unsigned char d4, unsigned char d5, unsigned char d6,
                      unsigned char d7) {
    return GUID{a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}};
}
constexpr GUID kIidUnknown   = mkGuid(0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
constexpr GUID kIidInspect   = mkGuid(0xAF86E2E0, 0xB12D, 0x4C6A, 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90);
constexpr GUID kIidAgile     = mkGuid(0x94EA2B94, 0xE9CC, 0x49E0, 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90);
constexpr GUID kIidController= mkGuid(0x9776ff56, 0x9bfd, 0x4581, 0xad, 0x45, 0xb6, 0x45, 0xbb, 0xa5, 0x26, 0xd6);
constexpr GUID kIidWheel     = mkGuid(0x646979cf, 0x6b71, 0x4e96, 0x8d, 0xf9, 0x59, 0xe3, 0x98, 0xd7, 0x42, 0x0c);
constexpr GUID kIidReading   = mkGuid(0x15198dca, 0xa85c, 0x455d, 0xa2, 0xab, 0xd1, 0xcb, 0x90, 0x7f, 0xc6, 0xd0);
constexpr GUID kIidStaticCfg = mkGuid(0xd45f6e0b, 0xffb6, 0x425f, 0xb1, 0xd9, 0x33, 0x6d, 0xd5, 0xf8, 0xae, 0xbb);

constexpr HRESULT kENoInterface = (HRESULT)0x80004002L;
constexpr HRESULT kEPointer     = (HRESULT)0x80004003L;
constexpr HRESULT kEOutOfMemory = (HRESULT)0x8007000EL;

bool same(const GUID* a, const GUID& b) { return a && std::memcmp(a, &b, sizeof(GUID)) == 0; }
bool isBaseIid(const GUID* r) { return same(r, kIidUnknown) || same(r, kIidInspect) || same(r, kIidAgile); }

// ---- state ------------------------------------------------------------------------------
struct Cs {
    CRITICAL_SECTION c;
    Cs() { InitializeCriticalSection(&c); }
};
Cs g_cs;
struct Lock {
    Lock() { EnterCriticalSection(&g_cs.c); }
    ~Lock() { LeaveCriticalSection(&g_cs.c); }
};

WheelConfig            g_wc;
bool                   g_diag = false;
uintptr_t              g_base = 0;
std::atomic<uint32_t>  g_rotation{900};
std::atomic<float>     g_gain{1.0f};
// steering lock (see steer_lock.h)
bool                   g_lockPerCar = false, g_lockScale = false;
int                    g_lockMax = 0;
std::vector<std::pair<unsigned, unsigned>> g_lockOverrides;
std::atomic<uint32_t>  g_carLock{0};          // current car's lock to lock in degrees, 0 = unknown
std::atomic<int64_t>   g_lockCar{-1};

LockInputs lockInputs(uint32_t gameRotation) {
    LockInputs in;
    in.wheelRange = (float)g_wc.wheelRange;
    in.maxRotation = (float)g_lockMax;
    in.scale = g_lockScale;
    in.perCar = g_lockPerCar;
    in.carLock = (float)g_carLock.load(std::memory_order_relaxed);
    in.gameRotation = (float)gameRotation;
    in.maxCarLock = (float)kMaxCarLockDeg;
    return in;
}
std::atomic<int64_t>   g_token{1};
std::atomic<uint64_t>  g_counter{1};
void*                  g_user = nullptr;      // owned reference

std::atomic<unsigned char> g_seenW[64], g_seenR[40], g_seenC[24], g_seenCtl[16];
bool first(std::atomic<unsigned char>* arr, int i) { return arr[i].exchange(1) == 0; }
void noteW(int n) { if (g_diag && first(g_seenW, n)) LOG("wheel: IWheel slot %d", n); }
void noteR(int n) { if (g_diag && first(g_seenR, n)) LOG("wheel: reading slot %d", n); }
void noteC(int n) { if (g_diag && first(g_seenC, n)) LOG("wheel: config slot %d", n); }

void logUnknownIid(const GUID* g) {
    static GUID seen[32];
    static int count = 0;
    Lock l;
    for (int i = 0; i < count; ++i) if (std::memcmp(&seen[i], g, sizeof(GUID)) == 0) return;
    if (count < 32) seen[count++] = *g;
    LOG("wheel: QI for unknown IID {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2,
        g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
        g->Data4[7]);
}

// HSTRING access must go through the game's own (Azimuth-bound) IAT slot.
typedef const wchar_t* (*RawBufFn)(void* hs, UINT32* len);
size_t hstrCopy(void* hs, wchar_t* dst, size_t cap) {
    dst[0] = 0;
    if (!hs || !g_base || cap < 2) return 0;
    RawBufFn fn = *reinterpret_cast<RawBufFn*>(g_base + 0x1dd0ed8);
    if (!fn) return 0;
    UINT32 len = 0;
    const wchar_t* s = fn(hs, &len);
    if (!s) return 0;
    size_t n = len < cap - 1 ? len : cap - 1;
    std::memcpy(dst, s, n * sizeof(wchar_t));
    dst[n] = 0;
    return n;
}

// ---- variables ----------------------------------------------------------------------------
struct Var { wchar_t name[48]; float value, mn, mx; bool touched; };
Var g_vars[32];
int g_varCount = 0;

Var* findOrAdd(const wchar_t* name) {   // caller holds the lock
    for (int i = 0; i < g_varCount; ++i) if (wcscmp(g_vars[i].name, name) == 0) return &g_vars[i];
    if (g_varCount >= 32) return nullptr;
    Var* v = &g_vars[g_varCount++];
    wcsncpy(v->name, name, 47); v->name[47] = 0;
    v->value = v->mn = v->mx = 0; v->touched = false;
    return v;
}
// Captured force feedback variables (protected by the same lock as g_vars).
struct FfbCapture {
    float torque = 0, menuSpring = 0, raceSpring = 0, raceSpringMax = 0, dampScale = 0, dampMax = 0,
          leftAmp = 0, leftWl = 0, rightAmp = 0, rightWl = 0;
    uint64_t lastHeartbeatQpc = 0, lastWriteQpc = 0;
};
FfbCapture g_ffb;
GameFfbInput g_gameFfb;   // qpc == 0 until the first UpdateFFB call (protected by the same lock)
std::atomic<bool> g_ffbLogged{false};

uint64_t qpcNow() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart; }
uint64_t qpcFreq() {
    static uint64_t f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return (uint64_t)x.QuadPart; }();
    return f;
}

void captureFfb(const wchar_t* n, float value) {   // caller holds the lock
    uint64_t now = qpcNow();
    g_ffb.lastWriteQpc = now;
    if (wcscmp(n, L"PauseHeartbeat") == 0) {
        if (value != 0) {
            g_ffb.lastHeartbeatQpc = now;
            if (!g_ffbLogged.exchange(true)) LOG("wheel ffb: game force feedback active");
        }
    }
    else if (wcscmp(n, L"Torque") == 0) g_ffb.torque = value;
    else if (wcscmp(n, L"InMenuSpringScale") == 0) g_ffb.menuSpring = value;
    else if (wcscmp(n, L"InRaceSpringScale") == 0) g_ffb.raceSpring = value;
    else if (wcscmp(n, L"InRaceSpringMaxForce") == 0) g_ffb.raceSpringMax = value;
    else if (wcscmp(n, L"InRaceDampingScale") == 0) g_ffb.dampScale = value;
    else if (wcscmp(n, L"InRaceDampingMaxForce") == 0) g_ffb.dampMax = value;
    else if (wcscmp(n, L"LeftAmplitude") == 0) g_ffb.leftAmp = value;
    else if (wcscmp(n, L"LeftWavelength") == 0) g_ffb.leftWl = value;
    else if (wcscmp(n, L"RightAmplitude") == 0) g_ffb.rightAmp = value;
    else if (wcscmp(n, L"RightWavelength") == 0) g_ffb.rightWl = value;
}

void setVar(void* hs, float value) {
    wchar_t n[48];
    hstrCopy(hs, n, 48);
    Lock l;
    if (Var* v = findOrAdd(n)) {
        v->value = value;
        if (!v->touched) { v->mn = v->mx = value; v->touched = true; }
        else { if (value < v->mn) v->mn = value; if (value > v->mx) v->mx = value; }
    }
    captureFfb(n, value);
}

// ---- IInspectable stubs -----------------------------------------------------------------------
HRESULT insp_GetIids(void*, ULONG* count, void** iids) {
    if (count) *count = 0;
    if (iids) *iids = nullptr;
    return S_OK;
}
HRESULT insp_GetClassName(void*, void** out) { if (out) *out = nullptr; return S_OK; }
HRESULT insp_GetTrust(void*, int* out) { if (out) *out = 0; return S_OK; }

// ---- WheelReading ---------------------------------------------------------------------------------
struct Obj { Vtbl vtbl; LONG refs; };

HRESULT objQI(Obj* o, const GUID* riid, void** ppv, const GUID& own) {
    if (!ppv) return kEPointer;
    if (isBaseIid(riid) || same(riid, own)) {
        InterlockedIncrement(&o->refs);
        *ppv = o;
        return S_OK;
    }
    *ppv = nullptr;
    return kENoInterface;
}

struct Reading {
    Obj      o;
    WheelInput in;
    float    steer;
    uint32_t rot;
    uint8_t  gear;
    uint64_t ts;
};

HRESULT rd_QI(void* self, const GUID* riid, void** ppv) { return objQI((Obj*)self, riid, ppv, kIidReading); }
ULONG   rd_AddRef(void* self) { return (ULONG)InterlockedIncrement(&((Obj*)self)->refs); }
ULONG   rd_Release(void* self) {
    LONG n = InterlockedDecrement(&((Obj*)self)->refs);
    if (n == 0) delete reinterpret_cast<Reading*>(self);
    return (ULONG)n;
}

uint8_t shifterGears() {
    uint8_t n = 0;
    for (int g : g_wc.gearButtons) if (g >= 0) ++n;
    return n;
}

template <int N> HRESULT rdByte(void* self, uint8_t* out) {
    noteR(N);
    if (!out) return S_OK;
    const Reading* r = reinterpret_cast<Reading*>(self);
    uint8_t v = 0;
    switch (N) {
        case 6: v = r->in.a; break;   case 7: v = r->in.b; break;   case 8: v = r->in.x; break;
        case 9: v = r->in.y; break;   case 10: v = r->in.view; break; case 11: v = r->in.menu; break;
        case 12: v = r->in.up; break; case 13: v = r->in.down; break; case 14: v = r->in.left; break;
        case 15: v = r->in.right; break;
        case 23: v = 1; break; case 24: v = 1; break; case 25: v = 1; break;
        case 26: v = g_wc.clutchAxis != WheelAxis::None; break;
        case 27: v = 0; break;
        case 28: v = r->in.lb; break; case 29: v = r->in.rb; break;
        case 31: v = g_wc.hShifter ? shifterGears() : 0; break;   // gear count (game caps.RX)
        case 32: v = r->gear; break;
        default: break;
    }
    *out = v;
    return S_OK;
}
template <int N> HRESULT rdFloat(void* self, float* out) {
    noteR(N);
    if (!out) return S_OK;
    const Reading* r = reinterpret_cast<Reading*>(self);
    float v = 0;
    switch (N) {
        case 16: v = r->steer; break;
        case 18: v = r->in.throttle; break;
        case 19: v = r->in.brake; break;
        case 20: v = r->in.clutch; break;
        default: break;          // 21 handbrake, 22: 0
    }
    *out = v;
    return S_OK;
}
template <int N> HRESULT rdU32(void* self, uint32_t* out) {
    noteR(N);
    // 17 rotation; 30 shifter type (nonzero = shifter present, game then asks slot 31 for the gear count)
    if (out) *out = (N == 17) ? reinterpret_cast<Reading*>(self)->rot : (N == 30 && g_wc.hShifter) ? 1u : 0u;
    return S_OK;
}
HRESULT rd33(void* self, uint64_t* out) {
    noteR(33);
    if (out) *out = reinterpret_cast<Reading*>(self)->ts;
    return S_OK;
}

const void* const g_readingVtbl[34] = {
    P(rd_QI), P(rd_AddRef), P(rd_Release), P(insp_GetIids), P(insp_GetClassName), P(insp_GetTrust),
    P(rdByte<6>), P(rdByte<7>), P(rdByte<8>), P(rdByte<9>), P(rdByte<10>), P(rdByte<11>), P(rdByte<12>),
    P(rdByte<13>), P(rdByte<14>), P(rdByte<15>),
    P(rdFloat<16>), P(rdU32<17>), P(rdFloat<18>), P(rdFloat<19>), P(rdFloat<20>), P(rdFloat<21>), P(rdFloat<22>),
    P(rdByte<23>), P(rdByte<24>), P(rdByte<25>), P(rdByte<26>), P(rdByte<27>), P(rdByte<28>), P(rdByte<29>),
    P(rdU32<30>), P(rdByte<31>), P(rdByte<32>), P(rd33),
};

Reading* newReading() {
    Reading* r = new (std::nothrow) Reading();
    if (!r) return nullptr;
    r->o.vtbl = g_readingVtbl;
    r->o.refs = 1;
    WheelInput in;
    bool ok = WheelDevice_Read(in);
    if (!ok) in = WheelInput();      // neutral
    r->in = in;
    r->rot = g_rotation.load();
    r->steer = GameSteer(lockInputs(r->rot), in.steering);
    r->gear = !g_wc.hShifter ? 0 : in.gear < 0 ? 0xFF : (uint8_t)in.gear;
    r->ts = ok ? in.timestamp : g_counter.fetch_add(1);
    return r;
}

// ---- StaticConfig ------------------------------------------------------------------------------------
struct StaticCfg { Obj o; };

HRESULT sc_QI(void* self, const GUID* riid, void** ppv) { return objQI((Obj*)self, riid, ppv, kIidStaticCfg); }
ULONG   sc_AddRef(void* self) { return (ULONG)InterlockedIncrement(&((Obj*)self)->refs); }
ULONG   sc_Release(void* self) {
    LONG n = InterlockedDecrement(&((Obj*)self)->refs);
    if (n == 0) delete reinterpret_cast<StaticCfg*>(self);
    return (ULONG)n;
}
template <int N> HRESULT scByte(void*, uint8_t* out) {
    noteC(N);
    if (!out) return S_OK;
    uint8_t v = 0;
    switch (N) {
        case 6: v = 16; break; case 7: v = 10; break; case 8: v = 10; break; case 9: v = 10; break;
        case 10: v = 0; break;
        case 13: v = 0; break;
        case 14: v = (uint8_t)g_wc.configFlag14; break;
        case 15: v = g_wc.clutchAxis != WheelAxis::None; break;
        case 16: case 17: case 19: v = 0; break;
        case 20: v = g_wc.hShifter; break;
        case 21: v = g_wc.hShifter ? 7 : 0; break;
        default: break;
    }
    *out = v;
    return S_OK;
}
template <int N> HRESULT scU32(void*, uint32_t* out) {
    noteC(N);
    if (out) *out = N == 11 ? 40u : N == 12 ? (uint32_t)g_wc.wheelRange : 0u;
    return S_OK;
}
const void* const g_cfgVtbl[22] = {
    P(sc_QI), P(sc_AddRef), P(sc_Release), P(insp_GetIids), P(insp_GetClassName), P(insp_GetTrust),
    P(scByte<6>), P(scByte<7>), P(scByte<8>), P(scByte<9>), P(scByte<10>), P(scU32<11>), P(scU32<12>),
    P(scByte<13>), P(scByte<14>), P(scByte<15>), P(scByte<16>), P(scByte<17>), P(scU32<18>), P(scByte<19>),
    P(scByte<20>), P(scByte<21>),
};

// ---- FakeController -----------------------------------------------------------------------------------
struct FakeController { Vtbl ctrl; Vtbl wheel; };
extern FakeController g_fake;

HRESULT ctl_QI(void*, const GUID* riid, void** ppv) {
    if (!ppv) return kEPointer;
    if (!riid) { *ppv = nullptr; return kEPointer; }
    if (isBaseIid(riid) || same(riid, kIidController)) { *ppv = &g_fake; return S_OK; }
    if (same(riid, kIidWheel)) { *ppv = reinterpret_cast<char*>(&g_fake) + sizeof(void*); return S_OK; }
    *ppv = nullptr;
    logUnknownIid(riid);
    return kENoInterface;
}
ULONG ctl_AddRef(void*) { return 2; }
ULONG ctl_Release(void*) { return 1; }
HRESULT whl_QI(void*, const GUID* riid, void** ppv) { return ctl_QI(nullptr, riid, ppv); }

HRESULT ctl_GetId(void*, uint64_t* out) { if (out) *out = 0x4648325748454C31ull; return S_OK; }
HRESULT ctl_GetType(void*, void** out) { if (out) *out = nullptr; return S_OK; }
HRESULT ctl_GetUser(void*, void** out) {
    if (!out) return S_OK;
    Lock l;
    *out = g_user;
    if (g_user) reinterpret_cast<ULONG (*)(void*)>((*reinterpret_cast<void***>(g_user))[1])(g_user);
    return S_OK;
}
template <int N> HRESULT ctlStub(void*) {
    if (first(g_seenCtl, N)) LOG("wheel: IController slot %d called", N);
    return S_OK;
}
const void* const g_ctrlVtbl[16] = {
    P(ctl_QI), P(ctl_AddRef), P(ctl_Release), P(insp_GetIids), P(insp_GetClassName), P(insp_GetTrust),
    P(ctl_GetId), P(ctl_GetType), P(ctl_GetUser),
    P(ctlStub<9>), P(ctlStub<10>), P(ctlStub<11>), P(ctlStub<12>), P(ctlStub<13>), P(ctlStub<14>), P(ctlStub<15>),
};

// ---- IWheel (this = IWheel pointer) ----------------------------------------------------------------------
template <int N> HRESULT wU32(void*, uint32_t* out) {
    noteW(N);
    if (!out) return S_OK;
    switch (N) {
        case 6: *out = 1; break;
        case 7: *out = 0; break;
        case 8: *out = 3; break;
        case 10: *out = ((uint32_t)g_wc.vendorId << 16) | (uint32_t)g_wc.productId; break;
        case 11: *out = 1; break;
        default: *out = 0; break;
    }
    return S_OK;
}
HRESULT w9(void*, uint8_t* out) { noteW(9); if (out) *out = 0; return S_OK; }
HRESULT w12(void*, void** out) {
    noteW(12);
    if (!out) return S_OK;
    *out = newReading();
    return *out ? S_OK : kEOutOfMemory;
}
HRESULT w13(void*, void** out) {
    noteW(13);
    if (!out) return S_OK;
    StaticCfg* c = new (std::nothrow) StaticCfg();
    if (c) { c->o.vtbl = g_cfgVtbl; c->o.refs = 1; }
    *out = c;
    return c ? S_OK : kEOutOfMemory;
}
HRESULT w14(void*, uint32_t degrees) {
    noteW(14);
    uint32_t d = degrees < 40 ? 40 : degrees > 65535 ? 65535 : degrees;
    if (g_rotation.exchange(d) != d) LOG("wheel: game rotation %u deg", d);
    return S_OK;
}
HRESULT w15(void*, float gain) { noteW(15); g_gain.store(gain); return S_OK; }
template <int N> HRESULT wAddEvent(void*, void*, int64_t* token) {
    noteW(N);
    if (token) *token = g_token.fetch_add(1);
    return S_OK;
}
template <int N> HRESULT wRemoveEvent(void*, int64_t) { noteW(N); return S_OK; }
HRESULT w26(void*, uint8_t slot, void* program, uint32_t flags, uint32_t* outLen) {
    noteW(26);
    if (outLen) *outLen = 0;
    if (g_diag) {
        wchar_t w[121];
        size_t n = hstrCopy(program, w, 121);
        char a[121];
        for (size_t i = 0; i <= n; ++i) a[i] = w[i] < 128 ? (char)w[i] : '?';
        LOG("wheel: equation %u: %s", (unsigned)slot, a);
    }
    return S_OK;
}
template <int N> HRESULT wByte(void*, uint8_t) { noteW(N); return S_OK; }
template <int N> HRESULT wNoop(void*) { noteW(N); return S_OK; }
HRESULT w36(void*, void* name) {
    noteW(36);
    wchar_t n[48];
    hstrCopy(name, n, 48);
    Lock l;
    findOrAdd(n);
    return S_OK;
}
HRESULT w37(void*, void* name, uint32_t v) { noteW(37); setVar(name, (float)v); return S_OK; }
HRESULT w38(void*, void* name, float v) { noteW(38); setVar(name, v); return S_OK; }
HRESULT w39(void*, void*, uint32_t* out) { noteW(39); if (out) *out = 0; return S_OK; }
HRESULT w40(void*, uint32_t, void** out) { noteW(40); if (out) *out = nullptr; return S_OK; }
HRESULT w41(void*, uint8_t, uint8_t* out) { noteW(41); if (out) *out = 0; return S_OK; }
HRESULT w48(void*, uint32_t, uint8_t*) { noteW(48); return S_OK; }
HRESULT w49(void*, uint32_t, uint32_t size, uint8_t* buf) {
    noteW(49);
    if (buf && size) std::memset(buf, 0, size);
    return S_OK;
}

const void* const g_wheelVtbl[50] = {
    P(whl_QI), P(ctl_AddRef), P(ctl_Release), P(insp_GetIids), P(insp_GetClassName), P(insp_GetTrust),
    P(wU32<6>), P(wU32<7>), P(wU32<8>), P(w9), P(wU32<10>), P(wU32<11>), P(w12), P(w13), P(w14), P(w15),
    P(wAddEvent<16>), P(wRemoveEvent<17>), P(wAddEvent<18>), P(wRemoveEvent<19>), P(wAddEvent<20>),
    P(wRemoveEvent<21>), P(wAddEvent<22>), P(wRemoveEvent<23>), P(wAddEvent<24>), P(wRemoveEvent<25>),
    P(w26),
    P(wByte<27>), P(wByte<28>), P(wByte<29>), P(wByte<30>), P(wByte<31>), P(wNoop<32>), P(wNoop<33>),
    P(wAddEvent<34>), P(wRemoveEvent<35>),
    P(w36), P(w37), P(w38), P(w39), P(w40), P(w41),
    P(wNoop<42>), P(wNoop<43>), P(wNoop<44>), P(wNoop<45>), P(wNoop<46>), P(wNoop<47>),
    P(w48), P(w49),
};

FakeController g_fake = {g_ctrlVtbl, g_wheelVtbl};

} // namespace

void VirtualWheel_Configure(const Config& cfg) {
    g_wc = cfg.wheel;
    g_diag = cfg.diagnostics;
    g_rotation.store((uint32_t)cfg.wheel.wheelRange);
    g_lockPerCar = cfg.sim.realisticSteeringLock;
    g_lockScale = cfg.sim.lockScale;
    g_lockMax = cfg.sim.maxWheelRotation;
    g_lockOverrides = cfg.sim.steeringLocks;
}

void* VirtualWheel_Controller() { return &g_fake; }
void VirtualWheel_SetGameBase(uintptr_t base) { g_base = base; }
void VirtualWheel_FfbSnapshot(FfbParams& out) {
    Lock l;
    out.torque = g_ffb.torque; out.menuSpringScale = g_ffb.menuSpring;
    out.raceSpringScale = g_ffb.raceSpring; out.raceSpringMax = g_ffb.raceSpringMax;
    out.dampScale = g_ffb.dampScale; out.dampMax = g_ffb.dampMax;
    out.leftAmp = g_ffb.leftAmp; out.leftWl = g_ffb.leftWl;
    out.rightAmp = g_ffb.rightAmp; out.rightWl = g_ffb.rightWl;
    uint64_t now = qpcNow(), f = qpcFreq();
    out.game = g_gameFfb;
    out.haveGame = g_gameFfb.qpc != 0 && (double)(qpcNow() - g_gameFfb.qpc) < 0.5 * (double)qpcFreq();
    out.race  = g_ffb.lastHeartbeatQpc != 0 && (double)(now - g_ffb.lastHeartbeatQpc) < 2.5 * (double)f;
    out.stale = g_ffb.lastWriteQpc == 0 || (double)(now - g_ffb.lastWriteQpc) > 1.0 * (double)f;
    static std::atomic<int> prevRace{-1};
    int r = out.race ? 1 : 0;
    if (prevRace.exchange(r) != r)
        LOG("wheel: game %s", r ? "racing (force feedback heartbeat on)" : "paused or in menus (heartbeat stopped)");
}

void VirtualWheel_SetGameFfbInput(const GameFfbInput& in) {
    Lock l;
    g_gameFfb = in;
    g_gameFfb.qpc = qpcNow();
}

bool VirtualWheel_LastGameFfbInput(GameFfbInput& out) {
    Lock l;
    out = g_gameFfb;
    return g_gameFfb.qpc != 0;
}

float VirtualWheel_GameAngle(float steering) { return GameSteer(lockInputs(g_rotation.load()), steering); }

float VirtualWheel_PhysicalLockDeg() { return PhysicalLock(lockInputs(g_rotation.load())); }
float VirtualWheel_DisplayLockDeg() { return DisplayLock(lockInputs(g_rotation.load())); }

void SteerLock_SetPlayerCar(uint32_t id) {
    if (g_lockCar.load(std::memory_order_relaxed) == (int64_t)id) return;
    const CarInfo* car = FindCar(id);
    uint32_t lock = 0;
    const char* src = "game";
    for (const auto& o : g_lockOverrides)
        if (o.first == id) { lock = o.second; src = "override"; }
    if (!lock && car) {
        lock = car->lockDeg;
        src = "car data";
    }
    g_carLock.store(lock);
    g_lockCar.store((int64_t)id);
    const LockInputs in = lockInputs(g_rotation.load());
    LOG("steering lock: car %u %s real %u deg -> wheel %u deg (display %u, %s)", id, car ? car->name : "unknown car", lock,
        (unsigned)std::lround(PhysicalLock(in)), (unsigned)std::lround(DisplayLock(in)), src);
}

float VirtualWheel_GameRotation() { return (float)g_rotation.load(); }

void VirtualWheel_SetUser(void* user) {
    void* old;
    {
        Lock l;
        old = g_user;
        g_user = user;
    }
    if (old) reinterpret_cast<ULONG (*)(void*)>((*reinterpret_cast<void***>(old))[2])(old);
}

void VirtualWheel_FormatVars(char* buf, size_t cap) {
    if (!cap) return;
    buf[0] = 0;
    size_t used = 0;
    Lock l;
    for (int i = 0; i < g_varCount && used + 1 < cap; ++i) {
        Var& v = g_vars[i];
        char nm[48];
        for (size_t k = 0; k < 48; ++k) nm[k] = v.name[k] < 128 ? (char)v.name[k] : '?';
        float mn = v.touched ? v.mn : v.value, mx = v.touched ? v.mx : v.value;
        int n = snprintf(buf + used, cap - used, "%s%s %.3f [%.3f..%.3f]", i ? " " : "", nm, v.value, mn, mx);
        if (n < 0) break;
        used += (size_t)n < cap - used ? (size_t)n : cap - used - 1;
        v.touched = false;
    }
}

} // namespace fh2

// Test hook for the integration harness (GetProcAddress); not used by the game or the plugin.
extern "C" __declspec(dllexport) bool FH2SI_TestLastGameFfbInput(fh2::GameFfbInput* out) {
    return out && fh2::VirtualWheel_LastGameFfbInput(*out);
}

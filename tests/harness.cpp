// SPDX-License-Identifier: MIT
// Integration test (runs under Windows or Wine):
//   1. maps the real forza_durango_release_final.exe into memory the way a loader would,
//   2. loads FH2SimImmersion.dll, which must find, verify and patch the image,
//   3. sends OpenTrack UDP packets,
//   4. calls the game's own CameraLayer::EvaluateInput on fake layers and checks results.
// usage: harness.exe <path-to-game-exe> <path-to-FH2SimImmersion.dll> [--deep]
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/dll/wheel_ffb.h"
#include "../src/common/car_db.h"

#ifdef _MSC_VER
#define MSABI
#else
#define MSABI __attribute__((ms_abi))
#endif

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { printf("  ok   "); } else { printf("  FAIL "); ++failures; } printf(__VA_ARGS__); printf("\n"); } while (0)

static uint8_t* mapImage(const wchar_t* path, size_t pad = 0) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(f, nullptr);
    std::vector<uint8_t> file(size);
    DWORD got; ReadFile(f, file.data(), size, &got, nullptr); CloseHandle(f);
    auto dos = (IMAGE_DOS_HEADER*)file.data();
    auto nt = (IMAGE_NT_HEADERS64*)(file.data() + dos->e_lfanew);
    // pad > 0 places the image inside a larger allocation, which defeats the plugin's
    // fast path and exercises its deep memory scan.
    uint8_t* alloc = (uint8_t*)VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage + pad, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!alloc) return nullptr;
    uint8_t* base = alloc + pad;
    memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].SizeOfRawData)
            memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData,
                   std::min(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData));
    // relocations
    uint64_t delta = (uint64_t)base - nt->OptionalHeader.ImageBase;
    auto& rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint8_t* r = base + rd.VirtualAddress; uint8_t* rend = r + rd.Size;
    while (r < rend) {
        auto blk = (IMAGE_BASE_RELOCATION*)r;
        if (!blk->SizeOfBlock) break;
        auto ent = (WORD*)(blk + 1);
        int n = (blk->SizeOfBlock - sizeof(*blk)) / 2;
        for (int i = 0; i < n; ++i)
            if ((ent[i] >> 12) == IMAGE_REL_BASED_DIR64)
                *(uint64_t*)(base + blk->VirtualAddress + (ent[i] & 0xFFF)) += delta;
        r += blk->SizeOfBlock;
    }
    return base;
}

// Fake car object: EvaluateInput calls car->vtbl[0x70/8]() twice and needs non-null.
struct FakeCar;
static void* MSABI carSlot14(FakeCar* self) { return self; }
static void* g_carVtbl[32];
struct FakeCar { void** vtbl; uint8_t pad[0x400]; };

typedef void (MSABI *EvalFn)(void* layer, void* car, void* r8, float dt);

struct alignas(16) Layer { uint8_t b[0x200]; };

static Layer makeLayer(int inputType, int outputType, float paramX) {
    Layer L; memset(&L, 0, sizeof(L));
    *(int*)(L.b + 0x48) = inputType;
    *(int*)(L.b + 0x50) = outputType;
    *(float*)(L.b + 0x58) = -25.0f;
    *(float*)(L.b + 0x5C) = 25.0f;
    *(int*)(L.b + 0xAC) = 4;          // FivePoint
    *(float*)(L.b + 0xE0) = paramX;
    return L;
}
static float f(const Layer& L, int off) { return *(const float*)(L.b + off); }
static int   i32(const Layer& L, int off) { return *(const int*)(L.b + off); }

static void sendPose(SOCKET s, double x, double y, double z, double yaw, double pitch, double roll) {
    double p[6] = {x, y, z, yaw, pitch, roll};
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(4242);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    sendto(s, (const char*)p, sizeof(p), 0, (sockaddr*)&a, sizeof(a));
}


// ---------------------------------------------------------------------------------------------
// Emulation of what Azimuth provides for the wheel tests: an activation-factory import that
// returns a statics object whose controller list is empty.
// ---------------------------------------------------------------------------------------------
static const GUID kIidUnknownH = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIidStaticsH = {0x7400624f, 0x144b, 0x47fc, {0x9f, 0x3b, 0x1d, 0xca, 0xfb, 0x48, 0x28, 0x73}};
static const GUID kIidGamepadH = {0x082e402c, 0x07df, 0x45e1, {0xa5, 0xab, 0xa3, 0x12, 0x7a, 0xf1, 0x97, 0xb5}};
static const GUID kIidWheelH   = {0x646979cf, 0x6b71, 0x4e96, {0x8d, 0xf9, 0x59, 0xe3, 0x98, 0xd7, 0x42, 0x0c}};

struct HObj { void** vtbl; };
static HRESULT MSABI h_QI(void* self, const GUID* iid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualGUID(*iid, kIidUnknownH) || IsEqualGUID(*iid, kIidStaticsH)) { *out = self; return S_OK; }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG MSABI h_AddRef(void*) { return 2; }
static ULONG MSABI h_Release(void*) { return 1; }
static HRESULT MSABI h_Stub(void*) { return S_OK; }
// Two real controllers in the fake list: a plain pad (does not answer IWheel) and a wheel that Azimuth
// "provides" (answers IWheel). OverrideAzimuthWheel=1 (default) must hide the second one.
static HRESULT MSABI h_CtlQI(void* self, const GUID* iid, void** out);
static HRESULT MSABI h_GetUserNull(void*, void** out) { if (out) *out = nullptr; return S_OK; }
static void* g_hPadVtbl[16], *g_hWheelVtbl[16];
static HObj g_hPad = {g_hPadVtbl}, g_hWheelCtl = {g_hWheelVtbl};
static HRESULT MSABI h_ViewGetAt(void*, uint32_t i, void** out) {
    if (out) *out = nullptr;
    if (i > 1) return (HRESULT)0x8000000BL;
    if (out) *out = i == 0 ? (void*)&g_hPad : (void*)&g_hWheelCtl;
    return S_OK;
}
static HRESULT MSABI h_ViewSize(void*, uint32_t* n) { *n = 2; return S_OK; }
static HRESULT MSABI h_ViewIndexOf(void*, void* item, uint32_t* i, uint8_t* f) {
    *i = item == (void*)&g_hWheelCtl ? 1 : 0;
    *f = item == (void*)&g_hPad || item == (void*)&g_hWheelCtl;
    return S_OK;
}
static HRESULT MSABI h_ViewGetMany(void*, uint32_t, uint32_t, void**, uint32_t* a) { *a = 0; return S_OK; }
static HRESULT MSABI h_CtlQI(void* self, const GUID* iid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualGUID(*iid, kIidUnknownH) || (self == (void*)&g_hWheelCtl && IsEqualGUID(*iid, kIidWheelH))) { *out = self; return S_OK; }
    *out = nullptr;
    return E_NOINTERFACE;
}
static void* g_hViewVtbl[10] = {(void*)h_QI, (void*)h_AddRef, (void*)h_Release, (void*)h_Stub, (void*)h_Stub,
                                (void*)h_Stub, (void*)h_ViewGetAt, (void*)h_ViewSize, (void*)h_ViewIndexOf,
                                (void*)h_ViewGetMany};
static HObj g_hView = {g_hViewVtbl};
static HRESULT MSABI h_GetControllers(void*, void** out) { *out = &g_hView; return S_OK; }
static void* g_hStaticsVtbl[16];
static HObj g_hStatics = {g_hStaticsVtbl};
static int g_unexpectedClass = 0;
static int g_userClassRequests = 0;
static HRESULT MSABI fakeGetActivationFactory(const wchar_t* name, const GUID* iid, void** out) {
    if (wcscmp(name, L"Windows.Xbox.Input.Controller") == 0 && IsEqualGUID(*iid, kIidStaticsH)) {
        *out = &g_hStatics;
        return S_OK;
    }
    if (wcscmp(name, L"Windows.Xbox.System.User") == 0) ++g_userClassRequests;   // system user fallback probe
    else ++g_unexpectedClass;
    if (out) *out = nullptr;
    return (HRESULT)0x80040154L;   // REGDB_E_CLASSNOTREG
}

typedef HRESULT (MSABI *Fn0)(void*);
typedef HRESULT (MSABI *FnU32Out)(void*, uint32_t*);
typedef HRESULT (MSABI *FnU64Out)(void*, uint64_t*);
typedef HRESULT (MSABI *FnF32Out)(void*, float*);
typedef HRESULT (MSABI *FnByteOut)(void*, uint8_t*);
typedef HRESULT (MSABI *FnPtrOut)(void*, void**);
typedef HRESULT (MSABI *FnQI)(void*, const GUID*, void**);
typedef HRESULT (MSABI *FnGetAt)(void*, uint32_t, void**);
typedef HRESULT (MSABI *FnSetRot)(void*, uint32_t);
typedef HRESULT (MSABI *FnSetVarF)(void*, void*, float);
typedef HRESULT (MSABI *FnEquation)(void*, uint8_t, void*, uint32_t, uint32_t*);
typedef void* (MSABI *FnWrapper)();
typedef HRESULT (WINAPI *FnWCS)(const wchar_t*, uint32_t, void**);
typedef const wchar_t* (WINAPI *FnRawBuf)(void*, uint32_t*);

static void** vt(void* o) { return *(void***)o; }
static void* slotOf(void* o, int i) { return vt(o)[i]; }

static void wheelTests(uint8_t* base, const uint8_t origFrame[5], FnWCS createString) {
    printf("virtual wheel:\n");
    bool hooked = false;
    for (int i = 0; i < 150 && !hooked; ++i) { hooked = memcmp(base + 0x226893, origFrame, 5) != 0; if (!hooked) Sleep(100); }
    CHECK(hooked, "frame hook call site re-pointed");
    CHECK(base[0x226893] == 0xE8, "frame hook keeps the call opcode");
    CHECK(*(void**)(base + 0x1dd1330) != (void*)&fakeGetActivationFactory, "activation import now points at the hook");

    void* view = ((FnWrapper)(base + 0x1078b80))();
    CHECK(view != nullptr, "game wrapper returned a controller list (%p)", view);
    if (!view) return;
    uint32_t size = 99;
    ((FnU32Out)slotOf(view, 7))(view, &size);
    CHECK(size == 2, "OverrideAzimuthWheel: list size == real count (2) - 1 + 1 (got %u)", size);
    {
        bool sawHarnessWheel = false;
        for (uint32_t i = 0; i < size; ++i) {
            void* c = nullptr;
            if (SUCCEEDED(((FnGetAt)slotOf(view, 6))(view, i, &c)) && c == (void*)&g_hWheelCtl) sawHarnessWheel = true;
        }
        CHECK(!sawHarnessWheel, "GetAt never returns the harness's (Azimuth's) wheel");
        void* c0 = nullptr; void* w0 = nullptr;
        ((FnGetAt)slotOf(view, 6))(view, 0, &c0);
        CHECK(c0 == (void*)&g_hPad && FAILED(((FnQI)slotOf(c0, 0))(c0, &kIidWheelH, &w0)), "GetAt(0) is the real pad");
        void* c2 = (void*)1;
        CHECK(((FnGetAt)slotOf(view, 6))(view, 2, &c2) == (HRESULT)0x8000000BL && c2 == nullptr, "GetAt(2) out of bounds");
        uint32_t idx = 9; uint8_t found = 0;
        typedef HRESULT (MSABI *FnIndexOf)(void*, void*, uint32_t*, uint8_t*);
        ((FnIndexOf)slotOf(view, 8))(view, &g_hWheelCtl, &idx, &found);
        CHECK(!found, "IndexOf(Azimuth's wheel) -> not found");
        ((FnIndexOf)slotOf(view, 8))(view, &g_hPad, &idx, &found);
        CHECK(found && idx == 0, "IndexOf(pad) == 0");
    }
    void* ctrl = nullptr;
    HRESULT hr = ((FnGetAt)slotOf(view, 6))(view, 1, &ctrl);
    CHECK(hr == S_OK && ctrl && ctrl != (void*)&g_hWheelCtl, "GetAt(1) is the plugin's wheel");
    if (!ctrl) return;
    void* gp = (void*)1;
    hr = ((FnQI)slotOf(ctrl, 0))(ctrl, &kIidGamepadH, &gp);
    CHECK(FAILED(hr) && gp == nullptr, "QI(IGamepad) fails (0x%08lx)", (unsigned long)hr);
    void* wheel = nullptr;
    hr = ((FnQI)slotOf(ctrl, 0))(ctrl, &kIidWheelH, &wheel);
    CHECK(hr == S_OK && wheel, "QI(IWheel) succeeds");
    if (!wheel) return;
    uint64_t id = 0;
    ((FnU64Out)slotOf(ctrl, 6))(ctrl, &id);
    CHECK(id == 0x4648325748454C31ull, "controller id constant (%llx)", (unsigned long long)id);
    void* user = (void*)1;
    hr = ((FnPtrOut)slotOf(ctrl, 8))(ctrl, &user);
    CHECK(hr == S_OK && user == nullptr, "get_User -> null with no real controller");

    uint32_t vp = 0;
    ((FnU32Out)slotOf(wheel, 10))(wheel, &vp);
    CHECK(vp == 0x046DC261, "IWheel slot 10 vendor/product 0x%08x", vp);

    void* reading = nullptr;
    hr = ((FnPtrOut)slotOf(wheel, 12))(wheel, &reading);
    CHECK(hr == S_OK && reading, "get_CurrentReading");
    if (!reading) return;
    float steer = -9, thr = -9;
    uint8_t a = 0;
    uint64_t ts = 0;
    ((FnF32Out)slotOf(reading, 16))(reading, &steer);
    ((FnF32Out)slotOf(reading, 18))(reading, &thr);
    ((FnByteOut)slotOf(reading, 6))(reading, &a);
    ((FnU64Out)slotOf(reading, 33))(reading, &ts);
    CHECK(std::fabs(steer - 0.25f) < 1e-5f, "reading steering 0.25 (got %f)", steer);
    CHECK(std::fabs(thr - 0.5f) < 1e-5f, "reading throttle 0.5 (got %f)", thr);
    CHECK(a == 1, "reading A pressed");
    CHECK(ts != 0, "reading timestamp non-zero");
    ((Fn0)slotOf(reading, 2))(reading);

    hr = ((FnSetRot)slotOf(wheel, 14))(wheel, 450);
    CHECK(hr == S_OK, "SetRotation(450)");
    reading = nullptr;
    ((FnPtrOut)slotOf(wheel, 12))(wheel, &reading);
    steer = -9;
    if (reading) { ((FnF32Out)slotOf(reading, 16))(reading, &steer); ((Fn0)slotOf(reading, 2))(reading); }
    CHECK(std::fabs(steer - 0.5f) < 1e-5f, "steering 0.5 after SetRotation(450) (got %f)", steer);

    void* hs = nullptr;
    createString(L"Torque", 6, &hs);
    hr = ((FnSetVarF)slotOf(wheel, 38))(wheel, hs, 0.3f);
    CHECK(hr == S_OK, "SetVariableFloat returns S_OK");
    uint32_t outLen = 0xDEADBEEF;
    hr = ((FnEquation)slotOf(wheel, 26))(wheel, 1, hs, 0, &outLen);
    CHECK(hr == S_OK && outLen == 0, "SetEquation: outLen == 0 (got %u)", outLen);

    void* cfg = nullptr;
    hr = ((FnPtrOut)slotOf(wheel, 13))(wheel, &cfg);
    CHECK(hr == S_OK && cfg, "get_StaticConfiguration");
    if (cfg) {
        uint32_t maxRot = 0;
        ((FnU32Out)slotOf(cfg, 12))(cfg, &maxRot);
        CHECK(maxRot == 900, "StaticConfig max rotation 900 (got %u)", maxRot);
        ((Fn0)slotOf(cfg, 2))(cfg);
    }
    CHECK(g_userClassRequests > 0, "empty controller list: plugin asked for the system user and survived its failure (%d)", g_userClassRequests);
    CHECK(g_unexpectedClass == 0, "no unexpected activation requests (%d)", g_unexpectedClass);
}

// Calls fn(a, b) with rsi = rsiValue (the game's FFB call site has the car vector element in rsi).
// rsi is callee-saved in the Windows x64 ABI, so the helper saves and restores it.
extern "C" void callWithRsiAsm(void* fn, void* a, void* b, void* rsiValue);
asm(R"(
.text
.globl callWithRsiAsm
callWithRsiAsm:
    push %rsi
    sub $32, %rsp
    mov %r9, %rsi
    mov %rcx, %rax
    mov %rdx, %rcx
    mov %r8, %rdx
    call *%rax
    add $32, %rsp
    pop %rsi
    ret
)");
static void callWithRsi(void* fn, uint8_t* slot, const float* in, void* rsiValue) {
    callWithRsiAsm(fn, slot, (void*)in, rsiValue);
}

typedef bool (*FnLastFfb)(fh2::GameFfbInput*);
typedef void (MSABI *FnUpdateFfb)(uint8_t*, const float*);
typedef float (MSABI *FnGetFov)(void*);

// Fake car for the front-slip capture: car + 0x4c08 -> phys, phys -> vtable of 0xC4 entries.
static float MSABI fakeSlip(void*, int i) { static const float s[4] = {0.3f, 0.3f, 0.1f, 0.1f}; return i >= 0 && i < 4 ? s[i] : 0.0f; }
static float MSABI fakeLoad(void*, int) { return 1.0f; }
static void MSABI fakeUnexpected() { printf("  FAIL: unexpected physics vtable entry called\n"); fflush(stdout); abort(); }

static void ffbTests(uint8_t* base, HMODULE plugin) {
    printf("game FFB input hook:\n");
    const uintptr_t kSite = 0x2fe134, kOrigTarget = 0x10743c0;
    bool hooked = false;
    for (int i = 0; i < 150 && !hooked; ++i) { hooked = base[kSite + 1] != 0x87 || base[kSite + 2] != 0x62; if (!hooked) Sleep(100); }
    int32_t rel; memcpy(&rel, base + kSite + 1, 4);
    uint8_t* target = base + kSite + 5 + rel;
    CHECK(base[kSite] == 0xE8, "call opcode preserved");
    CHECK(target != base + kOrigTarget && hooked, "call site no longer points at UpdateFFB (%p)", (void*)target);
    FnLastFfb last = (FnLastFfb)GetProcAddress(plugin, "FH2SI_TestLastGameFfbInput");
    CHECK(last != nullptr, "plugin test accessor exported");
    if (!hooked || !last) return;

    alignas(16) static uint8_t slot[0x1400];
    alignas(16) static float driver[32];
    memset(slot, 0, sizeof(slot)); memset(driver, 0, sizeof(driver));
    *(float**)(slot + 0x10c0) = driver;
    driver[0] = 1.0f;
    driver[0x28 / 4] = 0.42f; driver[0x2c / 4] = 1.0f; driver[0x48 / 4] = 0.5f;
    driver[0x54 / 4] = 0.32f; driver[0x58 / 4] = 0.55f; driver[0x5c / 4] = 1.0f; driver[0x60 / 4] = 0.0f;
    driver[0x64 / 4] = 2.0f; driver[0x68 / 4] = 15.0f; driver[0x6c / 4] = 0.15f; driver[0x70 / 4] = 1.0f;
    const float in[3] = {0.5f, 20.0f, 0.1f};
    float inCopy[3] = {in[0], in[1], in[2]};
    callWithRsi((void*)target, slot, inCopy, (void*)(uintptr_t)1);   // garbage rsi must be tolerated
    CHECK(std::fabs(driver[1] - 0.21f) < 1e-4f, "original UpdateFFB ran: torque output %f", driver[1]);
    CHECK(memcmp(in, inCopy, sizeof(in)) == 0, "hook left the input untouched");
    fh2::GameFfbInput g;
    bool got = last(&g);
    CHECK(got, "plugin captured the input");
    CHECK(std::fabs(g.rawTorque - 0.5f) < 1e-6f && std::fabs(g.speed - 20.0f) < 1e-6f && std::fabs(g.slip - 0.1f) < 1e-6f &&
          std::fabs(g.gain - 1.0f) < 1e-6f, "captured raw %f speed %f slip %f gain %f", g.rawTorque, g.speed, g.slip, g.gain);
    CHECK(std::fabs(g.dynScale - 0.42f) < 1e-6f && std::fabs(g.springParam - 0.5f) < 1e-6f && std::fabs(g.slip1 - 0.55f) < 1e-6f &&
          std::fabs(g.speed0 - 2.0f) < 1e-6f && std::fabs(g.speedScale1 - 1.0f) < 1e-6f, "captured driver parameters");
    CHECK(!g.haveFront, "garbage rsi: no front slip captured");

    // fake car: front slip comes from the physics object's vtable
    alignas(16) static uint8_t carBuf[0x4c10];
    alignas(16) static uint8_t physBuf[64];
    static void* fakeVt[0xC4];
    for (auto& e : fakeVt) e = (void*)&fakeUnexpected;
    fakeVt[0x578 / 8] = (void*)&fakeSlip;
    fakeVt[0x610 / 8] = (void*)&fakeLoad;
    memset(carBuf, 0, sizeof(carBuf)); memset(physBuf, 0, sizeof(physBuf));
    *(void**)physBuf = fakeVt;
    *(void**)(carBuf + 0x4c08) = physBuf;
    static void* carElemFake;
    carElemFake = carBuf;
    float inCopy2[3] = {in[0], in[1], in[2]};
    callWithRsi((void*)target, slot, inCopy2, &carElemFake);
    CHECK(last(&g), "plugin captured the input with a fake car");
    CHECK(g.haveFront && std::fabs(g.frontSlip - 0.3f) < 1e-6f, "front slip %f (have %d), expected 0.3", g.frontSlip, (int)g.haveFront);

    // steering lock per car (RealisticSteeringLock=1, WheelRange 900, game rotation 450, TestMode steering 0.25)
    printf("steering lock:\n");
    auto readSteer = [&]() -> float {
        float st = -9;
        void* view = ((FnWrapper)(base + 0x1078b80))();
        void* ctrl = nullptr; void* wheel = nullptr; void* reading = nullptr;
        if (view && SUCCEEDED(((FnGetAt)slotOf(view, 6))(view, 1, &ctrl)) && ctrl &&
            SUCCEEDED(((FnQI)slotOf(ctrl, 0))(ctrl, &kIidWheelH, &wheel)) && wheel &&
            SUCCEEDED(((FnPtrOut)slotOf(wheel, 12))(wheel, &reading)) && reading) {
            ((FnF32Out)slotOf(reading, 16))(reading, &st);
            ((Fn0)slotOf(reading, 2))(reading);
        }
        return st;
    };
    auto setCar = [&](uint32_t id) {
        *(uint32_t*)(carBuf + 0x118) = id;
        float inCopy3[3] = {in[0], in[1], in[2]};
        callWithRsi((void*)target, slot, inCopy3, &carElemFake);
    };
    const fh2::CarInfo *low = nullptr, *high = nullptr;
    for (size_t i = 0; i < fh2::kCarCount; ++i) {
        if (!low && fh2::kCars[i].lockDeg < 850) low = &fh2::kCars[i];
        if (!high && fh2::kCars[i].lockDeg > 950) high = &fh2::kCars[i];
    }
    CHECK(low && high, "car list has cars with a lock below 850 and above 950");
    if (low && high) {
        setCar(low->id);
        float expect = 0.25f * 900.0f / (float)low->lockDeg;
        float st = readSteer();
        CHECK(std::fabs(st - expect) < 1e-4f, "car %u (lock %u): steering %f, expected %f", low->id, (unsigned)low->lockDeg, st, expect);
        setCar(high->id);
        st = readSteer();
        CHECK(std::fabs(st - 0.25f) < 1e-4f, "car %u (lock %u, capped at WheelRange): steering %f, expected 0.25", high->id, (unsigned)high->lockDeg, st);
        setCar(0xFFFFF);
        st = readSteer();
        CHECK(std::fabs(st - 0.5f) < 1e-4f, "unknown car: the game's rotation (450) -> steering %f, expected 0.5", st);
    }
}

typedef void (*FnTestCockpit)(uint8_t*, float);

static void cockpitTests(uint8_t* base, HMODULE plugin, uint64_t slotOrig, const std::wstring& logPath) {
    printf("cockpit wheel and driver:\n");
    const uintptr_t kSlot = 0x2c5cc00, kCarUpdate = 0x2a9400, kSetDriver = 0xf75500;
    CHECK(slotOrig == (uint64_t)(base + kCarUpdate), "car update slot held the original before loading");
    uint64_t now = *(uint64_t*)(base + kSlot);
    CHECK(now != slotOrig, "car update slot re-pointed");
    static const uint8_t expect[10] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
    CHECK(memcmp(base + kSetDriver, expect, sizeof(expect)) == 0, "Car_SetDriverVisible first 10 bytes");
    {
        FILE* f = _wfopen(logPath.c_str(), L"rb");
        std::string text;
        if (f) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) text.append(b, n); fclose(f); }
        CHECK(text.find("cockpit: driver hide ready") != std::string::npos, "log: cockpit: driver hide ready");
        CHECK(text.find("cockpit: car update hook installed") != std::string::npos, "log: car update hook installed");
    }
    FnTestCockpit fn = (FnTestCockpit)GetProcAddress(plugin, "FH2SI_TestCockpitWheel");
    CHECK(fn != nullptr, "FH2SI_TestCockpitWheel exported");
    if (!fn) return;
    static uint8_t car[0x5000];
    memset(car, 0, sizeof(car));
    fn(car, 0.5f);
    float a; memcpy(&a, car + 0x4f30, 4);
    CHECK(std::fabs(a - 3.92699f) < 1e-4f, "steering 0.5 at display lock 900 -> %f rad (3.92699)", a);
    fn(car, -1.0f);
    memcpy(&a, car + 0x4f30, 4);
    CHECK(std::fabs(a + 7.85398f) < 1e-4f, "steering -1 -> %f rad (-7.85398)", a);
    car[0x4f11] = 1;
    float before = a;
    fn(car, 0.25f);
    memcpy(&a, car + 0x4f30, 4);
    CHECK(a == before, "frozen car (0x4f11 != 0): angle untouched");
}

typedef uint8_t (MSABI *FnGetter)(void*);

static bool autoOnlyBox(char g) { return g == 'a' || g == 'c' || g == 'e'; }

// abs / stm '*' = any
// clutch car, skipping the cars used by the assist override lines
static const fh2::CarInfo* findClutch(char clutch, int skip, const fh2::CarInfo* x1 = nullptr, const fh2::CarInfo* x2 = nullptr) {
    for (size_t i = 0; i < fh2::kCarCount; ++i) {
        const fh2::CarInfo& c = fh2::kCars[i];
        if (c.clutch == clutch && !autoOnlyBox(c.gearbox) && &c != x1 && &c != x2 && skip-- == 0) return &c;
    }
    return nullptr;
}

// car with this gearbox (and clutch, '*' = any), skipping up to four cars already used by override lines
static const fh2::CarInfo* findGearbox(char gearbox, char clutch, int skip, const fh2::CarInfo* x1 = nullptr, const fh2::CarInfo* x2 = nullptr,
                                       const fh2::CarInfo* x3 = nullptr, const fh2::CarInfo* x4 = nullptr) {
    for (size_t i = 0; i < fh2::kCarCount; ++i) {
        const fh2::CarInfo& c = fh2::kCars[i];
        if (c.gearbox == gearbox && (clutch == '*' || c.clutch == clutch) && &c != x1 && &c != x2 && &c != x3 && &c != x4 && skip-- == 0) return &c;
    }
    return nullptr;
}

// Cars for the shifting-mode checks; the start-up code writes the override lines for the same picks.
struct ShiftPicks {
    const fh2::CarInfo *ov, *ov3, *cyOv, *cnOv, *cy, *manualAuto, *atOn, *atPlain, *dct;
};
static ShiftPicks pickShiftCars();

static const fh2::CarInfo* findCarBy(char abs, char tc, char stm, int skip) {
    for (size_t i = 0; i < fh2::kCarCount; ++i)
        if ((abs == '*' || fh2::kCars[i].abs == abs) && fh2::kCars[i].tc == tc && (stm == '*' || fh2::kCars[i].stm == stm) && skip-- == 0)
            return &fh2::kCars[i];
    return nullptr;
}

static ShiftPicks pickShiftCars() {
    ShiftPicks p{};
    p.ov = findCarBy('y', 'y', 'y', 1);
    p.ov3 = findCarBy('y', 'y', 'y', 2);
    p.cyOv = findClutch('y', 0, p.ov, p.ov3);
    p.cnOv = findClutch('n', 0, p.ov, p.ov3);
    p.cy = findClutch('y', 1, p.ov, p.ov3);
    p.manualAuto = findGearbox('m', 'y', 0, p.ov, p.ov3, p.cyOv, p.cy);          // manual car forced to automatic shifting
    p.atOn = findGearbox('a', '*', 0);                                           // automatic-only car with clutch override "on"
    p.atPlain = findGearbox('a', '*', 1);
    p.dct = findGearbox('d', 'n', 0, p.ov, p.ov3, p.cyOv, p.cnOv);
    return p;
}

static void assistTests(uint8_t* base, uint64_t absOrig, uint64_t stmOrig, uint64_t tcOrig, uint64_t clutchOrig) {
    printf("realistic assists:\n");
    const uintptr_t kVt[2] = {0x2c5bfc8, 0x2cf39c0};
    bool patched = false;
    for (int i = 0; i < 150 && !patched; ++i) {
        patched = *(uint64_t*)(base + kVt[0] + 0xd8) != absOrig && *(uint64_t*)(base + kVt[1] + 0xf0) != tcOrig;
        if (!patched) Sleep(100);
    }
    for (int v = 0; v < 2; ++v) {
        CHECK(*(uint64_t*)(base + kVt[v] + 0xd8) != absOrig && *(uint64_t*)(base + kVt[v] + 0xd8) != 0, "vtable %d ABS slot re-pointed", v);
        CHECK(*(uint64_t*)(base + kVt[v] + 0xf0) != tcOrig && *(uint64_t*)(base + kVt[v] + 0xf0) != 0, "vtable %d TC slot re-pointed", v);
        CHECK(*(uint64_t*)(base + kVt[v] + 0xe8) != stmOrig && *(uint64_t*)(base + kVt[v] + 0xe8) != 0, "vtable %d STM slot re-pointed", v);
        CHECK(*(uint64_t*)(base + kVt[v] + 0xf8) != clutchOrig && *(uint64_t*)(base + kVt[v] + 0xf8) != 0, "vtable %d clutch slot re-pointed", v);
    }
    if (!patched) return;

    // fake LocalPlayerChoices: ABS off and TC off in the game settings
    alignas(16) static uint8_t choices[0x300];
    memset(choices, 0, sizeof(choices));
    *(uint64_t*)choices = (uint64_t)(base + kVt[0]);
    choices[0x2b1] = 2;
    choices[0x2b4] = 1;
    FnGetter absG = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xd8);
    FnGetter tcG  = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xf0);
    FnGetter stmG = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xe8);
    choices[0x2b3] = 1;
    CHECK(absG(choices) == 2 && tcG(choices) == 1 && stmG(choices) == 1, "no car seen yet: game values pass through (abs %u tc %u stm %u)", absG(choices), tcG(choices), stmG(choices));

    // call the patched UpdateFFB call-site target with rsi = &elem
    const uintptr_t kSite = 0x2fe134;
    int32_t rel; memcpy(&rel, base + kSite + 1, 4);
    void* target = base + kSite + 5 + rel;
    alignas(16) static uint8_t slot[0x1400];
    alignas(16) static float driver[32];
    memset(slot, 0, sizeof(slot)); memset(driver, 0, sizeof(driver));
    *(float**)(slot + 0x10c0) = driver;
    driver[0] = 1.0f;
    const float in[3] = {0.5f, 20.0f, 0.1f};
    alignas(16) static uint8_t carObj[0x200];
    void* elem = carObj;

    const fh2::CarInfo* yy = findCarBy('y', 'y', '*', 0);
    const fh2::CarInfo* yy2 = findCarBy('y', 'y', 'y', 1);      // 2-field override "off,on" (STM stays auto)
    const fh2::CarInfo* nn = findCarBy('n', 'n', '*', 0);
    const fh2::CarInfo* tcNoStm = findCarBy('*', 'y', 'n', 0);   // TC yes, STM no
    const fh2::CarInfo* allYes = findCarBy('y', 'y', 'y', 0);
    const fh2::CarInfo* ov3 = findCarBy('y', 'y', 'y', 2);       // 3-field override "auto,off,on"
    CHECK(yy && yy2 && nn && tcNoStm && allYes && ov3, "test cars found: ABS+TC car %u (%s), 2-field override car %u, neither car %u (%s), "
          "TC-yes/STM-no car %u, all-yes car %u, 3-field override car %u", yy ? yy->id : 0, yy ? yy->name : "-",
          yy2 ? yy2->id : 0, nn ? nn->id : 0, nn ? nn->name : "-", tcNoStm ? tcNoStm->id : 0, allYes ? allYes->id : 0,
          ov3 ? ov3->id : 0);
    if (!yy || !yy2 || !nn || !tcNoStm || !allYes || !ov3) return;

    memset(carObj, 0, sizeof(carObj));
    *(uint32_t*)(carObj + 0x118) = yy->id;
    callWithRsi(target, slot, in, &elem);
    CHECK(absG(choices) == 1 && tcG(choices) == 0, "car %u (ABS yes, TC yes): ABS on, TC on (abs %u tc %u)", yy->id, absG(choices), tcG(choices));
    CHECK(((FnGetter)*(uint64_t*)(base + kVt[1] + 0xd8))(choices) == 1 && ((FnGetter)*(uint64_t*)(base + kVt[1] + 0xf0))(choices) == 0,
          "second vtable behaves the same");
    choices[0x2b1] = 0;
    CHECK(absG(choices) == 0, "assisted braking (mode 0) is left alone (got %u)", absG(choices));
    choices[0x2b1] = 2;

    *(uint32_t*)(carObj + 0x118) = nn->id;
    callWithRsi(target, slot, in, &elem);
    choices[0x2b1] = 1; choices[0x2b4] = 0; choices[0x2b3] = 0;       // user has all three on
    CHECK(absG(choices) == 2 && tcG(choices) == 1 && stmG(choices) == (nn->stm == 'n' ? 1u : 0u),
          "car %u (ABS no, TC no): ABS off, TC off (abs %u tc %u stm %u)", nn->id, absG(choices), tcG(choices), stmG(choices));
    choices[0x2b1] = 2; choices[0x2b4] = 1; choices[0x2b3] = 1;

    *(uint32_t*)(carObj + 0x118) = tcNoStm->id;   // TC yes, STM no: TC forced on, STM forced off
    callWithRsi(target, slot, in, &elem);
    choices[0x2b4] = 1; choices[0x2b3] = 0;       // user has TC off, STM on
    CHECK(tcG(choices) == 0 && stmG(choices) == 1, "car %u (TC yes, STM no): TC getter 0, STM getter 1 (tc %u stm %u)", tcNoStm->id, tcG(choices), stmG(choices));
    choices[0x2b4] = 1; choices[0x2b3] = 1;

    *(uint32_t*)(carObj + 0x118) = allYes->id;    // TC yes, STM yes: both on
    callWithRsi(target, slot, in, &elem);
    choices[0x2b4] = 1; choices[0x2b3] = 1;       // user has both off
    CHECK(tcG(choices) == 0 && stmG(choices) == 0, "car %u (TC yes, STM yes): TC getter 0, STM getter 0 (tc %u stm %u)", allYes->id, tcG(choices), stmG(choices));

    *(uint32_t*)(carObj + 0x118) = yy2->id;       // per-car override file says <id>=off,on (old 2-field format)
    callWithRsi(target, slot, in, &elem);
    choices[0x2b1] = 1; choices[0x2b4] = 1; choices[0x2b3] = 1;
    CHECK(absG(choices) == 2 && tcG(choices) == 0, "car %u override off,on: ABS off, TC on (abs %u tc %u)", yy2->id, absG(choices), tcG(choices));
    CHECK(stmG(choices) == 0, "car %u 2-field override leaves STM auto (research: yes -> getter %u)", yy2->id, stmG(choices));
    choices[0x2b1] = 2;

    *(uint32_t*)(carObj + 0x118) = ov3->id;       // <id>=auto,off,on
    callWithRsi(target, slot, in, &elem);
    choices[0x2b4] = 0; choices[0x2b3] = 1;       // user has TC on, STM off
    CHECK(tcG(choices) == 1 && stmG(choices) == 0, "car %u override auto,off,on: TC 1, STM 0 (tc %u stm %u)", ov3->id, tcG(choices), stmG(choices));
    choices[0x2b4] = 1; choices[0x2b3] = 1;

    *(uint32_t*)(carObj + 0x118) = 999999;        // not in the table
    callWithRsi(target, slot, in, &elem);
    choices[0x2b1] = 1; choices[0x2b4] = 0; choices[0x2b3] = 0;
    CHECK(absG(choices) == 1 && tcG(choices) == 0 && stmG(choices) == 0, "unknown car: game values (abs %u tc %u stm %u)", absG(choices), tcG(choices), stmG(choices));

    callWithRsi(target, slot, in, nullptr);       // unreadable car vector element: no crash, car unchanged
    callWithRsi(target, slot, in, (void*)(uintptr_t)8);
    void* badElem = (void*)(uintptr_t)16;
    callWithRsi(target, slot, in, &badElem);
    CHECK(true, "garbage rsi values tolerated");

    // ---- realistic clutch: shifting mode 0 automatic, 1 manual, 2 manual with clutch ----
    {
        FnGetter clG  = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xf8);
        FnGetter clG1 = (FnGetter)*(uint64_t*)(base + kVt[1] + 0xf8);
        const ShiftPicks sp = pickShiftCars();                       // same picks as the override lines written at start-up
        const fh2::CarInfo *a1 = sp.ov, *a2 = sp.ov3;
        const fh2::CarInfo* cyOv = sp.cyOv;
        const fh2::CarInfo* cnOv = sp.cnOv;
        const fh2::CarInfo* cy = sp.cy;                              // plain cars: no clutch override
        const fh2::CarInfo* cn = findClutch('n', 1, a1, a2);
        const fh2::CarInfo* co = findClutch('o', 0, a1, a2);
        CHECK(cy && cn && co && cyOv && cnOv, "clutch test cars found: yes %u, no %u, optional %u, override yes->off %u, override no->on %u",
              cy ? cy->id : 0, cn ? cn->id : 0, co ? co->id : 0, cyOv ? cyOv->id : 0, cnOv ? cnOv->id : 0);
        if (cy && cn && co && cyOv && cnOv) {
            auto setCar = [&](uint32_t id) {
                *(uint32_t*)(carObj + 0x118) = id;
                callWithRsi(target, slot, in, &elem);
            };
            auto mode = [&](FnGetter g, uint8_t real) { choices[0x2b5] = real; return (unsigned)g(choices); };
            setCar(999999);
            CHECK(mode(clG, 0) == 0 && mode(clG, 1) == 1 && mode(clG, 2) == 2, "unknown car: shifting mode unchanged");
            setCar(cy->id);
            CHECK(mode(clG, 1) == 2 && mode(clG, 2) == 2 && mode(clG, 0) == 0,
                  "car %u (clutch yes): manual 1 -> 2, 2 -> 2, automatic 0 -> 0 (got %u %u %u)", cy->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
            CHECK(mode(clG1, 1) == 2, "second vtable behaves the same (clutch yes, 1 -> 2)");
            setCar(cn->id);
            CHECK(mode(clG, 2) == 1 && mode(clG, 1) == 1 && mode(clG, 0) == 0,
                  "car %u (clutch no): 2 -> 1, 1 -> 1, 0 -> 0 (got %u %u %u)", cn->id, mode(clG, 2), mode(clG, 1), mode(clG, 0));
            CHECK(mode(clG1, 2) == 1, "second vtable behaves the same (clutch no, 2 -> 1)");
            setCar(co->id);
            CHECK(mode(clG, 0) == 0 && mode(clG, 1) == 1 && mode(clG, 2) == 2, "car %u (clutch optional, OptionalAssists=game): unchanged", co->id);
            setCar(cyOv->id);              // <id>=auto,auto,auto,off
            CHECK(mode(clG, 2) == 1 && mode(clG, 1) == 1 && mode(clG, 0) == 0,
                  "car %u (clutch yes, 4-field override clutch off): 2 -> 1, 1 -> 1, 0 -> 0", cyOv->id);
            setCar(cnOv->id);              // <id>=auto,auto,auto,on
            CHECK(mode(clG, 1) == 2 && mode(clG, 2) == 2 && mode(clG, 0) == 0,
                  "car %u (clutch no, 4-field override clutch on): 1 -> 2, 2 -> 2, 0 -> 0", cnOv->id);
            // ---- RealisticAutomatic=1 (set in the harness ini) ----
            CHECK(sp.atOn && sp.atPlain && sp.dct && sp.manualAuto, "automatic test cars found: at %u / %u, dct %u, manual %u",
                  sp.atOn ? sp.atOn->id : 0, sp.atPlain ? sp.atPlain->id : 0, sp.dct ? sp.dct->id : 0, sp.manualAuto ? sp.manualAuto->id : 0);
            if (sp.atOn && sp.atPlain && sp.dct && sp.manualAuto) {
                setCar(sp.atPlain->id);        // gearbox 'a', no override
                CHECK(mode(clG, 1) == 0 && mode(clG, 2) == 0 && mode(clG, 0) == 0,
                      "car %u (gearbox at): 1 -> 0, 2 -> 0, 0 -> 0 (got %u %u %u)", sp.atPlain->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
                CHECK(mode(clG1, 2) == 0, "second vtable behaves the same (gearbox at, 2 -> 0)");
                setCar(sp.dct->id);            // gearbox 'd', clutch 'n': existing behaviour
                CHECK(mode(clG, 2) == 1 && mode(clG, 1) == 1 && mode(clG, 0) == 0,
                      "car %u (gearbox dct, clutch no): 2 -> 1, 1 -> 1, 0 -> 0 (got %u %u %u)", sp.dct->id, mode(clG, 2), mode(clG, 1), mode(clG, 0));
                setCar(sp.manualAuto->id);     // <id>=auto,auto,auto,automatic
                CHECK(mode(clG, 1) == 0 && mode(clG, 2) == 0 && mode(clG, 0) == 0,
                      "car %u (manual, 4-field override automatic): 1 -> 0, 2 -> 0 (got %u %u %u)", sp.manualAuto->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
                setCar(sp.atOn->id);           // <id>=auto,auto,auto,on cancels forcing
                CHECK(mode(clG, 1) == 2 && mode(clG, 2) == 2 && mode(clG, 0) == 0,
                      "car %u (gearbox at, override clutch on): forcing cancelled, 1 -> 2 (got %u %u %u)", sp.atOn->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
            }
            choices[0x2b5] = 0;
        }
    }
}

static void fovTests(uint8_t* base) {
    printf("field of view:\n");
    const uintptr_t kGetFov = 0x10340f0;
    uint64_t cockpit = *(uint64_t*)(base + 0x2e77c30 + 14 * 8);
    uint64_t hood    = *(uint64_t*)(base + 0x2e77ea8 + 14 * 8);
    CHECK(cockpit != (uint64_t)(base + kGetFov), "CCamCockpit GetFOV slot re-pointed");
    CHECK(hood == (uint64_t)(base + kGetFov), "CCamHood GetFOV slot unchanged (FovHood=0)");
    alignas(16) static uint8_t cam[0x400];
    memset(cam, 0, sizeof(cam));
    *(float*)(cam + 0x1d0) = 45.0f;
    cam[0x218] = 0;
    float v = ((FnGetFov)cockpit)(cam);
    CHECK(std::fabs(v - 55.0f) < 1e-4f, "cockpit FOV 45 + 10 = %f", v);
    v = ((FnGetFov)hood)(cam);
    CHECK(std::fabs(v - 45.0f) < 1e-4f, "hood FOV unchanged = %f", v);
}

static void drawDistTests(uint8_t* base, HMODULE plugin) {
    printf("draw distance:\n");
    // A. the tail-jump thunk in front of the track settings deserializer points at our hook
    uint8_t* jmp = base + 0x134af97;
    int32_t rel; memcpy(&rel, jmp + 1, 4);
    uint8_t* target = jmp + 5 + rel;
    CHECK(jmp[0] == 0xE9, "thunk keeps the jmp opcode");
    CHECK(target != base + 0x1144320, "track settings jmp re-pointed away from Serialize (%p)", (void*)target);
    typedef void (MSABI *FnApplyTrack)(uint8_t*);
    typedef void (MSABI *FnApplyTraffic)();
    auto applyTrack = (FnApplyTrack)GetProcAddress(plugin, "FH2SI_TestApplyTrackOverrides");
    auto applyTraffic = (FnApplyTraffic)GetProcAddress(plugin, "FH2SI_TestApplyTrafficOverrides");
    CHECK(applyTrack && applyTraffic, "test exports found");
    if (!applyTrack || !applyTraffic) return;

    alignas(16) static uint8_t ts[0x600];
    memset(ts, 0, sizeof(ts));
    auto F = [&](int off) -> float& { return *(float*)(ts + off); };
    F(0x234) = 85.0f; F(0x5e4) = 360.0f; F(0x32c) = 4000.0f; F(0x168) = 100.0f; F(0x404) = 250.0f;
    applyTrack(ts);
    CHECK(F(0x234) == 200.0f && F(0x5f4) == 200.0f, "shadow 85 -> %.0f, screen-area shadow 0 -> %.0f", F(0x234), F(0x5f4));
    CHECK(F(0x5e4) == 800.0f, "world objects 360 -> %.0f", F(0x5e4));
    CHECK(F(0x32c) == 4000.0f, "props: larger game value kept (%.0f)", F(0x32c));
    CHECK(F(0x168) == 100.0f && F(0x404) == 250.0f && F(0x164) == 0.0f && F(0x40c) == 0.0f && F(0x410) == 0.0f,
          "grass and crowd untouched with their settings at 0");

    // B. traffic: the game's static initializers did not run here, so lay the registry variables out first
    struct Var { uintptr_t rva; float def, max; };
    const Var vars[] = {{0x36e8c80, 100.0f, 500.0f}, {0x36e8d88, 150.0f, 500.0f}, {0x36e8160, 30.0f, 1000.0f}, {0x36e8048, 120.0f, 1000.0f}};
    for (auto& v : vars) {
        *(float*)(base + v.rva + 0x10) = v.def;
        *(float*)(base + v.rva + 0x14) = v.def;
        *(float*)(base + v.rva + 0x20) = v.max;
    }
    applyTraffic();
    float tmin = *(float*)(base + 0x36e8c80 + 0x10), tmax = *(float*)(base + 0x36e8d88 + 0x10),
          thigh = *(float*)(base + 0x36e8160 + 0x10), tmed = *(float*)(base + 0x36e8048 + 0x10);
    CHECK(tmin == 250.0f && tmax == 300.0f, "traffic fade %.0f-%.0f (want 250-300)", tmin, tmax);
    CHECK(tmed == 300.0f && thigh == 30.0f, "medium LOD %.0f (want 300), high LOD %.0f unchanged", tmed, thigh);
    // a mismatching layout is left alone
    *(float*)(base + 0x36e8d88 + 0x14) = 999.0f;
    *(float*)(base + 0x36e8d88 + 0x10) = 150.0f;
    applyTraffic();
    CHECK(*(float*)(base + 0x36e8d88 + 0x10) == 150.0f, "registry mismatch: unchanged");

    // C. racer cars: CarLODTransDist parse / defaults slots
    uint64_t* slots = (uint64_t*)(base + 0x2eb9bc0);
    CHECK(slots[0] == (uint64_t)(base + 0x1307230), "CarLODTransDist name getter slot unchanged");
    CHECK(slots[1] != (uint64_t)(base + 0x12ffa90), "parse slot re-pointed");
    CHECK(slots[2] != (uint64_t)(base + 0x130bb20), "defaults slot re-pointed");
    alignas(16) static uint8_t obj[0x40];
    memset(obj, 0, sizeof(obj));
    typedef uint64_t (MSABI *FnCar)(void*, void*, void*, void*);
    ((FnCar)slots[2])(obj, nullptr, nullptr, nullptr);
    float* lod = (float*)(obj + 8);
    CHECK(lod[0] == 30.0f && lod[1] == 50.0f && lod[2] == 100.0f && lod[3] == 120.0f,
          "default car LOD distances %.0f/%.0f/%.0f/%.0f (want 30/50/100/120: built-in defaults 15/25/50/60 x2)", lod[0], lod[1], lod[2], lod[3]);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { printf("usage: harness <game.exe> <FH2SimImmersion.dll> [--deep]\n"); return 2; }
    bool deep = argc > 3 && wcscmp(argv[3], L"--deep") == 0;
    uint8_t* base = mapImage(argv[1], deep ? 0x230000 : 0);
    if (!base) { printf("could not map game image\n"); return 2; }
    printf("game image mapped at %p\n", base);
    uint8_t original[5]; memcpy(original, base + 0x103832A, 5);
    uint8_t origFrame[5]; memcpy(origFrame, base + 0x226893, 5);

    // Emulate Azimuth for the wheel tests: bind the imports the plugin and the game use.
    HMODULE combase = LoadLibraryW(L"combase.dll");
    FnRawBuf rawBuf = combase ? (FnRawBuf)GetProcAddress(combase, "WindowsGetStringRawBuffer") : nullptr;
    FnWCS createString = combase ? (FnWCS)GetProcAddress(combase, "WindowsCreateString") : nullptr;
    if (!rawBuf || !createString) { printf("combase string functions missing\n"); return 2; }
    for (int i = 0; i < 3; ++i) g_hStaticsVtbl[i] = g_hViewVtbl[i];
    g_hStaticsVtbl[3] = g_hStaticsVtbl[4] = g_hStaticsVtbl[5] = (void*)h_Stub;
    for (int i = 0; i < 16; ++i) g_hPadVtbl[i] = g_hWheelVtbl[i] = (void*)h_Stub;
    g_hPadVtbl[0] = g_hWheelVtbl[0] = (void*)h_CtlQI;
    g_hPadVtbl[1] = g_hWheelVtbl[1] = (void*)h_AddRef;
    g_hPadVtbl[2] = g_hWheelVtbl[2] = (void*)h_Release;
    g_hPadVtbl[8] = g_hWheelVtbl[8] = (void*)h_GetUserNull;
    g_hStaticsVtbl[6] = (void*)h_GetControllers;
    for (int i = 7; i < 16; ++i) g_hStaticsVtbl[i] = (void*)h_Stub;
    *(void**)(base + 0x1dd1330) = (void*)&fakeGetActivationFactory;
    *(void**)(base + 0x1dd0ed8) = (void*)rawBuf;

    // No real wheel here: run the virtual wheel on synthetic input (TestMode) for this run.
    std::wstring iniPath = argv[2];
    std::wstring carsPath, logPath;
    {
        size_t dot = iniPath.find_last_of(L'.');
        iniPath = (dot == std::wstring::npos ? iniPath : iniPath.substr(0, dot)) + L".ini";
        size_t sep = iniPath.find_last_of(L"\\/");
        carsPath = (sep == std::wstring::npos ? std::wstring() : iniPath.substr(0, sep + 1)) + L"FH2SimImmersion_cars.ini";
        logPath = (sep == std::wstring::npos ? std::wstring() : iniPath.substr(0, sep + 1)) + L"FH2SimImmersion.log";
        WritePrivateProfileStringW(L"Input", L"ConfigVersion", L"4", iniPath.c_str());
        WritePrivateProfileStringW(L"Wheel", L"TestMode", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"FovCockpit", L"10", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticABS", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticTC", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticSTM", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticClutch", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticAutomatic", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticSteeringLock", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealCockpitWheel", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"HideDriverInCockpit", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"DrawDistance", L"ShadowDistance", L"200", iniPath.c_str());
        WritePrivateProfileStringW(L"DrawDistance", L"WorldDistance", L"800", iniPath.c_str());
        WritePrivateProfileStringW(L"DrawDistance", L"TrafficDistance", L"300", iniPath.c_str());
        WritePrivateProfileStringW(L"DrawDistance", L"RacerDistance", L"150", iniPath.c_str());
        // per-car overrides: old two-field format (ABS off, TC on, STM auto) and the three-field format
        const ShiftPicks sp = pickShiftCars();
        const fh2::CarInfo* ov = sp.ov;
        if (ov) WritePrivateProfileStringW(L"Cars", std::to_wstring(ov->id).c_str(), L"off,on", carsPath.c_str());
        const fh2::CarInfo* ov3 = sp.ov3;
        if (ov3) WritePrivateProfileStringW(L"Cars", std::to_wstring(ov3->id).c_str(), L"auto,off,on", carsPath.c_str());
        // 4-field overrides: a clutch-yes car forced to "no clutch", a clutch-no car forced to "has a clutch"
        const fh2::CarInfo* cy = sp.cyOv;
        const fh2::CarInfo* cn = sp.cnOv;
        if (cy) WritePrivateProfileStringW(L"Cars", std::to_wstring(cy->id).c_str(), L"auto,auto,auto,off", carsPath.c_str());
        if (cn) WritePrivateProfileStringW(L"Cars", std::to_wstring(cn->id).c_str(), L"auto,auto,auto,on", carsPath.c_str());
        // automatic shifting: a manual car forced to automatic, an automatic-only car with an explicit clutch "on"
        if (sp.manualAuto) WritePrivateProfileStringW(L"Cars", std::to_wstring(sp.manualAuto->id).c_str(), L"auto,auto,auto,automatic", carsPath.c_str());
        if (sp.atOn) WritePrivateProfileStringW(L"Cars", std::to_wstring(sp.atOn->id).c_str(), L"auto,auto,auto,on", carsPath.c_str());
    }
    struct RestoreIni {
        std::wstring p, cars;
        ~RestoreIni() {
            WritePrivateProfileStringW(L"Wheel", L"TestMode", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"FovCockpit", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticABS", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticTC", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticSTM", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticClutch", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticAutomatic", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticSteeringLock", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealCockpitWheel", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"HideDriverInCockpit", L"0", p.c_str());
            for (const wchar_t* k : {L"ShadowDistance", L"WorldDistance", L"TrafficDistance", L"RacerDistance"})
                WritePrivateProfileStringW(L"DrawDistance", k, L"0", p.c_str());
            DeleteFileW(cars.c_str());
        }
    } restoreIni{iniPath, carsPath};

    const uintptr_t kVt0 = 0x2c5bfc8;
    const uint64_t absOrig = *(uint64_t*)(base + kVt0 + 0xd8), stmOrig = *(uint64_t*)(base + kVt0 + 0xe8),
                   tcOrig = *(uint64_t*)(base + kVt0 + 0xf0);
    const uint64_t clutchOrig = *(uint64_t*)(base + kVt0 + 0xf8);
    if (clutchOrig != (uint64_t)(base + 0xfbcb70)) { printf("clutch getter slot does not hold the expected pointer\n"); return 2; }

    const uint64_t carUpdateSlotOrig = *(uint64_t*)(base + 0x2c5cc00);
    HMODULE plugin = LoadLibraryW(argv[2]);
    if (!plugin) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }

    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // wait for the patch
    bool patched = false;
    for (int i = 0; i < 150 && !patched; ++i) { Sleep(100); patched = memcmp(base + 0x103832A, original, 5) != 0; }
    CHECK(patched, "plugin patched the yaw input call site");
    CHECK(base[0x103832A] == 0xE8, "call opcode preserved");

    // First packet sets the (identity) centre, then a real pose.
    sendPose(s, 0, 0, 0, 0, 0, 0); Sleep(50);
    sendPose(s, 4.0, 2.0, -3.0, 30.0, -10.0, 5.0); Sleep(150);

    g_carVtbl[0x70 / 8] = (void*)&carSlot14;
    FakeCar car; car.vtbl = g_carVtbl;
    EvalFn eval = (EvalFn)(base + 0x1037DF0);

    printf("cockpit yaw layer (HeadTrackingYaw -> CameraSpaceYPROffset, Param.x=12):\n");
    Layer rot = makeLayer(31, 4, 12.0f);
    eval(&rot, &car, nullptr, 0.016f);
    CHECK(std::fabs(f(rot, 0) - 1.0f) < 1e-4f, "input forced to 1.0 (got %f)", f(rot, 0));
    CHECK(std::fabs(f(rot, 0xE0) - 30.0f) < 1e-3f && std::fabs(f(rot, 0xE4) - 10.0f) < 1e-3f && std::fabs(f(rot, 0xE8) - 5.0f) < 1e-3f,
          "Param = yaw/pitch/roll (%f %f %f)", f(rot, 0xE0), f(rot, 0xE4), f(rot, 0xE8));
    CHECK(i32(rot, 0xAC) == 1, "mapping set to Linear");
    CHECK(f(rot, 0x58) == -2.0f && f(rot, 0x5C) == 2.0f, "clamp widened");

    printf("cockpit X layer (HeadTrackingX -> CarSpaceXYZOffset, Param.x=0.03):\n");
    Layer pos = makeLayer(28, 2, 0.03f);
    eval(&pos, &car, nullptr, 0.016f);
    CHECK(std::fabs(f(pos, 0) - 1.0f) < 1e-6f, "input forced to 1.0 (got %f)", f(pos, 0));
    CHECK(std::fabs(f(pos, 0xE0) - 0.04f) < 1e-5f && std::fabs(f(pos, 0xE4) - 0.02f) < 1e-5f && std::fabs(f(pos, 0xE8) + 0.03f) < 1e-5f,
          "Param = x/y/z metres (%f %f %f)", f(pos, 0xE0), f(pos, 0xE4), f(pos, 0xE8));

    printf("layers that must stay neutral:\n");
    Layer y = makeLayer(29, 2, 0.0f);
    *(float*)(y.b + 0xE4) = 1.0f;
    eval(&y, &car, nullptr, 0.016f);
    CHECK(f(y, 0) == 0.0f && f(y, 0xE4) == 1.0f, "HeadTrackingY layer gets 0 and is untouched");
    Layer pitch = makeLayer(32, 4, 5.0f);
    eval(&pitch, &car, nullptr, 0.016f);
    CHECK(f(pitch, 0) == 0.0f, "HeadTrackingPitch layer gets 0");
    Layer hood = makeLayer(31, 4, 15.0f);
    eval(&hood, &car, nullptr, 0.016f);
    CHECK(f(hood, 0) == 0.0f && f(hood, 0xE0) == 15.0f, "hood/bumper yaw layer untouched with CockpitOnly=1");
    Layer rot2 = makeLayer(31, 4, 12.0f);
    eval(&rot, &car, nullptr, 0.016f);
    eval(&rot2, &car, nullptr, 0.016f);
    CHECK(f(rot, 0) == 1.0f && f(rot2, 0) == 0.0f, "second cockpit rotation layer in the same frame is neutral");
    Sleep(20);

    printf("updates and staleness:\n");
    sendPose(s, 0, 0, 0, -45.0, 0, 0); Sleep(100);
    eval(&rot, &car, nullptr, 0.016f);
    CHECK(std::fabs(f(rot, 0xE0) + 45.0f) < 1e-3f, "new pose arrives (yaw %f)", f(rot, 0xE0));
    Sleep(900);   // > StaleTimeoutMs
    eval(&rot, &car, nullptr, 0.016f);
    CHECK(f(rot, 0) == 0.0f, "stale tracker -> input 0, camera returns to rest");

    printf("FreeTrack shared memory (Beam / TrackIR emulation):\n");
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"FT_SharedMem");
    float* ft = (float*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    uint32_t* id = (uint32_t*)ft;
    // layout: DataID, CamW, CamH, Yaw, Pitch, Roll, X, Y, Z (radians, mm)
    for (int i = 0; i < 75; ++i) {
        ft[3] = -0.5f; ft[4] = 0.2f; ft[5] = 0.1f; ft[6] = 50.0f; ft[7] = -20.0f; ft[8] = 30.0f;
        *id += 1;
        sendPose(s, 0, 0, 0, 5.0, 0, 0);     // UDP keeps sending; FreeTrack must win
        Sleep(20);
    }
    eval(&rot, &car, nullptr, 0.016f);
    eval(&pos, &car, nullptr, 0.016f);
    CHECK(f(rot, 0) == 1.0f && std::fabs(f(rot, 0xE0) - 28.6479f) < 0.01f, "FreeTrack yaw -0.5 rad -> +28.65 deg (got %f)", f(rot, 0xE0));
    CHECK(std::fabs(f(rot, 0xE4) - 11.4592f) < 0.01f && std::fabs(f(rot, 0xE8) - 5.7296f) < 0.01f,
          "FreeTrack pitch/roll converted (%f %f)", f(rot, 0xE4), f(rot, 0xE8));
    CHECK(std::fabs(f(pos, 0xE0) - 0.05f) < 1e-4f && std::fabs(f(pos, 0xE4) + 0.02f) < 1e-4f && std::fabs(f(pos, 0xE8) - 0.03f) < 1e-4f,
          "FreeTrack mm -> metres (%f %f %f)", f(pos, 0xE0), f(pos, 0xE4), f(pos, 0xE8));

    wheelTests(base, origFrame, createString);
    ffbTests(base, plugin);
    assistTests(base, absOrig, stmOrig, tcOrig, clutchOrig);
    fovTests(base);
    cockpitTests(base, plugin, carUpdateSlotOrig, logPath);
    drawDistTests(base, plugin);

    printf(failures ? "\n%d check(s) FAILED\n" : "\nall integration checks passed\n", failures);
    return failures ? 1 : 0;
}

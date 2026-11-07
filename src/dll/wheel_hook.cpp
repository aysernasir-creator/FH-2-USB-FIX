// SPDX-License-Identifier: MIT
// Hooks that present the virtual wheel to Forza Horizon 2.
#include "wheel_hook.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "assist_hook.h"
#include "cockpit_hook.h"
#include "log.h"
#include "patch_util.h"
#include "wheel_com.h"
#include "wheel_device.h"

// ---- forwarding thunks for the statics proxy ---------------------------------------------------
// fwd_thunk_N: swap `this` for the real object and jump into the real vtable slot N. Other
// registers and the stack are untouched, so any signature forwards correctly.
extern "C" const void* const fwd_thunk_table[64];
asm(".text\n.intel_syntax noprefix\n"
    ".macro FWD n\n"
    ".globl fwd_thunk_\\n\n"
    "fwd_thunk_\\n:\n"
    "mov rcx, qword ptr [rcx+8]\n"
    "mov rax, qword ptr [rcx]\n"
    "jmp qword ptr [rax+8*\\n]\n"
    ".endm\n"
    ".irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,"
    "32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63\n"
    "FWD \\n\n"
    ".endr\n"
    ".data\n.globl fwd_thunk_table\nfwd_thunk_table:\n"
    ".irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,"
    "32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63\n"
    ".quad fwd_thunk_\\n\n"
    ".endr\n"
    ".text\n"
    ".att_syntax prefix\n");

namespace fh2 {

namespace {

constexpr uintptr_t kIatGetActivation = 0x1dd1330;
constexpr uintptr_t kIatUnboundHint   = 0x2c42474;
constexpr uintptr_t kControllerMgrRva = 0x365d7c8;
constexpr uintptr_t kRescanRva        = 0x1073120;
constexpr uintptr_t kUpdateRva        = 0x1075490;
constexpr uintptr_t kFrameSiteBytes   = 0x22688c;    // 12 bytes, call at +7 (rva 0x226893)
constexpr uintptr_t kFrameCallSite    = 0x226893;
constexpr uintptr_t kFfbUpdateRva     = 0x10743c0;   // UpdateFFB(slot, const float in[3])
constexpr uintptr_t kFfbSiteBytes     = 0x2fe12d;    // 12 bytes, call at +7 (rva 0x2fe134)
constexpr uintptr_t kFfbCallSite      = 0x2fe134;
const uint8_t kFfbSiteExpected[12] = {0x48, 0x8D, 0x55, 0xA8, 0x48, 0x8B, 0xCF, 0xE8, 0x87, 0x62, 0xD7, 0x00};
const uint8_t kFrameSiteExpected[12] = {0x48, 0x8B, 0x0D, 0x35, 0x6F, 0x43, 0x03, 0xE8, 0xF8, 0xEB, 0xE4, 0x00};

constexpr GUID mkGuid(unsigned long a, unsigned short b, unsigned short c, unsigned char d0, unsigned char d1,
                      unsigned char d2, unsigned char d3, unsigned char d4, unsigned char d5, unsigned char d6,
                      unsigned char d7) {
    return GUID{a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}};
}
constexpr GUID kIidUnknown  = mkGuid(0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
constexpr GUID kIidInspect  = mkGuid(0xAF86E2E0, 0xB12D, 0x4C6A, 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90);
constexpr GUID kIidAgile    = mkGuid(0x94EA2B94, 0xE9CC, 0x49E0, 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90);
constexpr GUID kIidStatics  = mkGuid(0x7400624f, 0x144b, 0x47fc, 0x9f, 0x3b, 0x1d, 0xca, 0xfb, 0x48, 0x28, 0x73);
constexpr GUID kIidWheel    = mkGuid(0x646979cf, 0x6b71, 0x4e96, 0x8d, 0xf9, 0x59, 0xe3, 0x98, 0xd7, 0x42, 0x0c);

constexpr HRESULT kEBounds = (HRESULT)0x8000000BL;

bool same(const GUID* a, const GUID& b) { return a && std::memcmp(a, &b, sizeof(GUID)) == 0; }
bool isBaseIid(const GUID* r) { return same(r, kIidUnknown) || same(r, kIidInspect) || same(r, kIidAgile); }

uintptr_t   g_base = 0;
WheelConfig g_wc;
bool        g_diag = false;
std::atomic<bool> g_rescanPending{false};
std::atomic<bool> g_azimuthHasWheel{false};

typedef HRESULT (*GetActivationFn)(const wchar_t*, const GUID*, void**);
GetActivationFn g_origGetActivation = nullptr;

// ---- raw COM helpers -------------------------------------------------------------------------
void** vtblOf(void* obj) { return *reinterpret_cast<void***>(obj); }
ULONG comAddRef(void* o) { return reinterpret_cast<ULONG (*)(void*)>(vtblOf(o)[1])(o); }
ULONG comRelease(void* o) { return reinterpret_cast<ULONG (*)(void*)>(vtblOf(o)[2])(o); }
HRESULT comQI(void* o, const GUID& iid, void** out) {
    return reinterpret_cast<HRESULT (*)(void*, const GUID*, void**)>(vtblOf(o)[0])(o, &iid, out);
}
HRESULT viewSize(void* v, uint32_t* n) {
    return reinterpret_cast<HRESULT (*)(void*, uint32_t*)>(vtblOf(v)[7])(v, n);
}
HRESULT viewGetAt(void* v, uint32_t i, void** out) {
    return reinterpret_cast<HRESULT (*)(void*, uint32_t, void**)>(vtblOf(v)[6])(v, i, out);
}

constexpr GUID kIidUserStatics = mkGuid(0xb4f23a93, 0x7fc6, 0x4a50, 0xb1, 0x53, 0x95, 0x00, 0x16, 0x38, 0x9e, 0x50);

// The signed-in system user (Windows.Xbox.System.User statics, get_Users[0]); AddRef'd, or nullptr.
void* systemUser() {
    static std::atomic<bool> failLogged{false};
    HRESULT hr = (HRESULT)0x80004005L;
    void* statics = nullptr;
    void* view = nullptr;
    void* user = nullptr;
    if (g_origGetActivation) {
        hr = g_origGetActivation(L"Windows.Xbox.System.User", &kIidUserStatics, &statics);
        if (SUCCEEDED(hr) && !statics) hr = (HRESULT)0x80004003L;
    }
    if (SUCCEEDED(hr)) {
        hr = reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(statics)[9])(statics, &view);
        if (SUCCEEDED(hr) && !view) hr = (HRESULT)0x80004003L;
    }
    if (SUCCEEDED(hr)) {
        uint32_t n = 0;
        hr = viewSize(view, &n);
        if (SUCCEEDED(hr) && n == 0) hr = kEBounds;
        if (SUCCEEDED(hr)) {
            hr = viewGetAt(view, 0, &user);
            if (SUCCEEDED(hr) && !user) hr = (HRESULT)0x80004003L;
        }
    }
    if (view) comRelease(view);
    if (statics) comRelease(statics);
    if (FAILED(hr)) {
        if (!failLogged.exchange(true)) LOG("wheel: no system user available (0x%08lx)", (unsigned long)hr);
        return nullptr;
    }
    return user;
}

struct SeenLog {
    GUID g[16];
    int n = 0;
    void note(const GUID* id, const char* what) {
        for (int i = 0; i < n; ++i) if (std::memcmp(&g[i], id, sizeof(GUID)) == 0) return;
        if (n < 16) g[n++] = *id;
        LOG("wheel: %s QI for unknown IID {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", what, id->Data1,
            id->Data2, id->Data3, id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3], id->Data4[4],
            id->Data4[5], id->Data4[6], id->Data4[7]);
    }
};
SeenLog g_listSeen;

// ---- ListProxy -----------------------------------------------------------------------------
// The index map is built once per get_Controllers call: the real indices that are exposed (in order),
// then the fake wheel at the end. Real controllers that answer IWheel are left out when
// OverrideAzimuthWheel is on.
struct ListProxy { const void* const* vtbl; void* realView; LONG refs; bool withFake; std::vector<uint32_t> map; };

uint32_t proxyCount(ListProxy* p) { return (uint32_t)p->map.size() + (p->withFake ? 1 : 0); }

HRESULT lp_QI(void* self, const GUID* riid, void** ppv) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (!ppv) return (HRESULT)0x80004003L;
    if (isBaseIid(riid)) { InterlockedIncrement(&p->refs); *ppv = p; return S_OK; }
    if (!riid) { *ppv = nullptr; return (HRESULT)0x80004003L; }
    g_listSeen.note(riid, "view");
    return comQI(p->realView, *riid, ppv);
}
ULONG lp_AddRef(void* self) { return (ULONG)InterlockedIncrement(&static_cast<ListProxy*>(self)->refs); }
ULONG lp_Release(void* self) {
    ListProxy* p = static_cast<ListProxy*>(self);
    LONG n = InterlockedDecrement(&p->refs);
    if (n == 0) { comRelease(p->realView); delete p; }
    return (ULONG)n;
}
template <int N> HRESULT lpForward(void* self, void* a, void* b) {   // slots 3..5: (this, a, b)
    ListProxy* p = static_cast<ListProxy*>(self);
    return reinterpret_cast<HRESULT (*)(void*, void*, void*)>(vtblOf(p->realView)[N])(p->realView, a, b);
}
HRESULT lp_GetAt(void* self, uint32_t i, void** out) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (!out) return S_OK;
    if (i < p->map.size()) return viewGetAt(p->realView, p->map[i], out);
    if (i == p->map.size() && p->withFake) {
        void* c = VirtualWheel_Controller();
        comAddRef(c);
        *out = c;
        return S_OK;
    }
    *out = nullptr;
    return kEBounds;
}
HRESULT lp_Size(void* self, uint32_t* out) {
    if (out) *out = proxyCount(static_cast<ListProxy*>(self));
    return S_OK;
}
HRESULT lp_IndexOf(void* self, void* item, uint32_t* idx, uint8_t* found) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (p->withFake && item == VirtualWheel_Controller()) {
        if (idx) *idx = (uint32_t)p->map.size();
        if (found) *found = 1;
        return S_OK;
    }
    uint32_t ri = 0;
    uint8_t f = 0;
    HRESULT hr = reinterpret_cast<HRESULT (*)(void*, void*, uint32_t*, uint8_t*)>(vtblOf(p->realView)[8])(
        p->realView, item, &ri, &f);
    if (FAILED(hr)) return hr;
    uint32_t pi = 0;
    bool ok = false;
    if (f) for (uint32_t k = 0; k < p->map.size(); ++k) if (p->map[k] == ri) { pi = k; ok = true; break; }
    if (idx) *idx = ok ? pi : 0;
    if (found) *found = ok ? 1 : 0;
    return hr;
}
HRESULT lp_GetMany(void* self, uint32_t start, uint32_t cap, void** items, uint32_t* actual) {
    ListProxy* p = static_cast<ListProxy*>(self);
    uint32_t size = proxyCount(p);
    uint32_t got = 0;
    while (items && start + got < size && got < cap) {
        void* e = nullptr;
        if (FAILED(lp_GetAt(self, start + got, &e))) break;
        items[got++] = e;
    }
    if (actual) *actual = got;
    return S_OK;
}
const void* const g_listVtbl[10] = {
    reinterpret_cast<const void*>(&lp_QI), reinterpret_cast<const void*>(&lp_AddRef),
    reinterpret_cast<const void*>(&lp_Release), reinterpret_cast<const void*>(&lpForward<3>),
    reinterpret_cast<const void*>(&lpForward<4>), reinterpret_cast<const void*>(&lpForward<5>),
    reinterpret_cast<const void*>(&lp_GetAt), reinterpret_cast<const void*>(&lp_Size),
    reinterpret_cast<const void*>(&lp_IndexOf), reinterpret_cast<const void*>(&lp_GetMany),
};

// ---- StaticsProxy -----------------------------------------------------------------------------------
struct StaticsProxy { const void* const* vtbl; void* real; LONG refs; };
extern const void* g_staticsVtbl[64];
StaticsProxy g_proxy = {g_staticsVtbl, nullptr, 0};
CRITICAL_SECTION g_proxyCs;

HRESULT sp_QI(void* self, const GUID* riid, void** ppv) {
    StaticsProxy* p = static_cast<StaticsProxy*>(self);
    if (!ppv) return (HRESULT)0x80004003L;
    if (isBaseIid(riid) || same(riid, kIidStatics)) {
        InterlockedIncrement(&p->refs);
        *ppv = p;
        return S_OK;
    }
    if (!riid) { *ppv = nullptr; return (HRESULT)0x80004003L; }
    return comQI(p->real, *riid, ppv);
}
ULONG sp_AddRef(void* self) { return (ULONG)InterlockedIncrement(&static_cast<StaticsProxy*>(self)->refs); }
ULONG sp_Release(void* self) { return (ULONG)InterlockedDecrement(&static_cast<StaticsProxy*>(self)->refs); }

HRESULT sp_GetControllers(void* self, void** out) {
    StaticsProxy* p = static_cast<StaticsProxy*>(self);
    void* real = p->real;
    HRESULT hr = reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(real)[6])(real, out);
    if (FAILED(hr) || !out || !*out) return hr;
    void* view = *out;

    if (g_azimuthHasWheel.load()) return hr;
    uint32_t n = 0;
    if (FAILED(viewSize(view, &n))) n = 0;
    std::vector<uint32_t> keep;
    bool hidAny = false;
    for (uint32_t i = 0; i < n; ++i) {
        void* c = nullptr;
        if (FAILED(viewGetAt(view, i, &c)) || !c) { keep.push_back(i); continue; }
        void* w = nullptr;
        bool isWheel = SUCCEEDED(comQI(c, kIidWheel, &w)) && w;
        if (w) comRelease(w);
        comRelease(c);
        if (!isWheel) { keep.push_back(i); continue; }
        if (!g_wc.overrideAzimuthWheel) {
            g_azimuthHasWheel = true;
            LOG("wheel: Azimuth already provides a wheel; virtual wheel disabled");
            return hr;
        }
        hidAny = true;
    }
    if (hidAny) {
        static std::atomic<bool> hiddenLogged{false};
        if (!hiddenLogged.exchange(true))
            LOG("wheel: Azimuth provides a wheel; hidden, using the plugin's wheel instead");
    }

    if (g_wc.userMode == WheelUserMode::Auto) {
        void* user = nullptr;
        if (!keep.empty()) {
            void* c = nullptr;
            if (SUCCEEDED(viewGetAt(view, keep[0], &c)) && c) {
                HRESULT uh = reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(c)[8])(c, &user);
                if (FAILED(uh)) user = nullptr;
                comRelease(c);
            }
        }
        if (!user) {
            user = systemUser();
            static std::atomic<bool> fallbackLogged{false};
            if (user && !fallbackLogged.exchange(true))
                LOG("wheel: no Xbox controller user, using the signed-in system user");
        }
        if (user) VirtualWheel_SetUser(user);
    }
    static std::atomic<bool> userLogged{false};
    if (!userLogged.exchange(true)) {
        // Read back through the fake's own get_User so the log shows what the game will see.
        void* u = nullptr;
        void* fake = VirtualWheel_Controller();
        reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(fake)[8])(fake, &u);
        LOG("wheel: user %p (mode %s)", u, g_wc.userMode == WheelUserMode::Auto ? "auto" : "none");
        if (u) comRelease(u);
    }

    ListProxy* lp = new (std::nothrow) ListProxy{g_listVtbl, view, 1, true, std::move(keep)};
    if (!lp) return hr;
    static std::atomic<bool> addedLogged{false};
    if (!addedLogged.exchange(true)) LOG("wheel: virtual wheel added to the controller list (index %u)", (unsigned)lp->map.size());
    *out = lp;
    return hr;
}

const void* g_staticsVtbl[64];   // filled in installWheel
// ---- activation factory hook ---------------------------------------------------------------------
HRESULT hookGetActivationFactory(const wchar_t* name, const GUID* iid, void** out) {
    HRESULT hr = g_origGetActivation(name, iid, out);
    if (SUCCEEDED(hr) && out && *out && name && iid && wcscmp(name, L"Windows.Xbox.Input.Controller") == 0 &&
        std::memcmp(iid, &kIidStatics, sizeof(GUID)) == 0) {
        void* real = *out;
        EnterCriticalSection(&g_proxyCs);
        if (g_proxy.real != real) {
            comAddRef(real);
            void* old = g_proxy.real;
            g_proxy.real = real;
            if (old) comRelease(old);
        }
        LeaveCriticalSection(&g_proxyCs);
        comRelease(real);                      // the proxy holds its own reference
        InterlockedIncrement(&g_proxy.refs);
        *out = &g_proxy;
    }
    return hr;
}

// ---- per-frame hook -------------------------------------------------------------------------------
void hookControllerUpdate(void* mgr, float dt) {
    if (g_rescanPending.load() && mgr) {
        g_rescanPending = false;
        LOG("wheel: rescanning controllers");
        reinterpret_cast<void (*)(void*)>(g_base + kRescanRva)(mgr);
    }
    reinterpret_cast<void (*)(void*, float)>(g_base + kUpdateRva)(mgr, dt);
}

// ---- UpdateFFB hook (game thread, once per frame) --------------------------------------------------
// The call-site thunk passes the caller's rsi as carElem: it points at the vector element holding the
// current car object (the call only happens for the local player's car).
// Captures the inputs and the driver's parameters, then runs the original. in[] is never modified.
std::atomic<bool> g_wheelActive{false};   // the virtual wheel is installed: feed the FFB capture
std::atomic<bool> g_trackCar{false};      // a feature needs the player's car (assists, steering lock, cockpit wheel, hiding the driver)

extern "C" void hookFfbUpdate(uint8_t* slot, const float* in, void** carElem) {
    if (g_trackCar.load(std::memory_order_relaxed) && readable(carElem, sizeof(void*))) {
        const uint8_t* car = static_cast<const uint8_t*>(*carElem);
        if (readable(car, 0x11c)) {
            const uint32_t id = *reinterpret_cast<const uint32_t*>(car + 0x118);
            AssistHook_SetPlayerCar(id);
            SteerLock_SetPlayerCar(id);
            CockpitHook_SetPlayerCar(const_cast<uint8_t*>(car));
        }
    }
    if (g_wheelActive.load(std::memory_order_relaxed) && slot && in) {
        GameFfbInput g;
        g.rawTorque = in[0]; g.speed = in[1]; g.slip = in[2];
        const float* d = *reinterpret_cast<float* const*>(slot + 0x10c0);
        if (d) {
            g.gain = d[0];
            g.dynScale = d[0x28 / 4]; g.dynMax = d[0x2c / 4]; g.springParam = d[0x48 / 4];
            g.slip0 = d[0x54 / 4]; g.slip1 = d[0x58 / 4]; g.slipScale0 = d[0x5c / 4]; g.slipScale1 = d[0x60 / 4];
            g.speed0 = d[0x64 / 4]; g.speed1 = d[0x68 / 4]; g.speedScale0 = d[0x6c / 4]; g.speedScale1 = d[0x70 / 4];
            // Front slip: load-weighted average of slip(0), slip(1) from the car's physics object,
            // computed the same way the game does for the rear (vtable +0x610 load, +0x578 slip).
            if (readable(carElem, sizeof(void*))) {
                const uint8_t* car = static_cast<const uint8_t*>(*carElem);
                if (readable(car, 0x4c10)) {
                    const uint8_t* phys = *reinterpret_cast<const uint8_t* const*>(car + 0x4c08);
                    if (readable(phys, 8)) {
                        const uint8_t* vt = *reinterpret_cast<const uint8_t* const*>(phys);
                        if (readable(vt, 0x618)) {
                            using Fn = float (*)(const void*, int);
                            Fn load = *reinterpret_cast<const Fn*>(vt + 0x610);
                            Fn slip = *reinterpret_cast<const Fn*>(vt + 0x578);
                            float l0 = load(phys, 0), l1 = load(phys, 1), s0 = slip(phys, 0), s1 = slip(phys, 1);
                            float sum = l0 + l1;
                            float fsl = (l0 * s0 + l1 * s1) / sum;
                            if (std::isfinite(l0) && std::isfinite(l1) && std::isfinite(s0) && std::isfinite(s1) &&
                                sum > 1e-6f && std::isfinite(fsl)) {
                                g.frontSlip = fsl; g.haveFront = true;
                            }
                        }
                    }
                }
            }
            VirtualWheel_SetGameFfbInput(g);
        }
    }
    reinterpret_cast<void (*)(uint8_t*, const float*)>(g_base + kFfbUpdateRva)(slot, in);
}

// ---- diagnostics ----------------------------------------------------------------------------------
DWORD WINAPI diagThread(void*) {
    for (;;) {
        Sleep(2000);
        WheelInput in;
        bool have = WheelDevice_Read(in);
        char btn[40]; size_t n = 0;
        auto add = [&](bool b, const char* s) { if (b) n += snprintf(btn + n, sizeof(btn) - n, "%s", s); };
        add(in.a, "A"); add(in.b, "B"); add(in.x, "X"); add(in.y, "Y"); add(in.view, "V"); add(in.menu, "M");
        add(in.lb, "l"); add(in.rb, "r"); add(in.up, "^"); add(in.down, "v"); add(in.left, "<"); add(in.right, ">");
        if (n == 0) { btn[0] = '-'; btn[1] = 0; }
        char vars[1024];
        VirtualWheel_FormatVars(vars, sizeof(vars));
        LOG("wheel diag: steer %.3f thr %.2f brk %.2f clu %.2f btn %s gear %d hb %d | game rot %.0f | vars: %s",
            in.steering, in.throttle, in.brake, in.clutch, btn, in.gear, in.handbrake ? 1 : 0, VirtualWheel_GameRotation(), vars);
        if (!have) continue;
        char pressed[256]; size_t pn = 0; pressed[0] = 0;
        for (int i = 0; i < 128 && pn < sizeof(pressed) - 1; ++i)
            if (in.rawButtons[i / 32] & (1u << (i % 32)))
                pn += snprintf(pressed + pn, sizeof(pressed) - pn, pn ? ",%d" : "%d", i);
        if (pn >= sizeof(pressed)) pn = sizeof(pressed) - 1;
        LOG("wheel diag raw: X %ld Y %ld Z %ld RX %ld RY %ld RZ %ld S0 %ld S1 %ld buttons %s",
            (long)in.raw[0], (long)in.raw[1], (long)in.raw[2], (long)in.raw[3], (long)in.raw[4],
            (long)in.raw[5], (long)in.raw[6], (long)in.raw[7], pn ? pressed : "none");
        FfbStatus fs;
        if (WheelDevice_FfbStatus(fs))
        {
            char game[96];
            if (fs.haveGame)
                snprintf(game, sizeof(game), " | game raw %.3f speed %.1f slip %.3f gain %.2f", fs.game.rawTorque,
                         fs.game.speed, fs.game.slip, fs.game.gain);
            else
                snprintf(game, sizeof(game), " | game input none");
            char front[24];
            if (fs.haveGame && fs.game.haveFront) snprintf(front, sizeof(front), "front %.3f", fs.game.frontSlip);
            else snprintf(front, sizeof(front), "front none");
            LOG("wheel diag ffb: %s F %.3f (torque %.3f spring %.3f damp %.3f rumble %.3f menu %.3f) angle %.3f vel %.2f out %ld slip %.3f align %.3f %s rumbleAmp L %.3f R %.3f%s lock %.0f/%.0f k %.2f",
                fs.stale ? "stale" : fs.race ? "race" : "menu", fs.terms.total, fs.terms.torque, fs.terms.spring,
                fs.terms.damping, fs.terms.rumble, fs.terms.menu, fs.angle, fs.velocity, fs.mag, fs.terms.slip,
                fs.terms.align, front, fs.leftAmp, fs.rightAmp, game, fs.lockDeg, fs.lockLim, fs.lockK);
        }
    }
}

} // namespace

static bool installWheel(uintptr_t base, const Config& cfg) {
    g_wc = cfg.wheel;
    g_diag = cfg.diagnostics;
    std::memcpy(g_staticsVtbl, fwd_thunk_table, sizeof(g_staticsVtbl));
    g_staticsVtbl[0] = reinterpret_cast<const void*>(&sp_QI);
    g_staticsVtbl[1] = reinterpret_cast<const void*>(&sp_AddRef);
    g_staticsVtbl[2] = reinterpret_cast<const void*>(&sp_Release);
    g_staticsVtbl[6] = reinterpret_cast<const void*>(&sp_GetControllers);
    InitializeCriticalSection(&g_proxyCs);
    VirtualWheel_Configure(cfg);
    VirtualWheel_SetGameBase(base);
    if (!WheelDevice_Start(cfg.wheel, cfg.diagnostics)) { LOG("wheel: no usable device; wheel hooks not installed"); return false; }

    // IAT hook: wait until Azimuth has bound the slot (file value is an unbound RVA hint).
    void** slot = reinterpret_cast<void**>(base + kIatGetActivation);
    void* orig = nullptr;
    for (int i = 0; i < 15000; ++i) {
        orig = *slot;
        if (orig && reinterpret_cast<uintptr_t>(orig) != kIatUnboundHint) break;
        orig = nullptr;
        Sleep(1);
    }
    if (!orig) { LOG("wheel: activation import never bound by Azimuth; wheel hooks not installed"); return false; }
    g_origGetActivation = reinterpret_cast<GetActivationFn>(orig);
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        LOG("wheel: VirtualProtect on the import slot failed %lu", GetLastError());
        return false;
    }
    *slot = reinterpret_cast<void*>(&hookGetActivationFactory);
    VirtualProtect(slot, sizeof(void*), old, &old);
    LOG("wheel: activation hook installed");

    // Per-frame hook (controller rescan).
    const uint8_t* site = reinterpret_cast<const uint8_t*>(base + kFrameSiteBytes);
    if (std::memcmp(site, kFrameSiteExpected, sizeof(kFrameSiteExpected)) != 0) {
        LOG("wheel: frame hook site mismatch; rescan disabled");
    } else {
        void* mgr = *reinterpret_cast<void**>(base + kControllerMgrRva);
        g_rescanPending = cfg.wheel.forceRescan && mgr != nullptr;
        LOG("wheel: controller manager %p, rescan %s", mgr,
            g_rescanPending.load() ? "scheduled" : (cfg.wheel.forceRescan ? "not needed yet" : "off"));
        uint8_t* thunk = static_cast<uint8_t*>(allocNear(base + kFrameCallSite, 4096));
        if (!thunk) {
            LOG("wheel: could not allocate frame hook memory; rescan disabled");
            g_rescanPending = false;
        } else if (!patchCallRel32(base + kFrameCallSite, reinterpret_cast<void*>(&hookControllerUpdate), thunk)) {
            LOG("wheel: frame hook failed; rescan disabled");
            g_rescanPending = false;
        } else {
            DWORD o;
            VirtualProtect(thunk, 4096, PAGE_EXECUTE_READ, &o);
            LOG("wheel: frame hook installed (thunk %p)", (void*)thunk);
        }
    }

    return true;
}

// Game FFB input hook (UpdateFFB call site): the plugin sees what the car computes (wheel FFB) and which
// car the local player drives (realistic assists). The stub passes rsi as the third argument.
static void installFfbSite(uintptr_t base, bool wheelFfb, bool track) {
    const uint8_t* fsite = reinterpret_cast<const uint8_t*>(base + kFfbSiteBytes);
    if (std::memcmp(fsite, kFfbSiteExpected, sizeof(kFfbSiteExpected)) != 0) {
        LOG("wheel ffb: FFB input hook site mismatch; using game variables only%s",
            track ? " (car tracking inactive)" : "");
        return;
    }
    uint8_t* fthunk = static_cast<uint8_t*>(allocNear(base + kFfbCallSite, 4096));
    if (!fthunk || !patchCallRel32(base + kFfbCallSite, reinterpret_cast<void*>(&hookFfbUpdate), fthunk, true)) {
        LOG("wheel ffb: FFB input hook failed; using game variables only%s", track ? " (car tracking inactive)" : "");
        return;
    }
    DWORD o;
    VirtualProtect(fthunk, 4096, PAGE_EXECUTE_READ, &o);
    g_trackCar = track;
    LOG("wheel ffb: game FFB input hook installed (wheel ffb %d, car tracking %d)", wheelFfb, track);
}

void WheelHook_Install(uintptr_t base, const Config& cfg) {
    g_base = base;
    const bool assists = cfg.sim.realisticAbs || cfg.sim.realisticTc || cfg.sim.realisticStm || cfg.sim.realisticClutch ||
                         cfg.sim.realisticAutomatic || cfg.sim.realisticSteeringLock ||
                         cfg.sim.realCockpitWheel || cfg.sim.hideDriverInCockpit;
    bool wheelOk = false;
    if (!cfg.wheel.enabled) {
        LOG("wheel: disabled in settings");
    } else {
        wheelOk = installWheel(base, cfg);
    }
    g_wheelActive = wheelOk && cfg.wheel.ffbEnabled;
    if (g_wheelActive || assists) installFfbSite(base, g_wheelActive, assists);
    if (wheelOk && cfg.diagnostics)
        if (HANDLE t = CreateThread(nullptr, 0, diagThread, nullptr, 0, nullptr)) CloseHandle(t);
}

} // namespace fh2

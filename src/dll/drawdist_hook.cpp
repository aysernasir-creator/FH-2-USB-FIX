// SPDX-License-Identifier: MIT
// Draw distance injection (no game files are edited):
//  A. track settings (shadows, world objects, props, crowd, grass): the game's TrackSettings
//     deserializer is wrapped and the loaded values are raised afterwards;
//  B. traffic cars: two registry variables (fade distances) in the game's .data, written lazily
//     on the first track-settings load, because the game's static initializers may not have run
//     when the plugin installs;
//  C. racer cars: the CarLODTransDist render setting (parse method and defaults method).
#include "drawdist_hook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "patch_util.h"
#include "log.h"

namespace fh2 {
namespace {

// ---- A. track settings
constexpr uintptr_t kThunkRva        = 0x134af90;   // add rcx,0x8110 ; jmp Serialize
constexpr uintptr_t kThunkJmpRva     = 0x134af97;   // the E9 rel32
constexpr uintptr_t kSerializeRva    = 0x1144320;
constexpr uint8_t   kThunkBytes[12]  = {0x48, 0x81, 0xC1, 0x10, 0x81, 0x00, 0x00, 0xE9, 0x84, 0x93, 0xDF, 0xFF};

constexpr int kOffShadowEnd = 0x234, kOffShadowScreen = 0x5f4, kOffWorld = 0x5e4, kOffProps = 0x32c,
              kOffCrowd = 0x404, kOffCrowdModel = 0x40c, kOffCrowdFade = 0x410,
              kOffGrassEnd = 0x168, kOffGrassStart = 0x164;
constexpr size_t kTrackSettingsSpan = 0x600;

// ---- B. traffic registry variables (float: +0x10 current, +0x14 default, +0x18 min, +0x1c 0, +0x20 max)
struct RegVar { const char* name; uintptr_t rva; float def, max; };
constexpr RegVar kTrafficMin  = {"AI/Traffic/Vehicle/MinFullAlphaDistance", 0x36e8c80, 100.0f, 500.0f};
constexpr RegVar kTrafficMax  = {"AI/Traffic/Vehicle/MaxFullAlphaDistance", 0x36e8d88, 150.0f, 500.0f};
constexpr RegVar kLodMedium   = {"AI/Traffic/VehiclePosition/MaxMediumLodDistance", 0x36e8048, 120.0f, 1000.0f};
constexpr float  kTrafficDefaultMax = 150.0f, kTrafficDefaultMin = 100.0f, kLodMediumDefault = 120.0f;

// ---- C. racer cars
constexpr uintptr_t kCarVtable      = 0x2eb9bc0;    // name getter, parse, defaults
constexpr uintptr_t kNameGetterRva  = 0x1307230;
constexpr uintptr_t kParseRva       = 0x12ffa90;
constexpr uintptr_t kDefaultsRva    = 0x130bb20;
constexpr float     kCarFarthest    = 75.0f;

uintptr_t      g_base = 0;
DrawDistConfig g_cfg;
std::atomic<bool> g_trafficDone{false};
std::atomic<bool> g_carLogged{false};

typedef uint64_t (*SerializeFn)(uint8_t*, void*, void*, void*);
typedef uint64_t (*CarFn)(void*, void*, void*, void*);

template <class T> T& at(uint8_t* p, int off) { return *reinterpret_cast<T*>(p + off); }
void raise(uint8_t* ts, int off, float v) {
    float& f = at<float>(ts, off);
    if (v > f) f = v;
}

// ---------------------------------------------------------------------------
// A
// ---------------------------------------------------------------------------
void applyTrackOverrides(uint8_t* ts) {
    if (!readable(ts, kTrackSettingsSpan)) return;
    const float shadow0 = at<float>(ts, kOffShadowEnd), world0 = at<float>(ts, kOffWorld),
                props0 = at<float>(ts, kOffProps), crowd0 = at<float>(ts, kOffCrowd),
                grass0 = at<float>(ts, kOffGrassEnd);
    if (g_cfg.shadow > 0) { raise(ts, kOffShadowEnd, g_cfg.shadow); raise(ts, kOffShadowScreen, g_cfg.shadow); }
    if (g_cfg.world > 0)  raise(ts, kOffWorld, g_cfg.world);
    if (g_cfg.props > 0)  raise(ts, kOffProps, g_cfg.props);
    if (g_cfg.crowd > 0) {
        raise(ts, kOffCrowd, g_cfg.crowd);
        raise(ts, kOffCrowdModel, g_cfg.crowd * 0.4f);
        raise(ts, kOffCrowdFade, g_cfg.crowd * 0.72f);
    }
    if (g_cfg.grass > 0) {
        raise(ts, kOffGrassEnd, g_cfg.grass);
        raise(ts, kOffGrassStart, g_cfg.grass * 0.75f);
    }
    LOG("drawdist: track settings loaded (shadow %.0f->%.0f m, world %.0f->%.0f m, props %.0f->%.0f m, "
        "crowd %.0f->%.0f m, grass %.0f->%.0f m)",
        shadow0, at<float>(ts, kOffShadowEnd), world0, at<float>(ts, kOffWorld), props0, at<float>(ts, kOffProps),
        crowd0, at<float>(ts, kOffCrowd), grass0, at<float>(ts, kOffGrassEnd));
}

// ---------------------------------------------------------------------------
// B
// ---------------------------------------------------------------------------
// The variable's default and maximum must match what was researched, else it is not the variable we think.
float* verifiedVar(const RegVar& v) {
    uint8_t* p = reinterpret_cast<uint8_t*>(g_base + v.rva);
    if (!readable(p, 0x24) || at<float>(p, 0x14) != v.def || at<float>(p, 0x20) != v.max) return nullptr;
    return &at<float>(p, 0x10);
}

void applyTrafficOverrides() {
    if (!(g_cfg.traffic > 0)) return;
    const float maxD = std::max(kTrafficDefaultMax, g_cfg.traffic);
    const float minD = std::max(kTrafficDefaultMin, maxD - 50.0f);
    const bool lod = g_cfg.traffic > kTrafficDefaultMax;
    float* pMin = verifiedVar(kTrafficMin);
    float* pMax = verifiedVar(kTrafficMax);
    float* pMed = lod ? verifiedVar(kLodMedium) : nullptr;
    if (!pMin || !pMax || (lod && !pMed)) {
        LOG("drawdist: traffic registry mismatch; unchanged");
        return;
    }
    *pMin = minD;
    *pMax = maxD;
    if (pMed) *pMed = std::max(kLodMediumDefault, g_cfg.traffic);
    LOG("drawdist: traffic fade %.0f-%.0f m", minD, maxD);
}

// ---------------------------------------------------------------------------
// the hook on the track settings deserializer
// ---------------------------------------------------------------------------
extern "C" uint64_t hookTrackSettings(uint8_t* ts, void* ar, void* r8, void* r9) {
    uint64_t r = reinterpret_cast<SerializeFn>(g_base + kSerializeRva)(ts, ar, r8, r9);
    if (!g_trafficDone.exchange(true)) applyTrafficOverrides();
    applyTrackOverrides(ts);
    return r;
}

bool installTrackHook() {
    const uint8_t* site = reinterpret_cast<const uint8_t*>(g_base + kThunkRva);
    if (!readable(site, sizeof(kThunkBytes)) || std::memcmp(site, kThunkBytes, sizeof(kThunkBytes)) != 0) {
        LOG("drawdist: track settings thunk mismatch; unchanged");
        return false;
    }
    void* mem = allocNear(g_base + kThunkJmpRva, 64);
    if (!mem) { LOG("drawdist: could not allocate thunk memory; unchanged"); return false; }
    // patchCallRel32 only rewrites the rel32 behind the opcode byte, so it serves the E9 jump as well.
    if (!patchCallRel32(g_base + kThunkJmpRva, reinterpret_cast<void*>(&hookTrackSettings), mem)) return false;
    LOG("drawdist: track settings hook installed");
    return true;
}

// ---------------------------------------------------------------------------
// C
// ---------------------------------------------------------------------------
void scaleCarLod(void* self) {
    if (!(g_cfg.racer > kCarFarthest)) return;
    float* v = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + 8);
    if (!readable(v, 16)) return;
    const float k = g_cfg.racer / kCarFarthest;
    for (int i = 0; i < 4; ++i) v[i] = std::max(v[i], v[i] * k);
    if (!g_carLogged.exchange(true)) LOG("drawdist: car LOD distances %.0f/%.0f/%.0f/%.0f m", v[0], v[1], v[2], v[3]);
}

uint64_t hookCarParse(void* self, void* a2, void* a3, void* a4) {
    uint64_t r = reinterpret_cast<CarFn>(g_base + kParseRva)(self, a2, a3, a4);
    scaleCarLod(self);
    return r;
}

uint64_t hookCarDefaults(void* self, void* a2, void* a3, void* a4) {
    uint64_t r = reinterpret_cast<CarFn>(g_base + kDefaultsRva)(self, a2, a3, a4);
    scaleCarLod(self);
    return r;
}

void installCarHook() {
    uint64_t* slots = reinterpret_cast<uint64_t*>(g_base + kCarVtable);
    if (!readable(slots, 24) || slots[0] != g_base + kNameGetterRva || slots[1] != g_base + kParseRva ||
        slots[2] != g_base + kDefaultsRva) {
        LOG("drawdist: car LOD vtable mismatch; unchanged");
        return;
    }
    DWORD prot;
    if (!VirtualProtect(&slots[1], 16, PAGE_READWRITE, &prot)) {
        LOG("drawdist: car LOD VirtualProtect failed %lu; unchanged", GetLastError());
        return;
    }
    slots[1] = reinterpret_cast<uint64_t>(&hookCarParse);
    slots[2] = reinterpret_cast<uint64_t>(&hookCarDefaults);
    VirtualProtect(&slots[1], 16, prot, &prot);
    LOG("drawdist: car LOD hook installed (racer %.0f m)", g_cfg.racer);
}

} // namespace

void DrawDist_Install(uintptr_t base, const Config& cfg) {
    g_base = base;
    g_cfg = cfg.drawDist;
    const DrawDistConfig& d = g_cfg;
    if (d.shadow > 0 || d.world > 0 || d.props > 0 || d.crowd > 0 || d.grass > 0 || d.traffic > 0) installTrackHook();
    if (d.racer > 0) installCarHook();
}

} // namespace fh2

// Test hooks for tests/harness.cpp: the original deserializer needs a real archive, so the overrides
// are exercised directly.
extern "C" __declspec(dllexport) void FH2SI_TestApplyTrackOverrides(uint8_t* ts) { fh2::applyTrackOverrides(ts); }
extern "C" __declspec(dllexport) void FH2SI_TestApplyTrafficOverrides() { fh2::applyTrafficOverrides(); }

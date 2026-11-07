// SPDX-License-Identifier: MIT
// Replaces vtable slot 14 (GetFOV) of the selected camera classes with a wrapper that adds
// a configurable number of degrees to the game's own value.
#include "fov_hook.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include "log.h"

namespace fh2 {
namespace {

constexpr uintptr_t kGetFovRva = 0x10340f0;
constexpr size_t    kFovSlot   = 14;

enum Cls { kCockpit, kDriver, kHood, kBumper, kClsCount };
struct ClsInfo { const char* name; uintptr_t vtableRva; };
const ClsInfo kCls[kClsCount] = {
    {"cockpit", 0x2e77c30}, {"driver", 0x2e78120}, {"hood", 0x2e77ea8}, {"bumper", 0x2e703b0},
};

uintptr_t         g_base = 0;
float             g_addDeg[kClsCount] = {};
std::atomic<bool> g_logged[kClsCount];
std::atomic<uint64_t> g_cockpitTick{0};

float applyFov(int cls, void* cam) {
    float f = reinterpret_cast<float (*)(void*)>(g_base + kGetFovRva)(cam);
    if (!g_logged[cls].exchange(true)) LOG("fov: %s base %.3f", kCls[cls].name, f);
    if (cls == kCockpit) g_cockpitTick.store(GetTickCount64(), std::memory_order_relaxed);
    if (g_addDeg[cls] == 0.0f) return f;        // wrapper only installed for the cockpit timestamp
    const float kPi = 3.14159265358979f;
    bool deg = f > 3.2f;
    float add = deg ? g_addDeg[cls] : g_addDeg[cls] * kPi / 180.0f;
    float hi = deg ? 150.0f : 150.0f * kPi / 180.0f;
    float lo = f * 0.5f;
    float n = f + add;
    if (n > hi) n = hi;
    if (n < lo) n = lo;
    if (!(n > 0.0f)) n = f;
    return n;
}

float fovCockpit(void* cam) { return applyFov(kCockpit, cam); }
float fovDriver(void* cam)  { return applyFov(kDriver, cam); }
float fovHood(void* cam)    { return applyFov(kHood, cam); }
float fovBumper(void* cam)  { return applyFov(kBumper, cam); }

} // namespace

void FovHook_Install(uintptr_t base, const Config& cfg) {
    g_base = base;
    g_addDeg[kCockpit] = cfg.sim.fovCockpit;
    g_addDeg[kDriver]  = cfg.sim.fovDriver;
    g_addDeg[kHood]    = cfg.sim.fovHood;
    g_addDeg[kBumper]  = cfg.sim.fovBumper;
    void* const fns[kClsCount] = {
        reinterpret_cast<void*>(&fovCockpit), reinterpret_cast<void*>(&fovDriver),
        reinterpret_cast<void*>(&fovHood), reinterpret_cast<void*>(&fovBumper),
    };
    for (int i = 0; i < kClsCount; ++i) {
        if (g_addDeg[i] == 0.0f && !(i == kCockpit && cfg.sim.hideDriverInCockpit)) continue;
        uint64_t* slot = reinterpret_cast<uint64_t*>(base + kCls[i].vtableRva + kFovSlot * 8);
        if (*slot != static_cast<uint64_t>(base + kGetFovRva)) {
            LOG("fov: %s vtable mismatch; unchanged", kCls[i].name);
            continue;
        }
        DWORD prot;
        if (!VirtualProtect(slot, 8, PAGE_READWRITE, &prot)) {
            LOG("fov: %s VirtualProtect failed %lu; unchanged", kCls[i].name, GetLastError());
            continue;
        }
        *slot = reinterpret_cast<uint64_t>(fns[i]);
        VirtualProtect(slot, 8, prot, &prot);
        LOG("fov: %s +%.1f deg installed", kCls[i].name, g_addDeg[i]);
    }
}

uint64_t FovHook_LastCockpitTick() { return g_cockpitTick.load(std::memory_order_relaxed); }

} // namespace fh2

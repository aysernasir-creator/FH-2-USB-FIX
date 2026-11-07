// SPDX-License-Identifier: MIT
// Replaces the ABS / traction control / stability control / shifting-mode getters of LocalPlayerChoices so that
// each car gets the assists its real counterpart had (data/car_assists.csv), or a per-car override.
#include "assist_hook.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include "../common/car_db.h"
#include "log.h"

namespace fh2 {
namespace {

constexpr uintptr_t kVtableRva[2]  = {0x2c5bfc8, 0x2cf39c0};   // LocalPlayerChoices, TRefCountedObject<ForzaLocalPlayerChoices>
constexpr size_t    kAbsSlotOff    = 0xd8;                      // braking mode: 0 assisted, 1 ABS on, 2 ABS off
constexpr size_t    kClutchSlotOff = 0xf8;                   // shifting mode: 0 automatic, 1 manual, 2 manual with clutch
constexpr size_t    kStmSlotOff    = 0xe8;                      // stability control: 0 on, 1 off
constexpr size_t    kTcSlotOff     = 0xf0;                      // traction control: 0 on, 1 off
constexpr uintptr_t kAbsGetterRva  = 0xfbcb50;
constexpr uintptr_t kStmGetterRva  = 0xfbcb80;
constexpr uintptr_t kTcGetterRva   = 0x1391660;
constexpr uintptr_t kClutchGetterRva = 0xfbcb70;
constexpr size_t    kAbsFieldOff   = 0x2b1;
constexpr size_t    kStmFieldOff   = 0x2b3;
constexpr size_t    kTcFieldOff    = 0x2b4;
constexpr size_t    kClutchFieldOff = 0x2b5;

uintptr_t g_base = 0;
bool      g_absOn = false, g_tcOn = false, g_stmOn = false, g_clutchOn = false, g_autoOn = false;
AssistMode g_optional = AssistMode::Game;
std::vector<CarAssistOverride> g_overrides;

std::atomic<bool>     g_installed{false};
std::atomic<int64_t>  g_carId{-1};
std::atomic<uint64_t> g_carTick{0};
std::atomic<const char*> g_carName{nullptr};
std::atomic<int>      g_absMode{0}, g_tcMode{0}, g_stmMode{0}, g_clutchMode{0};   // AssistMode of the current car (clutch: On = has one)
std::atomic<bool>     g_forceAuto{false};                                          // current car: always shift automatically
std::atomic<bool>     g_loggedAbs{false}, g_loggedTc{false}, g_loggedStm{false}, g_loggedClutch{false}, g_loggedAuto{false};
std::atomic<uint32_t> g_callsAbs{0}, g_callsTc{0}, g_callsStm{0}, g_callsClutch{0};

const char* word(char c) {
    switch (c) { case 'y': return "yes"; case 'n': return "no"; case 'o': return "optional"; default: return "unknown"; }
}

AssistMode carValue(char c) {
    switch (c) {
        case 'y': return AssistMode::On;
        case 'n': return AssistMode::Off;
        case 'o': return g_optional;
        default:  return AssistMode::Game;
    }
}

const char* gearboxWord(char c) {
    switch (c) {
        case 'm': return "manual"; case 'x': return "mixed"; case 'a': return "at"; case 't': return "atm"; case 'd': return "dct";
        case 's': return "amt"; case 'c': return "cvt"; case 'e': return "ev"; case 'q': return "seq"; default: return "unknown";
    }
}

const char* modeName(AssistMode m) { return m == AssistMode::On ? "on" : m == AssistMode::Off ? "off" : "game"; }

// ---- getters (same signature as the originals: movzx eax, byte [rcx+X]) --------------------------
uint8_t absGetter(void* self) {
    uint8_t real = *(reinterpret_cast<uint8_t*>(self) + kAbsFieldOff);
    g_callsAbs.fetch_add(1, std::memory_order_relaxed);
    uint8_t out = real;
    switch (static_cast<AssistMode>(g_absMode.load(std::memory_order_relaxed))) {
        case AssistMode::On:  out = real == 2 ? 1 : real; break;
        case AssistMode::Off: out = 2; break;
        default: break;
    }
    if (out != real && !g_loggedAbs.exchange(true)) {
        const char* n = g_carName.load();
        LOG("assists: ABS forced %s for %s", out == 2 ? "off" : "on", n ? n : "unknown car");
    }
    return out;
}

uint8_t tcLike(void* self, size_t field, std::atomic<int>& mode, std::atomic<bool>& logged, const char* what) {
    uint8_t real = *(reinterpret_cast<uint8_t*>(self) + field);
    uint8_t out = real;
    switch (static_cast<AssistMode>(mode.load(std::memory_order_relaxed))) {
        case AssistMode::On:  out = 0; break;
        case AssistMode::Off: out = 1; break;
        default: break;
    }
    if (out != real && !logged.exchange(true)) {
        const char* n = g_carName.load();
        LOG("assists: %s forced %s for %s", what, out == 0 ? "on" : "off", n ? n : "unknown car");
    }
    return out;
}

uint8_t tcGetter(void* self) {
    g_callsTc.fetch_add(1, std::memory_order_relaxed);
    return tcLike(self, kTcFieldOff, g_tcMode, g_loggedTc, "TC");
}

uint8_t stmGetter(void* self) {
    g_callsStm.fetch_add(1, std::memory_order_relaxed);
    return tcLike(self, kStmFieldOff, g_stmMode, g_loggedStm, "stability control");
}

uint8_t clutchGetter(void* self) {
    uint8_t real = *(reinterpret_cast<uint8_t*>(self) + kClutchFieldOff);
    g_callsClutch.fetch_add(1, std::memory_order_relaxed);
    uint8_t out = real;
    if (g_forceAuto.load(std::memory_order_relaxed)) {
        out = 0;
        if (!g_loggedAuto.exchange(true)) {
            const char* n = g_carName.load();
            LOG("assists: automatic shifting forced for %s", n ? n : "unknown car");
        }
        return out;
    }
    switch (static_cast<AssistMode>(g_clutchMode.load(std::memory_order_relaxed))) {
        case AssistMode::On:  if (real == 1) out = 2; break;           // automatic stays automatic
        case AssistMode::Off: if (real == 2) out = 1; break;
        default: break;
    }
    if (out != real && !g_loggedClutch.exchange(true)) {
        const char* n = g_carName.load();
        LOG("assists: clutch forced %s for %s", out == 2 ? "on" : "off", n ? n : "unknown car");
    }
    return out;
}

// ---- decision per car ----------------------------------------------------------------------------
void decide(uint32_t id, const CarInfo* car) {
    AssistMode a = AssistMode::Game, t = AssistMode::Game, s = AssistMode::Game, c = AssistMode::Game;
    if (car) {
        if (g_absOn) a = carValue(car->abs);
        if (g_tcOn)  t = carValue(car->tc);
        if (g_stmOn) s = carValue(car->stm);
        if (g_clutchOn) c = carValue(car->clutch);
    }
    // automatic-only gearbox: forced unless the car has an explicit clutch override (on / off)
    bool forceAuto = car && g_autoOn && (car->gearbox == 'a' || car->gearbox == 'c' || car->gearbox == 'e');
    for (const auto& o : g_overrides) {
        if (o.id != id) continue;
        if (o.clutchSet) forceAuto = o.forceAutomatic;      // "automatic" forces; on / off cancel; auto (not set) keeps the rule
        if (o.clutchSet && g_clutchOn && !o.forceAutomatic) c = o.clutch;
        if (o.absSet && g_absOn) a = o.abs;
        if (o.tcSet && g_tcOn)   t = o.tc;
        if (o.stmSet && g_stmOn) s = o.stm;
        break;
    }
    g_absMode = static_cast<int>(a);
    g_tcMode  = static_cast<int>(t);
    g_stmMode = static_cast<int>(s);
    g_clutchMode = static_cast<int>(c);
    g_forceAuto = forceAuto;
}

DWORD WINAPI diagThread(void*) {
    for (;;) {
        Sleep(5000);
        int64_t id = g_carId.load();
        LOG("assists diag: car %u getters abs %u tc %u stm %u clutch %u (calls since last)", id < 0 ? 0u : (unsigned)id,
            g_callsAbs.exchange(0), g_callsTc.exchange(0), g_callsStm.exchange(0), g_callsClutch.exchange(0));
    }
}

bool patchVtable(int which, void* absFn, void* stmFn, void* tcFn, void* clutchFn) {
    uint64_t* vt = reinterpret_cast<uint64_t*>(g_base + kVtableRva[which]);
    uint64_t* sAbs = vt + kAbsSlotOff / 8;
    uint64_t* sStm = vt + kStmSlotOff / 8;
    uint64_t* sTc  = vt + kTcSlotOff / 8;
    uint64_t* sClutch = vt + kClutchSlotOff / 8;
    if (*sAbs != g_base + kAbsGetterRva || *sStm != g_base + kStmGetterRva || *sTc != g_base + kTcGetterRva ||
        *sClutch != g_base + kClutchGetterRva) {
        LOG("assists: vtable %d mismatch; unchanged", which);
        return false;
    }
    struct Job { uint64_t* slot; void* fn; };
    const Job jobs[4] = {{sAbs, absFn}, {sStm, stmFn}, {sTc, tcFn}, {sClutch, clutchFn}};
    for (const Job& j : jobs) {
        if (!j.fn) continue;
        DWORD prot;
        if (!VirtualProtect(j.slot, 8, PAGE_READWRITE, &prot)) {
            LOG("assists: vtable %d VirtualProtect failed %lu; slot unchanged", which, GetLastError());
            continue;
        }
        *j.slot = reinterpret_cast<uint64_t>(j.fn);
        VirtualProtect(j.slot, 8, prot, &prot);
    }
    return true;
}

} // namespace

void AssistHook_SetPlayerCar(uint32_t ordinal) {
    if (!g_installed.load(std::memory_order_acquire)) return;
    g_carTick.store(GetTickCount64(), std::memory_order_relaxed);
    if (g_carId.load(std::memory_order_relaxed) == static_cast<int64_t>(ordinal)) return;
    const CarInfo* car = FindCar(ordinal);
    decide(ordinal, car);
    g_carName.store(car ? car->name : nullptr);
    g_loggedAbs = false; g_loggedTc = false; g_loggedStm = false; g_loggedClutch = false; g_loggedAuto = false;
    g_carId.store(static_cast<int64_t>(ordinal));
    LOG("car: %u %s (ABS %s, TC %s, STM %s, clutch %s, gearbox %s)", ordinal, car ? car->name : "unknown car",
        car ? word(car->abs) : "unknown", car ? word(car->tc) : "unknown", car ? word(car->stm) : "unknown",
        car ? word(car->clutch) : "unknown", car ? gearboxWord(car->gearbox) : "unknown");
}

void AssistHook_Install(uintptr_t base, const Config& cfg) {
    g_absOn = cfg.sim.realisticAbs;
    g_tcOn  = cfg.sim.realisticTc;
    g_stmOn = cfg.sim.realisticStm;
    g_clutchOn = cfg.sim.realisticClutch;
    g_autoOn = cfg.sim.realisticAutomatic;
    if (!g_absOn && !g_tcOn && !g_stmOn && !g_clutchOn && !g_autoOn) return;
    g_base = base;
    g_optional = cfg.sim.optionalAssists;
    g_overrides = cfg.sim.carOverrides;
    void* absFn = g_absOn ? reinterpret_cast<void*>(&absGetter) : nullptr;
    void* tcFn  = g_tcOn ? reinterpret_cast<void*>(&tcGetter) : nullptr;
    void* stmFn = g_stmOn ? reinterpret_cast<void*>(&stmGetter) : nullptr;
    void* clutchFn = (g_clutchOn || g_autoOn) ? reinterpret_cast<void*>(&clutchGetter) : nullptr;
    int ok = 0;
    for (int i = 0; i < 2; ++i) if (patchVtable(i, absFn, stmFn, tcFn, clutchFn)) ++ok;
    if (!ok) { LOG("assists: no vtable verified; realistic assists inactive"); return; }
    LOG("assists: getters installed in %d vtable(s) (ABS %d, TC %d, STM %d, clutch %d, automatic %d, optional %s)", ok, g_absOn, g_tcOn,
        g_stmOn, g_clutchOn, g_autoOn, modeName(g_optional));
    g_installed.store(true, std::memory_order_release);
    if (cfg.diagnostics)
        if (HANDLE t = CreateThread(nullptr, 0, diagThread, nullptr, 0, nullptr)) CloseHandle(t);
}

} // namespace fh2

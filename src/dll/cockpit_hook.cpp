// SPDX-License-Identifier: MIT
// Replaces the car update entry in the car vtable: after the game computed the in-game steering wheel angle
// (car + 0x4f30, radians) the angle of the real wheel is written over it, and the driver model is shown or
// hidden depending on the active camera.
#include "cockpit_hook.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include "fov_hook.h"
#include "log.h"
#include "patch_util.h"
#include "wheel_com.h"
#include "wheel_device.h"

namespace fh2 {
namespace {

constexpr uintptr_t kCarUpdateRva  = 0x2a9400;     // void CarUpdate(Car*, float, float, int)
constexpr uintptr_t kCarUpdateSlot = 0x2c5cc00;     // vtable slot holding base + kCarUpdateRva
constexpr uintptr_t kSetDriverRva  = 0xf75500;     // void Car_SetDriverVisible(Car*, bool)
const uint8_t       kSetDriverBytes[10] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr size_t    kWheelAngleOff = 0x4f30;        // float, radians
constexpr size_t    kFrozenOff     = 0x4f11;        // byte, != 0: the game freezes the angle (photo mode ...)
constexpr size_t    kDriverShownOff = 0x4f10;       // byte, 1 = driver visible
constexpr size_t    kDriverModelOff = 0x4d38;       // qword pointer, != 0: the car has a driver model
constexpr size_t    kCarReadSize   = 0x4f40;
constexpr uint64_t  kCockpitFreshMs = 250;
constexpr uint64_t  kPlayerFreshMs  = 1000;

using CarUpdateFn = void (*)(uint8_t*, float, float, int);
using SetDriverFn = void (*)(uint8_t*, bool);

uintptr_t         g_base = 0;
bool              g_wheelOn = false, g_hideOn = false, g_hideReady = false;
float             g_dir = 1.0f;
std::atomic<uint8_t*>  g_playerCar{nullptr};
std::atomic<uint64_t>  g_playerTick{0};
uint8_t*          g_hidCar = nullptr;          // game thread only
bool              g_weHid = false;
std::atomic<bool> g_loggedWheel{false};

const float kPi = 3.14159265358979f;

// The in-game wheel angle for the steering value s (-1..1) at the given display lock.
void applyWheel(uint8_t* car, float s, float displayLock) {
    if (!readable(car, kCarReadSize)) return;
    if (car[kFrozenOff] != 0) return;
    const float a = g_dir * s * displayLock * 0.5f * kPi / 180.0f;
    std::memcpy(car + kWheelAngleOff, &a, sizeof(a));
}

void applyDriver(uint8_t* car) {
    if (!g_hideReady || !readable(car, kCarReadSize)) return;
    if (g_weHid && g_hidCar != car) g_weHid = false;      // the player's car changed: never touch the old one
    uint64_t model;
    std::memcpy(&model, car + kDriverModelOff, sizeof(model));
    if (model == 0) return;                                // no driver model: the game ignores the call too
    const bool cockpit = GetTickCount64() - FovHook_LastCockpitTick() <= kCockpitFreshMs && FovHook_LastCockpitTick() != 0;
    const bool want = !cockpit;                            // driver visible wanted
    auto setVisible = reinterpret_cast<SetDriverFn>(g_base + kSetDriverRva);
    if (!want && car[kDriverShownOff] != 0) {
        setVisible(car, false);
        g_weHid = true; g_hidCar = car;
        LOG("cockpit: driver hidden");
    } else if (want && g_weHid && car[kDriverShownOff] == 0) {
        setVisible(car, true);
        g_weHid = false;
        LOG("cockpit: driver shown");
    }
}

void hookCarUpdate(uint8_t* car, float a, float b, int c) {
    reinterpret_cast<CarUpdateFn>(g_base + kCarUpdateRva)(car, a, b, c);
    if (!car || car != g_playerCar.load(std::memory_order_relaxed)) return;
    if (GetTickCount64() - g_playerTick.load(std::memory_order_relaxed) > kPlayerFreshMs) return;
    if (!readable(car, kCarReadSize)) return;
    if (g_wheelOn) {
        WheelInput in;
        if (WheelDevice_Read(in)) {
            applyWheel(car, VirtualWheel_GameAngle(in.steering), VirtualWheel_DisplayLockDeg());
            if (!g_loggedWheel.exchange(true))
                LOG("cockpit: in-game wheel follows the real wheel (display lock %.0f deg)", VirtualWheel_DisplayLockDeg());
        }
    }
    if (g_hideOn) applyDriver(car);
}

} // namespace

void CockpitHook_SetPlayerCar(uint8_t* car) {
    g_playerCar.store(car, std::memory_order_relaxed);
    g_playerTick.store(GetTickCount64(), std::memory_order_relaxed);
}

void CockpitHook_Install(uintptr_t base, const Config& cfg) {
    g_wheelOn = cfg.sim.realCockpitWheel;
    g_hideOn = cfg.sim.hideDriverInCockpit;
    g_dir = cfg.sim.cockpitWheelDirection < 0 ? -1.0f : 1.0f;
    if (!g_wheelOn && !g_hideOn) return;
    g_base = base;
    if (g_hideOn) {
        if (std::memcmp(reinterpret_cast<const void*>(base + kSetDriverRva), kSetDriverBytes, sizeof(kSetDriverBytes)) != 0) {
            LOG("cockpit: Car_SetDriverVisible bytes mismatch; driver hiding off");
            g_hideOn = false;
        } else {
            g_hideReady = true;
            LOG("cockpit: driver hide ready");
        }
    }
    if (!g_wheelOn && !g_hideOn) return;
    uint64_t* slot = reinterpret_cast<uint64_t*>(base + kCarUpdateSlot);
    if (*slot != static_cast<uint64_t>(base + kCarUpdateRva)) {
        LOG("cockpit: car update slot mismatch; unchanged");
        return;
    }
    DWORD prot;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &prot)) {
        LOG("cockpit: VirtualProtect failed %lu; unchanged", GetLastError());
        return;
    }
    *slot = reinterpret_cast<uint64_t>(&hookCarUpdate);
    VirtualProtect(slot, 8, prot, &prot);
    LOG("cockpit: car update hook installed (in-game wheel %d, hide driver %d, direction %+d)", g_wheelOn, g_hideOn, (int)g_dir);
}

} // namespace fh2

// Test hook for the integration harness: runs the wheel angle computation on a fake car buffer as if it were
// the player car with a display lock of 900 degrees.
extern "C" __declspec(dllexport) void FH2SI_TestCockpitWheel(uint8_t* car, float s) {
    fh2::applyWheel(car, s, 900.0f);
}

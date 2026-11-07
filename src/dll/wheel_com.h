// SPDX-License-Identifier: MIT
// The virtual Xbox wheel: a fake Windows.Xbox.Input.IController/IWheel and its helper objects.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include "config.h"
#include "wheel_ffb.h"

namespace fh2 {

void  VirtualWheel_Configure(const Config& cfg);   // call before anything else (stores settings)
void* VirtualWheel_Controller();          // IController* of the single static fake controller (borrowed)
void  VirtualWheel_SetGameBase(uintptr_t base);
float VirtualWheel_GameRotation();        // last SetRotation value (degrees), default cfg.wheelRange
// Takes over one reference to `user` (may be null) as the controller's User (UserMode auto).
void  VirtualWheel_SetUser(void* user);
// Formats the registered variables (current [min..max]) for the diagnostics line and
// resets their min/max.
// Snapshot of the captured force feedback variables plus the race/stale flags.
void  VirtualWheel_FfbSnapshot(FfbParams& out);
// The steering value the game sees for a -1..1 physical steering input (rotation-scaled, clamped).
float VirtualWheel_GameAngle(float steering);
// Physical lock (degrees lock to lock) the player turns the real wheel through for full steering, and the
// lock shown by the in-game wheel (see steer_lock.h). Use the current car and settings.
float VirtualWheel_PhysicalLockDeg();
float VirtualWheel_DisplayLockDeg();
// Records the local player's car (called from the FFB call-site hook, game thread): selects its steering lock.
void  SteerLock_SetPlayerCar(uint32_t id);
// Stores the game's per-frame UpdateFFB input (called from the game thread; stamps qpc itself).
void  VirtualWheel_SetGameFfbInput(const GameFfbInput& in);
// Latest captured UpdateFFB input regardless of age; false when none was captured yet (tests).
bool  VirtualWheel_LastGameFfbInput(GameFfbInput& out);
void  VirtualWheel_FormatVars(char* buf, size_t cap);

} // namespace fh2

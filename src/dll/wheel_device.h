// SPDX-License-Identifier: MIT
// DirectInput steering wheel reader (Logitech G29 and friends).
#pragma once
#include <windows.h>
#include <cstdint>
#include "config.h"
#include "wheel_ffb.h"

namespace fh2 {

struct WheelInput {            // already mapped to game conventions
    float steering = 0;        // -1..1 BEFORE rotation scaling (full lock of the physical wheel = +-1)
    float throttle = 0, brake = 0, clutch = 0;   // 0..1, 0 = released
    bool  a = false, b = false, x = false, y = false, view = false, menu = false;
    bool  up = false, down = false, left = false, right = false, lb = false, rb = false;
    bool  handbrake = false;   // real handbrake pulled (already ORed into the configured game button)
    float handbrakeAxis = 0;   // 0..1 normalized handbrake axis (0 when none)
    int   gear = 0;            // -1 reverse, 0 neutral, 1..7 (only when H-shifter enabled)
    uint64_t timestamp = 0;    // QPC ticks of last poll
    bool  valid = false;       // device acquired and polled within the last 500 ms
    int32_t  raw[8] = {};      // raw DirectInput axes: lX, lY, lZ, lRx, lRy, lRz, slider0, slider1
    uint32_t rawButtons[4] = {};   // bit i of word i/32 set when DirectInput button i is down (0..127)
};

struct FfbStatus {             // latest force feedback state, for diagnostics
    FfbTerms terms{};
    float angle = 0, velocity = 0;
    long  mag = 0;             // last computed device magnitude (-10000..10000)
    float lockDeg = 0, lockLim = 0, lockK = 0;   // soft lock: |wheel degrees|, lock/2, build-up 0..1
    bool  race = false, stale = true;
    bool  active = false;      // effect running
    bool  haveGame = false;    // game UpdateFFB input was fresh
    GameFfbInput game{};
    float leftAmp = 0, rightAmp = 0;   // raw rumble amplitudes from the game
};

// Enumerates, logs, starts the poll thread; false if no device (unless TestMode).
bool WheelDevice_Start(const WheelConfig& cfg, bool diagnostics = false);
// Copy of the latest snapshot; returns out.valid.
bool WheelDevice_Read(WheelInput& out);
// True when n > 0, the snapshot is fresh and every listed DirectInput button (0-based) is down.
bool WheelDevice_ButtonsHeld(const int* idx, int n);
// Copy of the latest force feedback state; returns out.active.
bool WheelDevice_FfbStatus(FfbStatus& out);

} // namespace fh2

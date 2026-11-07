// SPDX-License-Identifier: MIT
// Steering lock math (pure inline functions, unit-tested by tests/test_lock.cpp).
#pragma once
#include <algorithm>
#include <cmath>
#include "wheel_ffb.h"

namespace fh2 {

struct LockInputs {
    float wheelRange = 900;     // [Wheel] WheelRange (degrees, the range set in G HUB)
    float maxRotation = 0;      // MaxWheelRotation (0 = WheelRange)
    bool  scale = false;        // SteeringLockMode=scale
    bool  perCar = false;       // RealisticSteeringLock
    float carLock = 0;          // current car's lock to lock (degrees), 0 = unknown
    float gameRotation = 900;   // the game's own SetRotation value
    float maxCarLock = 0;       // largest lock in the car list
};

// Physical lock (degrees lock to lock) the player turns the real wheel through for full in-game steering.
inline float PhysicalLock(const LockInputs& in) {
    const float M = in.maxRotation > 0 ? std::min(in.maxRotation, in.wheelRange) : in.wheelRange;
    const bool car = in.perCar && in.carLock > 0;
    const float L = car ? in.carLock : in.gameRotation;
    float p;
    if (!in.perCar && !(in.maxRotation > 0)) p = L;         // nothing configured: the game's rotation, as before 0.9.6
    else if (in.scale && car) {
        p = in.maxCarLock > 0 ? std::min(L * M / in.maxCarLock, M) : std::min(L, M);
    } else p = std::min(L, M);
    return std::max(p, 40.0f);
}

// Lock shown by the in-game wheel at full steering.
inline float DisplayLock(const LockInputs& in) {
    return (in.perCar && in.carLock > 0) ? in.carLock : PhysicalLock(in);
}

// Game steering value (-1..1) for a -1..1 physical steering input
// (= clamp(physDeg / (PhysicalLock / 2), -1, 1) with physDeg = steering * wheelRange / 2).
inline float GameSteer(const LockInputs& in, float steering) {
    const float s = steering * in.wheelRange / PhysicalLock(in);
    return s > 1.0f ? 1.0f : s < -1.0f ? -1.0f : s;
}

// Build-up factor 0..1 of the soft lock: 0 inside the lock (or when it does not apply), 1 once rampDeg past it.
inline float SoftLockK(float physDeg, float physLock, float wheelRange, float strength, float rampDeg) {
    if (!(strength > 0.0f) || !(physLock < wheelRange - 1.0f) || !(rampDeg > 0.0f)) return 0.0f;
    const float over = std::fabs(physDeg) - physLock * 0.5f;
    if (!(over > 0.0f)) return 0.0f;
    return std::min(over / rampDeg, 1.0f);
}

// Blends the end-stop force into a device magnitude (-10000..10000).
inline long ApplySoftLock(long mag, float physDeg, float physLock, float wheelRange, float strength, float rampDeg,
                          bool invert) {
    const float k = SoftLockK(physDeg, physLock, wheelRange, strength, rampDeg);
    if (!(k > 0.0f)) return mag;
    const float Fe = -(physDeg > 0 ? 1.0f : -1.0f) * 0.5f * strength;   // game units, like the spring term -scale*angle
    const long magEnd = FfbDeviceMagnitude(Fe, 1.0f, invert);
    return std::lround((float)mag * (1.0f - k) + (float)magEnd * k);
}

} // namespace fh2

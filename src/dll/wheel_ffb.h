// SPDX-License-Identifier: MIT
// Emulation of the Xbox wheel's on-device force feedback programs (pure math, no Windows calls).
#pragma once
#include <cstdint>

namespace fh2 {

// Inputs the game hands to UpdateFFB each frame, plus the driver's parameters (captured by a hook).
struct GameFfbInput {
    float rawTorque = 0, speed = 0, slip = 0, gain = 0, dynScale = 0, dynMax = 0, springParam = 0,
          slip0 = 0, slip1 = 0, slipScale0 = 0, slipScale1 = 0, speed0 = 0, speed1 = 0,
          speedScale0 = 0, speedScale1 = 0;
    float frontSlip = 0;     // load-weighted average of the front tyres' slip
    bool  haveFront = false;
    uint64_t qpc = 0;
};

struct FfbParams {
    float torque = 0, menuSpringScale = 0, raceSpringScale = 0, raceSpringMax = 0,
          dampScale = 0, dampMax = 0, leftAmp = 0, leftWl = 0, rightAmp = 0, rightWl = 0;
    bool race = false;   // heartbeat seen within 2.5 s
    bool stale = true;   // no variable writes for > 1 s
    GameFfbInput game;   // latest UpdateFFB input
    bool haveGame = false;   // captured within the last 0.5 s
};

struct FfbTuning {
    float torqueGain = 2.0f, springGain = 1.0f, slipSpringFloor = 0.4f, slipGain = 0.5f,
          slipStart = 0.08f, slipFull = 0.55f;
    bool slipInvert = true;
    float alignGain = 0.6f, alignStart = 0.05f, alignFull = 0.5f;   // front-slip aligning force
    float rumbleGain = 1.5f;
};

struct FfbTerms { float torque, spring, damping, rumble, menu, total, slip, align; };

// Returns the clamped total force in game units (-0.5..0.5). terms may be null.
float FfbForce(const FfbParams& p, const FfbTuning& tune, float angle, float velocity, double tMs, FfbTerms* terms);
// Neutral tuning (torque x1, spring x1, no floor, no slip term).
float FfbForce(const FfbParams& p, float angle, float velocity, double tMs, FfbTerms* terms);
// Device magnitude -10000..10000 from a force, gain and invert flag.
long FfbDeviceMagnitude(float force, float gain, bool invert);

} // namespace fh2

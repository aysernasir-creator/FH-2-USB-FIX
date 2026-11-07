// SPDX-License-Identifier: MIT
// Pure pose math: OpenTrack pose -> camera offsets. No Windows dependencies so it
// can be unit tested on any platform.
#pragma once
#include <cmath>

namespace fh2 {

// OpenTrack UDP payload: six little-endian doubles.
// x, y, z in centimetres; yaw, pitch, roll in degrees.
struct RawPose {
    double x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0;
};

struct AxisCfg {
    float gain   = 1.0f;   // output units per input unit
    bool  invert = false;
    float limit  = 0.0f;   // |output| clamp, 0 = axis disabled
};

struct MapCfg {
    AxisCfg yaw, pitch, roll;  // degrees in, degrees out
    AxisCfg x, y, z;           // centimetres in, metres out (gain is m per m)
    bool    positionEnabled = true;
    // Output component order for the rotation Param vector (game: x=yaw,y=pitch,z=roll)
    int     rotOrder[3] = {0, 1, 2};
    // Output component order for the translation Param vector (car space)
    int     posOrder[3] = {0, 1, 2};
    // Extra yaw strength per side; yawLeftIsPositive says which sign of the yaw output is "left".
    float   yawLeftGain = 1.0f, yawRightGain = 1.0f;
    bool    yawLeftIsPositive = true;
};

struct CameraOffsets {
    float rotDeg[3] = {0, 0, 0};   // already ordered for the game's Param vector
    float posM[3]   = {0, 0, 0};
};

// posSideGain / negSideGain scale the value (before the limit) when it is positive / negative.
inline float applyAxis(double v, const AxisCfg& a, float posSideGain = 1.0f, float negSideGain = 1.0f) {
    if (a.limit <= 0.0f) return 0.0f;
    float out = static_cast<float>(v) * a.gain * (a.invert ? -1.0f : 1.0f);
    out *= out > 0.0f ? posSideGain : negSideGain;
    if (out >  a.limit) out =  a.limit;
    if (out < -a.limit) out = -a.limit;
    return out;
}

inline CameraOffsets mapPose(const RawPose& p, const RawPose& center, const MapCfg& c) {
    CameraOffsets o;
    // The game's camera pitch is positive downwards, OpenTrack's is positive up.
    float rot[3] = {
        applyAxis(p.yaw   - center.yaw,   c.yaw,
                  c.yawLeftIsPositive ? c.yawLeftGain : c.yawRightGain,
                  c.yawLeftIsPositive ? c.yawRightGain : c.yawLeftGain),
        -applyAxis(p.pitch - center.pitch, c.pitch),
        applyAxis(p.roll  - center.roll,  c.roll),
    };
    for (int i = 0; i < 3; ++i) o.rotDeg[c.rotOrder[i]] = rot[i];
    if (c.positionEnabled) {
        float pos[3] = {
            applyAxis((p.x - center.x) / 100.0, c.x),
            applyAxis((p.y - center.y) / 100.0, c.y),
            applyAxis((p.z - center.z) / 100.0, c.z),
        };
        for (int i = 0; i < 3; ++i) o.posM[c.posOrder[i]] = pos[i];
    }
    return o;
}

// Exponential smoothing that is independent of packet rate.
// amount 0 = none, 1 = very heavy. dt in seconds.
inline double smoothFactor(double amount, double dt) {
    if (amount <= 0.0) return 1.0;
    if (amount > 0.99) amount = 0.99;
    // time constant grows from 0 to ~0.5 s across the range
    double tau = 0.5 * amount * amount;
    return 1.0 - std::exp(-dt / tau);
}

inline void smoothPose(RawPose& state, const RawPose& in, double alpha) {
    state.x     += (in.x - state.x) * alpha;
    state.y     += (in.y - state.y) * alpha;
    state.z     += (in.z - state.z) * alpha;
    state.yaw   += (in.yaw - state.yaw) * alpha;
    state.pitch += (in.pitch - state.pitch) * alpha;
    state.roll  += (in.roll - state.roll) * alpha;
}

inline bool poseFinite(const RawPose& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.yaw) && std::isfinite(p.pitch) && std::isfinite(p.roll); }

} // namespace fh2

// SPDX-License-Identifier: MIT
// Host-side unit tests for the pose mapping (g++ -std=c++17 tests/test_pose.cpp).
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/dll/pose.h"

using namespace fh2;

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

static MapCfg defaults() {
    MapCfg c;
    c.yaw = {1.0f, false, 150.0f};
    c.pitch = {1.0f, false, 60.0f};
    c.roll = {1.0f, false, 30.0f};
    c.x = {1.0f, false, 0.25f};
    c.y = {1.0f, false, 0.15f};
    c.z = {1.0f, false, 0.20f};
    return c;
}

int main() {
    RawPose zero;
    {
        RawPose p;
        assert(poseFinite(p));
        p.yaw = std::nan("");
        assert(!poseFinite(p));
        RawPose q;
        q.x = HUGE_VAL;
        assert(!poseFinite(q));
    }
    // identity mapping: degrees pass through, cm -> m
    {
        RawPose p{10, -5, 3, 30, -10, 4};
        auto o = mapPose(p, zero, defaults());
        assert(near(o.rotDeg[0], 30) && near(o.rotDeg[1], 10) && near(o.rotDeg[2], 4));   // pitch flipped for the game
        assert(near(o.posM[0], 0.10f) && near(o.posM[1], -0.05f) && near(o.posM[2], 0.03f));
    }
    // limits clamp, limit 0 disables, invert flips, gain scales
    {
        MapCfg c = defaults();
        c.yaw.gain = 2.0f; c.pitch.invert = true; c.roll.limit = 0; c.x.limit = 0.05f;
        RawPose p{40, 0, 0, 100, 20, 15};
        auto o = mapPose(p, zero, c);
        assert(near(o.rotDeg[0], 150));      // 200 clamped
        assert(near(o.rotDeg[1], 20));    // inverted, then flipped for the game
        assert(near(o.rotDeg[2], 0));
        assert(near(o.posM[0], 0.05f));
    }
    // centre is subtracted before mapping
    {
        RawPose center{1, 2, 3, 10, 5, 0};
        RawPose p{1, 2, 3, 25, 5, 0};
        auto o = mapPose(p, center, defaults());
        assert(near(o.rotDeg[0], 15) && near(o.rotDeg[1], 0) && near(o.posM[0], 0));
    }
    // axis reordering
    {
        MapCfg c = defaults();
        c.rotOrder[0] = 1; c.rotOrder[1] = 0; c.rotOrder[2] = 2;
        c.posOrder[0] = 2; c.posOrder[1] = 1; c.posOrder[2] = 0;
        RawPose p{5, 0, -7, 20, 8, 0};
        auto o = mapPose(p, zero, c);
        assert(near(o.rotDeg[1], 20) && near(o.rotDeg[0], -8));
        assert(near(o.posM[2], 0.05f) && near(o.posM[0], -0.07f));
    }
    // position disabled
    {
        MapCfg c = defaults(); c.positionEnabled = false;
        auto o = mapPose(RawPose{10, 10, 10, 0, 0, 0}, zero, c);
        assert(o.posM[0] == 0 && o.posM[1] == 0 && o.posM[2] == 0);
    }
    // smoothing: 0 = passthrough, more smoothing = slower, converges
    {
        assert(smoothFactor(0.0, 0.004) == 1.0);
        double a1 = smoothFactor(0.2, 0.004), a2 = smoothFactor(0.8, 0.004);
        assert(a1 > a2 && a2 > 0.0 && a1 < 1.0);
        RawPose s; RawPose target{0, 0, 0, 10, 0, 0};
        for (int i = 0; i < 1000; ++i) smoothPose(s, target, a2);   // 4 s at 250 Hz
        assert(std::fabs(s.yaw - 10) < 0.01);
    }
    // per-side yaw gain
    {
        MapCfg c = defaults();
        c.yawLeftGain = 2.0f;                        // left = positive by default
        auto o = mapPose(RawPose{0, 0, 0, 20, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], 40));
        o = mapPose(RawPose{0, 0, 0, -20, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], -20));              // right side untouched
        o = mapPose(RawPose{0, 0, 0, 100, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], 150));              // limit still applies after the side gain
        o = mapPose(RawPose{0, 0, 0, 0, 20, 10}, zero, c);
        assert(near(o.rotDeg[1], -20) && near(o.rotDeg[2], 10));   // pitch/roll unchanged
        c.yawLeftIsPositive = false;                 // swapped: positive yaw is "right"
        o = mapPose(RawPose{0, 0, 0, 20, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], 20));
        o = mapPose(RawPose{0, 0, 0, -20, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], -40));
        c.yawRightGain = 0.5f;
        o = mapPose(RawPose{0, 0, 0, 20, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], 10));
        // gain/invert are applied before the side is decided
        c = defaults(); c.yaw.invert = true; c.yawLeftGain = 2.0f;
        o = mapPose(RawPose{0, 0, 0, -20, 0, 0}, zero, c);
        assert(near(o.rotDeg[0], 40));
    }
    // the game's rotation case multiplies by 180/pi: input 1 deg-radian must round-trip to 1.0
    {
        float r = 1.0f * 0.017453292519943295f;
        float back = r * 180.0f * 0.31830987334251404f;   // constants read from the game
        assert(near(back, 1.0f, 1e-5f));
    }
    std::puts("all pose tests passed");
    return 0;
}

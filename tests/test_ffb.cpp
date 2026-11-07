// SPDX-License-Identifier: MIT
// Host-side unit tests for the force feedback model (g++ -std=c++17 tests/test_ffb.cpp src/dll/wheel_ffb.cpp).
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/dll/wheel_ffb.h"

using namespace fh2;

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

static FfbParams live(bool race) { FfbParams p; p.stale = false; p.race = race; return p; }

int main() {
    // 1. stale
    {
        FfbParams p; p.menuSpringScale = 0.5f; p.torque = 0.3f; p.race = true;   // stale defaults true
        FfbTerms t{1, 1, 1, 1, 1, 1};
        assert(FfbForce(p, 0.4f, 1.0f, 0, &t) == 0.0f);
        assert(t.torque == 0 && t.spring == 0 && t.damping == 0 && t.rumble == 0 && t.menu == 0 && t.total == 0);
    }
    // 2. menu
    {
        FfbParams p = live(false); p.menuSpringScale = 0.5f;
        assert(near(FfbForce(p, 0.4f, 0, 0, nullptr), -0.2f));
        assert(near(FfbForce(p, 2.0f, 0, 0, nullptr), -0.5f));
    }
    // 3. torque only
    {
        FfbParams p = live(true); p.torque = 0.08f;
        assert(near(FfbForce(p, 0, 0, 0, nullptr), 0.08f));
    }
    // 4. spring clamp
    {
        FfbParams p = live(true); p.raceSpringScale = 0.04f; p.raceSpringMax = 0.5f;
        assert(near(FfbForce(p, 1.0f, 0, 0, nullptr), -0.04f));
        p.raceSpringScale = 2.0f; p.raceSpringMax = 0.3f;
        assert(near(FfbForce(p, 1.0f, 0, 0, nullptr), -0.3f));
        assert(near(FfbForce(p, -1.0f, 0, 0, nullptr), 0.3f));
    }
    // 5. damping
    {
        FfbParams p = live(true); p.dampScale = 0.33f; p.dampMax = 0.31f;
        assert(near(FfbForce(p, 0, 0.5f, 0, nullptr), -0.165f));
        assert(near(FfbForce(p, 0, 10.0f, 0, nullptr), -0.31f));
    }
    // 6. rumble
    {
        FfbParams p = live(true); p.leftAmp = 0.1f; p.leftWl = 40.0f;
        assert(near(FfbForce(p, 0, 0, 10.0, nullptr), 0.1f));
        assert(near(FfbForce(p, 0, 0, 30.0, nullptr), -0.1f));
        p.leftWl = 0;
        assert(near(FfbForce(p, 0, 0, 10.0, nullptr), 0.0f));
    }
    // 7. total clamp
    {
        FfbParams p = live(true); p.torque = 0.9f;
        assert(near(FfbForce(p, 0, 0, 0, nullptr), 0.5f));
    }
    // 9. torque gain
    {
        FfbParams p = live(true); p.torque = 0.1f;
        FfbTuning t; t.slipGain = 0;
        assert(near(FfbForce(p, t, 0, 0, 0, nullptr), 0.2f));
        t.torqueGain = 1.0f;
        assert(near(FfbForce(p, t, 0, 0, 0, nullptr), 0.1f));
    }
    // 10. game-driven spring with slip floor
    auto game = [] {
        FfbParams p = live(true);
        p.haveGame = true; p.raceSpringMax = 0.5f; p.raceSpringScale = 0.9f;
        GameFfbInput& g = p.game;
        g.speed = 20; g.slip = 0.6f; g.gain = 1; g.springParam = 0.5f;
        g.slip0 = 0.32f; g.slip1 = 0.55f; g.slipScale0 = 1; g.slipScale1 = 0;
        g.speed0 = 2; g.speed1 = 15; g.speedScale0 = 0.15f; g.speedScale1 = 1;
        return p;
    };
    {
        FfbParams p = game();
        FfbTuning t; t.torqueGain = 1; t.slipGain = 0; t.slipSpringFloor = 0.4f;
        FfbTerms tm{};
        FfbForce(p, t, 0.5f, 0, 0, &tm);
        assert(near(tm.spring, -0.1f));
        t.slipSpringFloor = 0;                       // the game removes the spring in a slide
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), 0.0f));
        p.game.slip = 0.1f; p.game.speed = 1.0f;      // slow, no slip: speedF clamps to 0.15
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.15f * 1.0f * 0.5f) * 0.5f));
        t.springGain = 2.0f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.15f * 0.5f) * 2.0f * 0.5f));
        // swapped / equal points
        p.game.speed = 8.5f; p.game.speed0 = 15; p.game.speed1 = 2; p.game.speedScale0 = 1; p.game.speedScale1 = 0.15f;
        t.springGain = 1.0f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.575f * 0.5f) * 0.5f));
        p.game.speed0 = p.game.speed1 = 5; p.game.speedScale0 = 0.3f; p.game.speedScale1 = 0.8f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.8f * 0.5f) * 0.5f));
    }
    // 11. counter-steer slip term
    {
        FfbParams p = game();
        FfbTuning t; t.torqueGain = 1; t.slipSpringFloor = 0; t.slipGain = 0.5f;
        t.slipInvert = false; t.alignGain = 0; t.rumbleGain = 1;
        p.game.slip = 0.315f;
        FfbTerms tm{};
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.slip, 0.125f));
        p.game.slip = -0.315f;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.slip, -0.125f));
        t.slipInvert = true;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.slip, 0.125f));
        t.slipInvert = false;
        p.game.slip = 0.05f;                         // below start
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.slip == 0.0f);
        p.game.slip = 2.0f;                          // saturates at gain*0.5
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.slip, 0.25f));
        t.slipFull = t.slipStart;                    // degenerate range
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.slip == 0.0f);
        t.slipFull = 0.55f; t.slipGain = 0;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.slip == 0.0f);
        // total includes the slip term
        t.slipGain = 0.5f; p.game.slip = 0.315f;
        assert(near(FfbForce(p, t, 0, 0, 0, nullptr), 0.125f));
    }
    // 13. front-slip aligning force
    {
        FfbParams p = game();
        FfbTuning t; t.torqueGain = 1; t.slipSpringFloor = 0; t.slipGain = 0; t.rumbleGain = 1;
        t.alignGain = 0.6f; t.alignStart = 0.05f; t.alignFull = 0.5f; t.slipInvert = true;
        p.game.haveFront = true; p.game.frontSlip = 0.275f;
        FfbTerms tm{};
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.align, -0.15f));
        assert(near(FfbForce(p, t, 0, 0, 0, nullptr), -0.15f));       // included in the total
        t.slipInvert = false;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.align, 0.15f));
        p.game.frontSlip = -0.275f;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.align, -0.15f));
        p.game.frontSlip = 0.02f;                                      // below start
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.align == 0.0f);
        p.game.frontSlip = 3.0f;                                       // saturates at gain*0.5
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(near(tm.align, 0.3f));
        p.game.frontSlip = 0.275f;
        p.game.haveFront = false;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.align == 0.0f);
        p.game.haveFront = true; t.alignGain = 0;
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.align == 0.0f);
        t.alignGain = 0.6f; t.alignFull = t.alignStart;                // degenerate range
        FfbForce(p, t, 0, 0, 0, &tm);
        assert(tm.align == 0.0f);
    }
    // 14. rumble gain
    {
        FfbParams p = live(true); p.leftAmp = 0.1f; p.leftWl = 40.0f;
        FfbTuning t; t.torqueGain = 1; t.rumbleGain = 1;
        FfbTerms tm{};
        FfbForce(p, t, 0, 0, 10.0, &tm);
        assert(near(tm.rumble, 0.1f));
        t.rumbleGain = 2;
        FfbForce(p, t, 0, 0, 10.0, &tm);
        assert(near(tm.rumble, 0.2f));
        t.rumbleGain = 0;
        FfbForce(p, t, 0, 0, 10.0, &tm);
        assert(tm.rumble == 0.0f);
    }
    // 12. no game input: fall back to the captured spring scale; no slip term
    {
        FfbParams p = game();
        p.haveGame = false;
        FfbTuning t; t.torqueGain = 1; t.slipSpringFloor = 0.4f; t.slipGain = 0.5f; t.springGain = 1.0f;
        FfbTerms tm{};
        FfbForce(p, t, 0.5f, 0, 0, &tm);
        assert(near(tm.spring, -0.45f) && tm.slip == 0.0f);
        t.springGain = 0.5f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -0.225f));
    }

    // 8. device magnitude
    assert(FfbDeviceMagnitude(0.25f, 1.0f, false) == -5000);
    assert(FfbDeviceMagnitude(0.25f, 1.0f, true) == 5000);
    assert(FfbDeviceMagnitude(0.5f, 2.0f, false) == -10000);
    assert(FfbDeviceMagnitude(-0.1f, 1.0f, false) == 2000);

    std::puts("all ffb tests passed");
    return 0;
}

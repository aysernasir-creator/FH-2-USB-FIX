// SPDX-License-Identifier: MIT
// Host-side unit tests for the steering lock math (g++ -std=c++17 tests/test_lock.cpp src/dll/wheel_ffb.cpp).
#include <cmath>
#include <cstdio>
#include "../src/dll/steer_lock.h"

using namespace fh2;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

int main() {
    LockInputs in;
    in.wheelRange = 900; in.maxRotation = 900; in.perCar = true; in.gameRotation = 900; in.maxCarLock = 1620;

    // per-car 540, cap: the wheel turns 540
    in.carLock = 540;
    CHECK(near(PhysicalLock(in), 540) && near(DisplayLock(in), 540));
    // per-car 1080, cap, max 900: wheel 900, display 1080
    in.carLock = 1080;
    CHECK(near(PhysicalLock(in), 900) && near(DisplayLock(in), 1080));
    // scale: 540 -> 300, 1620 -> 900
    in.scale = true;
    in.carLock = 540;  CHECK(near(PhysicalLock(in), 300) && near(DisplayLock(in), 540));
    in.carLock = 1620; CHECK(near(PhysicalLock(in), 900) && near(DisplayLock(in), 1620));
    in.maxCarLock = 0; in.carLock = 1080; CHECK(near(PhysicalLock(in), 900));       // no list maximum: cap
    in.maxCarLock = 1620; in.scale = false;
    // per-car off: the game's rotation, limited by the maximum
    in.perCar = false; in.carLock = 540; in.gameRotation = 900; in.maxRotation = 720;
    CHECK(near(PhysicalLock(in), 720) && near(DisplayLock(in), 720));
    // max 0 -> WheelRange (per-car on, car unknown)
    in.perCar = true; in.carLock = 0; in.maxRotation = 0; in.gameRotation = 1500;
    CHECK(near(PhysicalLock(in), 900) && near(DisplayLock(in), 900));
    // nothing configured: exactly the game's rotation (0.9.5 behaviour), even above WheelRange
    in.perCar = false; in.gameRotation = 1080;
    CHECK(near(PhysicalLock(in), 1080));
    in.gameRotation = 10; CHECK(near(PhysicalLock(in), 40));                          // never below 40
    // 0.9.5 formula steering * WheelRange / rotation is reproduced exactly
    in.gameRotation = 450;
    CHECK(GameSteer(in, 0.25f) == 0.25f * 900.0f / 450.0f);
    in.gameRotation = 1080;
    CHECK(GameSteer(in, 0.3f) == 0.3f * 900.0f / 1080.0f);
    // physDeg 135 with physical lock 540 -> 0.5; clamped at +-1
    in.perCar = true; in.carLock = 540; in.maxRotation = 900;
    CHECK(near(GameSteer(in, 135.0f / 450.0f), 0.5f));
    CHECK(GameSteer(in, 1.0f) == 1.0f && GameSteer(in, -1.0f) == -1.0f);

    // soft lock (physical lock 540 on a 900 wheel: limit 270 degrees each way)
    CHECK(ApplySoftLock(1234, 280, 540, 900, 1.0f, 3.0f, false) == 10000);   // k 1: full restoring force (toward centre)
    CHECK(ApplySoftLock(1234, -280, 540, 900, 1.0f, 3.0f, false) == -10000);
    CHECK(ApplySoftLock(1234, 280, 540, 900, 1.0f, 3.0f, true) == -10000);
    CHECK(near(SoftLockK(271.5f, 540, 900, 1.0f, 3.0f), 0.5f));
    CHECK(ApplySoftLock(0, 271.5f, 540, 900, 1.0f, 3.0f, false) == 5000);    // 0.5 * 10000
    CHECK(ApplySoftLock(2000, 271.5f, 540, 900, 1.0f, 3.0f, false) == 6000);
    CHECK(ApplySoftLock(1234, 200, 540, 900, 1.0f, 3.0f, false) == 1234);    // inside the lock
    CHECK(ApplySoftLock(1234, 280, 900, 900, 1.0f, 3.0f, false) == 1234);    // lock == wheel range: the wheel's own stop
    CHECK(ApplySoftLock(1234, 280, 540, 900, 0.0f, 3.0f, false) == 1234);    // strength 0: off
    CHECK(ApplySoftLock(0, 280, 540, 900, 0.5f, 3.0f, false) == 5000);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("all lock tests passed");
    return 0;
}

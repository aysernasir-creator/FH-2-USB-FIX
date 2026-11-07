// SPDX-License-Identifier: MIT
// Head tracker input: publishes the latest camera offsets for the game thread.
#pragma once
#include "config.h"
#include "pose.h"

namespace fh2 {

// Receives OpenTrack UDP, applies centring/smoothing/mapping on its own thread and
// publishes the latest CameraOffsets for the game thread to read cheaply.
class Tracker {
public:
    static void start(const Config& cfg);
    // Called from the game's camera thread. Returns false when tracking is off or stale.
    static bool read(CameraOffsets& out);
};

} // namespace fh2

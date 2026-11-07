// SPDX-License-Identifier: MIT
// Head-tracking hook: waits for the game image and redirects its camera inputs.
#pragma once
#include "config.h"

namespace fh2 {

// Waits for the game image to be mapped into this (azimuth.exe) process, verifies
// the head-tracking camera input code, and redirects it to the tracker.
// Returns false if the game build is not the one this plugin knows.
bool installWhenReady(const Config& cfg);

} // namespace fh2

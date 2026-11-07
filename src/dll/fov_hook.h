// SPDX-License-Identifier: MIT
// Field-of-view adjustment for the cockpit, driver, hood and bumper cameras.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
void FovHook_Install(uintptr_t base, const Config& cfg);

// GetTickCount64() of the last call of the cockpit camera's GetFOV (0 = never); only maintained with
// HideDriverInCockpit or FovCockpit set.
uint64_t FovHook_LastCockpitTick();

} // namespace fh2

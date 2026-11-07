// SPDX-License-Identifier: MIT
// Hooks that present the virtual wheel to Forza Horizon 2 (IAT + controller list + rescan).
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
void WheelHook_Install(uintptr_t base, const Config& cfg);

} // namespace fh2

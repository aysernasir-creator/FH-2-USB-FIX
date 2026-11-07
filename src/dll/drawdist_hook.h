// SPDX-License-Identifier: MIT
// Draw distance: raises the game's own shadow, world-object, prop, crowd, grass, traffic and
// racer-car detail distances (never lowers them). Values are metres, 0 = leave the game value.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
// Installs only the parts whose setting is non-zero.
void DrawDist_Install(uintptr_t base, const Config& cfg);

} // namespace fh2

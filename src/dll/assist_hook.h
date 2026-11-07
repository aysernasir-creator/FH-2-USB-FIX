// SPDX-License-Identifier: MIT
// Realistic ABS / traction control / stability control / clutch / automatic shifting: overrides the game's LocalPlayerChoices getters per car.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
// Does nothing unless RealisticABS, RealisticTC, RealisticSTM, RealisticClutch or RealisticAutomatic is on.
void AssistHook_Install(uintptr_t base, const Config& cfg);

// Records the car ordinal of the local player's car (called from the FFB call-site hook, game thread).
void AssistHook_SetPlayerCar(uint32_t ordinal);

} // namespace fh2

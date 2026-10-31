// A patient player for the tests (and the page's demo hook): walks, never
// hurries, gathers every ghost, flares at wisps, and walks home.
#pragma once

#include "sim/world.h"

namespace gl {

Input autopilot(const World& w);

// Plays the current level with the autopilot until it is done or `seconds`
// of game time pass. Returns true if every ghost got home.
bool playLevel(World& w, float seconds);

}  // namespace gl

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "presets/Variation.h"

namespace spaceforge
{
MacroKnobs rollMacros (const MacroKnobs& from, juce::Random& random)
{
    // Even draws over the whole knob, redrawn until at least two knobs moved enough. Most draws
    // pass at once; a forced fallback after many tries keeps the loop bounded.
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        MacroKnobs to {};
        int moved = 0;
        for (size_t m = 0; m < to.size(); ++m)
        {
            to[m] = random.nextFloat();
            if (std::abs (to[m] - from[m]) >= kMinMove)
                ++moved;
        }
        if (moved >= 2)
            return to;
    }
    // fallback: push two knobs to the far side of their travel
    auto to = from;
    for (size_t m = 0; m < 2; ++m)
        to[m] = from[m] < 0.5f ? 1.0f : 0.0f;
    return to;
}

int rollMode (int from, int numModes, juce::Random& random)
{
    if (numModes < 2)
        return from;
    const int other = random.nextInt (numModes - 1);
    return other >= from ? other + 1 : other;
}
} // namespace spaceforge

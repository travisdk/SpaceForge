// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <deque>

// The dice (from SpaceForge 1): moves the four macros to new random positions over their whole
// travel, so you see and hear what happened. Mode and Mix are never touched (CLAUDE.md).
namespace spaceforge
{
using MacroKnobs = std::array<float, 4>; // Space, Echo, Motion, Tone (0..1)

// New positions. At least two knobs move by at least kMinMove of their travel, so a roll is
// always noticeable.
inline constexpr float kMinMove = 0.2f;
MacroKnobs rollMacros (const MacroKnobs& from, juce::Random& random);

// Undo for the last rolls: the knobs before each. At most kDepth steps; the oldest falls off.
class VariationHistory
{
public:
    static constexpr size_t kDepth = 10;

    void push (const MacroKnobs& knobs)
    {
        steps.push_back (knobs);
        if (steps.size() > kDepth)
            steps.pop_front();
    }
    bool canUndo() const { return ! steps.empty(); }
    MacroKnobs pop()
    {
        const auto knobs = steps.back();
        steps.pop_back();
        return knobs;
    }
    void clear() { steps.clear(); }
    size_t size() const { return steps.size(); }

private:
    std::deque<MacroKnobs> steps;
};
} // namespace spaceforge

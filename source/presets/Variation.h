// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <deque>

// The dice (from SpaceForge 1): moves the four macros to new random positions over their whole
// travel, so you see and hear what happened; with the Mode lock open (or Shift) also to another
// Mode. Mix is never touched (CLAUDE.md).
namespace spaceforge
{
using MacroKnobs = std::array<float, 4>; // Space, Echo, Motion, Tone (0..1)

// New positions. At least two knobs move by at least kMinMove of their travel, so a roll is
// always noticeable.
inline constexpr float kMinMove = 0.2f;
MacroKnobs rollMacros (const MacroKnobs& from, juce::Random& random);

// Another Mode than `from`, evenly among the others.
int rollMode (int from, int numModes, juce::Random& random);

// What a roll changes: the four knobs and the Mode.
struct RollStep
{
    MacroKnobs knobs {};
    int mode = 0;
};

// Undo for the last rolls: what was there before each. At most kDepth steps; the oldest falls off.
class VariationHistory
{
public:
    static constexpr size_t kDepth = 10;

    void push (const RollStep& step)
    {
        steps.push_back (step);
        if (steps.size() > kDepth)
            steps.pop_front();
    }
    bool canUndo() const { return ! steps.empty(); }
    RollStep pop()
    {
        const auto step = steps.back();
        steps.pop_back();
        return step;
    }
    void clear() { steps.clear(); }
    size_t size() const { return steps.size(); }

private:
    std::deque<RollStep> steps;
};
} // namespace spaceforge

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "presets/Preset.h"

// The sorting of preset candidates (presets/candidates, made by sf2_candidates): Henrik hears one
// at a time and keeps it (K) or skips it (S); back undoes. Every decision is saved at once to
// review.json in the same folder, so sorting can stop and go on later where it left off. Only
// the kept ones become factory presets. Message thread only.
class CandidateSorter
{
public:
    enum class Decision
    {
        none,
        keep,
        skip
    };

    explicit CandidateSorter (const juce::File& folder);

    int size() const { return (int) candidates.size(); }
    // The candidate being heard; size() when every one is decided.
    int position() const { return pos; }
    bool finished() const { return pos >= size(); }
    const spaceforge::Preset* current() const { return finished() ? nullptr : &candidates[(size_t) pos]; }
    juce::String idOf (int index) const; // the file name without .json, e.g. "0-Sky-03"
    Decision decisionOf (int index) const;
    int count (Decision d) const;

    // Decides the current one and moves on to the next undecided.
    void decide (Decision d);
    // Back to the last one decided (in this session, else the one before), undecided again.
    bool undo();

    juce::File reviewFile() const { return folder.getChildFile ("review.json"); }

    static constexpr int kMaxCandidates = 1000;

private:
    void save() const;
    void moveToNextUndecided (int from);

    juce::File folder;
    std::vector<spaceforge::Preset> candidates;
    std::vector<Decision> decisions;
    std::vector<int> history;
    int pos = 0;
};

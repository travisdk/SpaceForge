// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "presets/CandidateSorter.h"

using namespace spaceforge;

CandidateSorter::CandidateSorter (const juce::File& f) : folder (f)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
    files.removeIf ([] (const juce::File& file) { return file.getFileName() == "review.json"; });
    files.sort(); // "<Mode number>-<Mode>-<nn>": by Mode, then as generated
    for (const auto& file : files)
        if (auto preset = presets::fromFile (file); preset && (int) candidates.size() < kMaxCandidates)
            candidates.push_back (*preset);
    decisions.assign (candidates.size(), Decision::none);

    // Earlier decisions: {"0-Sky-03": "keep", ...}; anything else in the file is ignored.
    const auto review = reviewFile();
    if (review.existsAsFile() && review.getSize() <= presets::kMaxFileBytes * 4)
    {
        const auto root = juce::JSON::parse (review.loadFileAsString());
        for (int i = 0; i < size(); ++i)
        {
            const auto value = root.getProperty (idOf (i), {}).toString();
            decisions[(size_t) i] = value == "keep" ? Decision::keep : value == "skip" ? Decision::skip : Decision::none;
        }
    }
    moveToNextUndecided (0);
}

juce::String CandidateSorter::idOf (int index) const
{
    return juce::isPositiveAndBelow (index, size()) ? candidates[(size_t) index].file.getFileNameWithoutExtension() : juce::String();
}

CandidateSorter::Decision CandidateSorter::decisionOf (int index) const
{
    return juce::isPositiveAndBelow (index, size()) ? decisions[(size_t) index] : Decision::none;
}

int CandidateSorter::count (Decision d) const
{
    return (int) std::count (decisions.begin(), decisions.end(), d);
}

void CandidateSorter::moveToNextUndecided (int from)
{
    pos = from;
    while (pos < size() && decisions[(size_t) pos] != Decision::none)
        ++pos;
}

void CandidateSorter::decide (Decision d)
{
    if (finished() || d == Decision::none)
        return;
    decisions[(size_t) pos] = d;
    history.push_back (pos);
    save();
    moveToNextUndecided (pos + 1);
}

bool CandidateSorter::undo()
{
    int back = -1;
    if (! history.empty())
    {
        back = history.back();
        history.pop_back();
    }
    else if (pos > 0)
        back = pos - 1;
    if (back < 0)
        return false;
    decisions[(size_t) back] = Decision::none;
    pos = back;
    save();
    return true;
}

void CandidateSorter::save() const
{
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);
    for (int i = 0; i < size(); ++i)
        if (decisions[(size_t) i] != Decision::none)
            root->setProperty (idOf (i), decisions[(size_t) i] == Decision::keep ? "keep" : "skip");
    reviewFile().replaceWithText (juce::JSON::toString (rootVar, false, 2) + "\n");
}

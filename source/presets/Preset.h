// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include "dsp/Macros.h"
#include "plugin/Defaults.h"

// A preset as stored in a JSON file: its name and the visible controls (Mode, the four macros,
// Mix, and the small Echo and Duck with Echo's fixed note, which is shown as text by the knob).
// Nothing else, so what you hear can always be read off the controls (
// "Intet skjult"). The format (adapted from SpaceForge 1):
//
//   { "name": "Init", "mode": "Sky", "space": 0.95, "reflect": 0, "motion": 0.5, "tone": 0.5,
//     "mix": 0.4, "echo": 0, "echoNote": "1/8d", "duck": 0 }
// (Every key is named after its knob. The last three are left out of older files: they mean
// 0, 1/8d, 0.)
// (Freeze is out for now, kept on the branch feature/freeze; a "freeze" entry in older files is
// ignored.)
namespace spaceforge
{
struct Preset
{
    juce::String name { "Init" };
    int mode = 0;
    float space = dsp::MacroPositions::kInitSpace, reflect = dsp::MacroPositions::kInitReflect;
    float motion = dsp::MacroPositions::kInitMotion, tone = dsp::MacroPositions::kInitTone, mix = plugin::kDefaultMix;
    float delay = dsp::MacroPositions::kInitDelay, duck = dsp::MacroPositions::kInitDuck;
    int delayNote = dsp::MacroPositions::kInitDelayNote;

    // Where it came from (not stored in the file).
    bool isFactory = false;
    juce::File file; // user presets and candidates

    bool sameSound (const Preset& other) const;
    // For favourites: "factory:<name>" or "user:<name>".
    juce::String key() const { return juce::String (isFactory ? "factory:" : "user:") + name; }
};

namespace presets
{
// Larger files are refused unread (a preset is a few hundred bytes).
inline constexpr juce::int64 kMaxFileBytes = 64 * 1024;
inline constexpr int kMaxNameLength = 64;

// Reads a preset. Nothing if the text is not one; any value that is missing, not a finite number
// or out of range falls back to Init's, and an unknown Mode to the first.
std::optional<Preset> fromJson (const juce::String& text);
std::optional<Preset> fromFile (const juce::File& file);
juce::String toJson (const Preset&);

// A name safe to show and to use as a file name: no path separators, control characters or
// leading dots, at most kMaxNameLength characters; empty if nothing is left.
juce::String sanitiseName (const juce::String& name);

Preset makeInit();
} // namespace presets
} // namespace spaceforge

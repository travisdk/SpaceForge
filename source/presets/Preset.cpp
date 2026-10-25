// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "presets/Preset.h"
#include "dsp/Modes.h"

namespace spaceforge
{
bool Preset::sameSound (const Preset& o) const
{
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.005f; };
    return mode == o.mode && near (space, o.space) && near (reflect, o.reflect) && near (motion, o.motion) && near (tone, o.tone) && near (mix, o.mix)
           && near (delay, o.delay) && near (duck, o.duck) && delayNote == o.delayNote;
}

namespace presets
{
namespace
{
// A number rounded for writing, so files say 0.3 and not 0.30000001192092896 (SpaceForge 1).
juce::var tidy (float value)
{
    return juce::var (std::round ((double) value * 1000.0) / 1000.0);
}

// A knob position: only a finite number in 0..1, anything else keeps the fallback.
float unit (const juce::var& v, float fallback)
{
    if (! (v.isDouble() || v.isInt() || v.isInt64()))
        return fallback;
    const double x = v;
    return std::isfinite (x) ? (float) std::clamp (x, 0.0, 1.0) : fallback;
}
} // namespace

juce::String sanitiseName (const juce::String& name)
{
    juce::String clean;
    for (auto p = name.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c >= 0x20 && c != 0x7f && juce::String ("/\\:*?\"<>|").indexOfChar (c) < 0)
            clean += juce::String::charToString (c);
    }
    clean = clean.trim().trimCharactersAtStart (".").trim();
    return clean.substring (0, kMaxNameLength).trim();
}

std::optional<Preset> fromJson (const juce::String& text)
{
    if (text.getNumBytesAsUTF8() > (size_t) kMaxFileBytes)
        return std::nullopt;
    juce::var root;
    if (juce::JSON::parse (text, root).failed() || ! root.isObject())
        return std::nullopt;

    Preset p = makeInit();
    p.isFactory = false;
    p.name = sanitiseName (root.getProperty ("name", "").toString());
    if (p.name.isEmpty())
        return std::nullopt;
    const auto mode = root.getProperty ("mode", "").toString();
    p.mode = 0;
    for (int m = 0; m < dsp::kNumModes; ++m)
        if (mode == dsp::modeSetup (m).name)
            p.mode = m;
    p.space = unit (root.getProperty ("space", {}), p.space);
    p.reflect = unit (root.getProperty ("echo", {}), p.reflect);
    p.motion = unit (root.getProperty ("motion", {}), p.motion);
    p.tone = unit (root.getProperty ("tone", {}), p.tone);
    p.mix = unit (root.getProperty ("mix", {}), p.mix);
    p.delay = unit (root.getProperty ("delay", {}), p.delay);
    p.duck = unit (root.getProperty ("duck", {}), p.duck);
    const auto note = root.getProperty ("delayNote", "").toString();
    for (int n = 0; n < dsp::kNumDelayNotes; ++n)
        if (note == dsp::delayNoteName (n))
            p.delayNote = n;
    return p;
}

std::optional<Preset> fromFile (const juce::File& file)
{
    if (! file.existsAsFile() || file.getSize() > kMaxFileBytes)
        return std::nullopt;
    auto preset = fromJson (file.loadFileAsString());
    if (preset)
        preset->file = file;
    return preset;
}

juce::String toJson (const Preset& p)
{
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);
    root->setProperty ("name", sanitiseName (p.name));
    root->setProperty ("mode", dsp::modeSetup (p.mode).name);
    root->setProperty ("space", tidy (p.space));
    root->setProperty ("echo", tidy (p.reflect));
    root->setProperty ("motion", tidy (p.motion));
    root->setProperty ("tone", tidy (p.tone));
    root->setProperty ("mix", tidy (p.mix));
    root->setProperty ("delay", tidy (p.delay));
    root->setProperty ("delayNote", dsp::delayNoteName (p.delayNote));
    root->setProperty ("duck", tidy (p.duck));
    return juce::JSON::toString (rootVar, false, 4) + "\n";
}

Preset makeInit()
{
    Preset p;
    p.isFactory = true;
    return p;
}
} // namespace presets
} // namespace spaceforge

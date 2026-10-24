// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "presets/PresetLibrary.h"
#include "SpaceForgePresets.h"

using namespace spaceforge;

PresetLibrary::PresetLibrary()
{
    reload();
}

std::vector<Preset> PresetLibrary::loadFactory()
{
    std::vector<Preset> result;
    for (int i = 0; i < SpaceForgePresets::namedResourceListSize; ++i)
    {
        int size = 0;
        const char* data = SpaceForgePresets::getNamedResource (SpaceForgePresets::namedResourceList[i], size);
        if (data == nullptr)
            continue;
        if (auto preset = presets::fromJson (juce::String::fromUTF8 (data, size)))
        {
            preset->isFactory = true;
            result.push_back (std::move (*preset));
        }
    }
    // Init first, then by Mode, then by name
    std::sort (result.begin(), result.end(), [] (const Preset& a, const Preset& b) {
        const bool initA = a.name == "Init", initB = b.name == "Init";
        if (initA != initB)
            return initA;
        if (a.mode != b.mode)
            return a.mode < b.mode;
        return a.name.compareNatural (b.name) < 0;
    });
    return result;
}

juce::File PresetLibrary::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SpaceForge2").getChildFile ("Presets");
}

void PresetLibrary::reload()
{
    presets = loadFactory();
    if (presets.empty() || presets.front().name != "Init")
        presets.insert (presets.begin(), presets::makeInit());

    std::vector<Preset> user;
    for (const auto& file : userFolder().findChildFiles (juce::File::findFiles, false, "*.json"))
        if (auto preset = presets::fromFile (file); preset && (int) user.size() < kMaxUserPresets)
            user.push_back (std::move (*preset));
    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    presets.insert (presets.end(), user.begin(), user.end());
}

const Preset* PresetLibrary::at (int index) const
{
    return juce::isPositiveAndBelow (index, size()) ? &presets[(size_t) index] : nullptr;
}

int PresetLibrary::indexOf (const juce::String& name) const
{
    for (int i = 0; i < size(); ++i)
        if (presets[(size_t) i].name == name)
            return i;
    return -1;
}

int PresetLibrary::saveUserPreset (const Preset& preset)
{
    Preset p = preset;
    p.name = presets::sanitiseName (p.name);
    if (p.name.isEmpty() || ! userFolder().createDirectory())
        return -1;
    // The sanitised name cannot leave the folder; checked anyway.
    const auto file = userFolder().getChildFile (juce::File::createLegalFileName (p.name) + ".json");
    if (! file.isAChildOf (userFolder()) || ! file.replaceWithText (presets::toJson (p)))
        return -1;
    reload();
    for (int i = 0; i < size(); ++i)
        if (presets[(size_t) i].file == file)
            return i;
    return -1;
}

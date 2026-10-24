// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "presets/PresetLibrary.h"
#include "SpaceForgePresets.h"
#include "dsp/Modes.h"

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

int PresetLibrary::indexOfKey (const juce::String& key) const
{
    for (int i = 0; i < size(); ++i)
        if (presets[(size_t) i].key() == key)
            return i;
    return -1;
}

juce::String PresetLibrary::categoryOf (const Preset& p)
{
    if (! p.isFactory)
        return "User";
    return p.name == "Init" ? juce::String ("Init") : juce::String (dsp::modeSetup (p.mode).name);
}

juce::StringArray PresetLibrary::categories() const
{
    juce::StringArray result;
    for (int m = 0; m < dsp::kNumModes; ++m)
        for (const auto& p : presets)
            if (p.isFactory && p.mode == m && p.name != "Init")
            {
                result.add (dsp::modeSetup (m).name);
                break;
            }
    if (std::any_of (presets.begin(), presets.end(), [] (const Preset& p) { return ! p.isFactory; }))
        result.add ("User");
    return result;
}

std::vector<int> PresetLibrary::filter (const juce::String& query, const juce::String& category, bool favouritesOnly) const
{
    const auto words = juce::StringArray::fromTokens (query.toLowerCase(), " ", "\"");
    std::vector<int> result;
    for (int i = 0; i < size(); ++i)
    {
        const auto& p = presets[(size_t) i];
        if (category.isNotEmpty() && (categoryOf (p) != category || p.name == "Init"))
            continue;
        if (favouritesOnly && ! isFavourite (p.key()))
            continue;
        const auto haystack = (p.name + " " + categoryOf (p)).toLowerCase();
        bool matches = true;
        for (const auto& w : words)
            if (w.isNotEmpty() && ! haystack.contains (w))
                matches = false;
        if (matches)
            result.push_back (i);
    }
    return result;
}

void PresetLibrary::setFavourite (const juce::String& key, bool on)
{
    if (on)
        favourites.addIfNotAlreadyThere (key);
    else
        favourites.removeString (key);
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

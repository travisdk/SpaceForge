// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "presets/PresetLibrary.h"
#include "SpaceForgePresets.h"
#include "dsp/Modes.h"
#include "presets/PresetFiles.h"

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
    // Init first, then by Mode (in the order the Modes are shown), then by name
    std::sort (result.begin(), result.end(), [] (const Preset& a, const Preset& b) {
        const bool initA = a.name == "Init", initB = b.name == "Init";
        if (initA != initB)
            return initA;
        if (a.mode != b.mode)
            return dsp::modeDisplayPosition (a.mode) < dsp::modeDisplayPosition (b.mode);
        return a.name.compareNatural (b.name) < 0;
    });
    return result;
}

namespace
{
juce::File& userFolderOverride()
{
    static juce::File folder;
    return folder;
}
} // namespace

void PresetLibrary::setUserFolderForTests (const juce::File& folder)
{
    userFolderOverride() = folder;
}

juce::File PresetLibrary::userFolder()
{
    if (userFolderOverride() != juce::File())
        return userFolderOverride();
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SpaceForge").getChildFile ("Presets");
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
    for (const int m : dsp::kModeDisplayOrder)
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

int PresetLibrary::importFiles (const juce::Array<juce::File>& files, juce::StringArray& refused)
{
    int imported = 0;
    for (int i = 0; i < juce::jmin (files.size(), kMaxImportFiles); ++i)
    {
        auto preset = presets::fromFile (files[i]);
        if (! preset)
        {
            refused.add (files[i].getFileName());
            continue;
        }
        preset->isFactory = false;
        const auto base = preset->name;
        auto taken = [this] (const juce::String& name) {
            return std::any_of (presets.begin(), presets.end(), [&name] (const Preset& p) { return p.name.equalsIgnoreCase (name); });
        };
        for (int n = 2; taken (preset->name) && n < 1000; ++n)
            preset->name = presets::sanitiseName (base.substring (0, presets::kMaxNameLength - 6) + " " + juce::String (n));
        if (saveUserPreset (*preset) >= 0)
            ++imported;
        else
            refused.add (files[i].getFileName());
    }
    return imported;
}

bool PresetLibrary::exportPreset (const Preset& preset, const juce::File& file)
{
    return file.replaceWithText (presets::toJson (preset));
}

int PresetLibrary::renameUserPreset (int index, const juce::String& newName)
{
    const auto* p = at (index);
    const auto name = presets::sanitiseName (newName);
    if (p == nullptr || p->isFactory || name.isEmpty())
        return -1;
    Preset renamed = *p;
    renamed.name = name;
    const auto oldFile = p->file;
    const int saved = saveUserPreset (renamed);
    if (saved < 0)
        return -1;
    const auto newFile = presets[(size_t) saved].file;
    if (oldFile != newFile && oldFile.isAChildOf (userFolder()))
        oldFile.deleteFile();
    reload();
    for (int i = 0; i < size(); ++i)
        if (presets[(size_t) i].file == newFile)
            return i;
    return -1;
}

bool PresetLibrary::deleteUserPreset (int index)
{
    const auto* p = at (index);
    if (p == nullptr || p->isFactory || ! p->file.isAChildOf (userFolder()))
        return false;
    const bool ok = files::moveToTrash (p->file);
    reload();
    return ok;
}

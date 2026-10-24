// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "presets/Preset.h"

// Every preset the plug-in offers (adapted from SpaceForge 1): Init, the factory presets compiled
// into the plug-in (presets/factory, the candidates Henrik kept), and the user's own files in
// Documents/SpaceForge2/Presets. In order: Init, factory by Mode then name, user by name.
// Categories are the Modes plus "User"; search and favourites as in SpaceForge 1. Message
// thread only.
class PresetLibrary
{
public:
    PresetLibrary();

    // Reads the factory presets and the user folder again.
    void reload();

    const std::vector<spaceforge::Preset>& all() const { return presets; }
    int size() const { return (int) presets.size(); }
    const spaceforge::Preset* at (int index) const;
    // The first preset with this name (user presets may share a factory name), or -1.
    int indexOf (const juce::String& name) const;
    int indexOfKey (const juce::String& key) const;

    // The Modes that have presets, then "User" if there are user presets.
    juce::StringArray categories() const;
    // Indices of the presets matching a filter, in library order. Empty category = all. Every
    // word of the query must match the name or the category somewhere.
    std::vector<int> filter (const juce::String& query, const juce::String& category, bool favouritesOnly) const;
    static juce::String categoryOf (const spaceforge::Preset&);

    bool isFavourite (const juce::String& key) const { return favourites.contains (key); }
    void setFavourite (const juce::String& key, bool on);
    const juce::StringArray& getFavourites() const { return favourites; }
    void setFavourites (const juce::StringArray& keys) { favourites = keys; }

    // Saves the controls as a user preset (replacing one with the same name); returns its index
    // after reloading, or -1 if the name is unusable or the file could not be written.
    int saveUserPreset (const spaceforge::Preset& preset);

    // Reads preset files from elsewhere: each goes through the same checks as every preset
    // (size, values, name) and is written anew into the user folder, so nothing of the original
    // file but the checked values arrives. A name already taken gets " 2", " 3"... Returns how
    // many were imported; the names of the files that were not presets go into `refused`.
    int importFiles (const juce::Array<juce::File>& files, juce::StringArray& refused);
    // Writes a preset to any file (export); false if it could not be written.
    static bool exportPreset (const spaceforge::Preset& preset, const juce::File& file);
    // User presets only: a new name (the file follows), or into the trash. Return the new index /
    // success; factory presets are never changed.
    int renameUserPreset (int index, const juce::String& newName);
    bool deleteUserPreset (int index);
    static constexpr int kMaxImportFiles = 500;

    static juce::File userFolder();
    // The tests use a folder of their own instead of the user's documents.
    static void setUserFolderForTests (const juce::File& folder);
    static std::vector<spaceforge::Preset> loadFactory();

    static constexpr int kMaxUserPresets = 2000;

private:
    std::vector<spaceforge::Preset> presets;
    juce::StringArray favourites;
};

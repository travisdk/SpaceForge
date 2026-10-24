// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "presets/Preset.h"

// Every preset the plug-in offers (adapted from SpaceForge 1): Init, the factory presets compiled
// into the plug-in (presets/factory, the candidates Henrik kept), and the user's own files in
// Documents/SpaceForge2/Presets. In order: Init, factory by Mode then name, user by name.
// Message thread only.
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

    // Saves the controls as a user preset (replacing one with the same name); returns its index
    // after reloading, or -1 if the name is unusable or the file could not be written.
    int saveUserPreset (const spaceforge::Preset& preset);

    static juce::File userFolder();
    static std::vector<spaceforge::Preset> loadFactory();

    static constexpr int kMaxUserPresets = 2000;

private:
    std::vector<spaceforge::Preset> presets;
};

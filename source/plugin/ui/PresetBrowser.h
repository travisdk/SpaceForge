// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "presets/PresetLibrary.h"

#include <functional>

// The preset browser (from SpaceForge 1): a search field, the categories (All, Favourites, the
// Modes, User) and the presets in the chosen category. Clicking or arrowing onto a preset loads
// it at once; the heart on each row marks a favourite.
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PresetLibrary&);

    std::function<void (int libraryIndex)> onLoad;
    std::function<void (const juce::String& key, bool on)> onFavourite;

    // Rebuilds the lists (after a save or favourite change) and highlights the current preset.
    void refresh (int currentLibraryIndex);
    const std::vector<int>& visiblePresets() const { return visible; }
    void focusList() { list.grabKeyboardFocus(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int getNumRows() override { return (int) visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void returnKeyPressed (int) override {}

    void rebuild();
    juce::Rectangle<int> categoryArea() const;
    int categoryAt (juce::Point<int>) const;

    PresetLibrary& library;
    juce::TextEditor search;
    juce::ListBox list { "Presets", this };
    juce::StringArray categoryNames; // "All", "Favourites", then the library's
    int selectedCategory = 0;
    std::vector<int> visible;
    int current = -1;
    bool updatingSelection = false;

    static constexpr int kCategoryRow = 26, kCategoryWidth = 128;
};

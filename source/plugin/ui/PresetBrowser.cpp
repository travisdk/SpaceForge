// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/ui/PresetBrowser.h"
#include "plugin/ui/Theme.h"

using namespace spaceforge;

namespace
{
// A small heart, drawn rather than taken from a font so it looks the same everywhere.
void drawHeart (juce::Graphics& g, juce::Rectangle<float> area, bool filled, juce::Colour colour)
{
    const float s = juce::jmin (area.getWidth(), area.getHeight()) * 0.62f;
    const auto c = area.getCentre();
    juce::Path heart;
    heart.startNewSubPath (c.x, c.y + s * 0.45f);
    heart.cubicTo (c.x - s * 0.9f, c.y - s * 0.1f, c.x - s * 0.45f, c.y - s * 0.75f, c.x, c.y - s * 0.25f);
    heart.cubicTo (c.x + s * 0.45f, c.y - s * 0.75f, c.x + s * 0.9f, c.y - s * 0.1f, c.x, c.y + s * 0.45f);
    heart.closeSubPath();
    g.setColour (colour);
    if (filled)
        g.fillPath (heart);
    else
        g.strokePath (heart, juce::PathStrokeType (1.2f));
}
} // namespace

PresetBrowser::PresetBrowser (PresetLibrary& lib)
    : library (lib)
{
    search.setTextToShowWhenEmpty ("Search presets...", theme::dimText);
    search.setFont (theme::font (14.0f));
    search.setIndents (8, 6);
    search.onTextChange = [this] { rebuild(); };
    search.onEscapeKey = [this] { search.clear(); rebuild(); list.grabKeyboardFocus(); };
    search.onReturnKey = [this] { list.grabKeyboardFocus(); };
    addAndMakeVisible (search);

    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);
    rebuild();
}

void PresetBrowser::refresh (int currentLibraryIndex)
{
    current = currentLibraryIndex;
    rebuild();
}

void PresetBrowser::rebuild()
{
    categoryNames = { "All", "Favourites" };
    categoryNames.addArray (library.categories());
    selectedCategory = juce::jlimit (0, categoryNames.size() - 1, selectedCategory);

    const auto category = selectedCategory <= 1 ? juce::String() : categoryNames[selectedCategory];
    visible = library.filter (search.getText(), category, selectedCategory == 1);
    list.updateContent();

    // keep the current preset highlighted (without loading it again)
    updatingSelection = true;
    const auto it = std::find (visible.begin(), visible.end(), current);
    if (it != visible.end())
    {
        const int row = (int) (it - visible.begin());
        list.selectRow (row, false, true);
    }
    else
    {
        list.deselectAllRows();
    }
    updatingSelection = false;
    list.repaint();
    repaint();
}

juce::Rectangle<int> PresetBrowser::categoryArea() const
{
    return getLocalBounds().withTrimmedTop (44).removeFromLeft (kCategoryWidth);
}

int PresetBrowser::categoryAt (juce::Point<int> p) const
{
    const auto area = categoryArea();
    if (! area.contains (p))
        return -1;
    const int row = (p.y - area.getY() - 4) / kCategoryRow;
    return juce::isPositiveAndBelow (row, categoryNames.size()) ? row : -1;
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (theme::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    const auto area = categoryArea();
    g.setColour (theme::outline);
    g.drawVerticalLine (area.getRight(), (float) area.getY() + 4.0f, (float) area.getBottom() - 8.0f);

    for (int i = 0; i < categoryNames.size(); ++i)
    {
        auto row = juce::Rectangle<int> (area.getX() + 6, area.getY() + 4 + i * kCategoryRow, area.getWidth() - 12, kCategoryRow - 2);
        const bool selected = i == selectedCategory;
        if (selected)
        {
            g.setColour (theme::glow.withAlpha (0.16f));
            g.fillRoundedRectangle (row.toFloat(), 4.0f);
        }
        if (i == 1)
        {
            drawHeart (g, row.removeFromLeft (18).toFloat(), true, theme::ice.withAlpha (selected ? 1.0f : 0.6f));
            row.removeFromLeft (2);
        }
        else
        {
            row.removeFromLeft (6);
        }
        g.setColour (selected ? theme::text : theme::dimText);
        g.setFont (theme::font (13.5f, selected));
        g.drawText (categoryNames[i], row, juce::Justification::centredLeft, true);
        if (i == 1)
        {
            // a thin line after "Favourites": above it are the views, below the categories
            g.setColour (theme::outline);
            g.drawHorizontalLine (row.getBottom() + 1, (float) area.getX() + 10.0f, (float) area.getRight() - 10.0f);
        }
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (8);
    search.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    area.removeFromLeft (kCategoryWidth - 8 + 6);
    list.setBounds (area);
}

void PresetBrowser::mouseUp (const juce::MouseEvent& e)
{
    const int row = categoryAt (e.getPosition());
    if (row >= 0 && row != selectedCategory)
    {
        selectedCategory = row;
        rebuild();
    }
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;
    const auto* p = library.at (visible[(size_t) row]);
    if (p == nullptr)
        return;
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);
    if (selected)
    {
        g.setColour (theme::glow.withAlpha (0.22f));
        g.fillRoundedRectangle (area.toFloat(), 4.0f);
    }
    const bool favourite = library.isFavourite (p->key());
    drawHeart (g, area.removeFromRight (24).toFloat(), favourite, favourite ? theme::ice : theme::outline);

    area.removeFromLeft (8);
    g.setColour (selected ? theme::text : theme::text.withAlpha (0.85f));
    g.setFont (theme::font (14.0f, selected));
    g.drawText (p->name, area, juce::Justification::centredLeft, true);

    // in mixed lists, the category in small type on the right
    if (selectedCategory <= 1 || ! p->isFactory)
    {
        g.setColour (theme::dimText.withAlpha (0.8f));
        g.setFont (theme::font (11.0f));
        g.drawText (PresetLibrary::categoryOf (*p), area, juce::Justification::centredRight, true);
    }
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    // a click on the heart toggles the favourite instead of (re)loading
    if (e.x > list.getVisibleRowWidth() - 28 && juce::isPositiveAndBelow (row, (int) visible.size()))
        if (const auto* p = library.at (visible[(size_t) row]))
            if (onFavourite)
                onFavourite (p->key(), ! library.isFavourite (p->key()));
}

void PresetBrowser::selectedRowsChanged (int lastRowSelected)
{
    if (updatingSelection || ! juce::isPositiveAndBelow (lastRowSelected, (int) visible.size()))
        return;
    const int index = visible[(size_t) lastRowSelected];
    if (index != current && onLoad)
        onLoad (index);
}

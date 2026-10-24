// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/ui/MacroKnob.h"
#include "plugin/ui/Theme.h"

using namespace spaceforge;

MacroKnob::MacroKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterId, const juce::String& macroName,
                      const juce::String& hint, juce::Colour accent, float initValue)
    : name (macroName), attachment (apvts, parameterId, slider)
{
    slider.setTitle (macroName); // for screen readers
    slider.setTooltip (hint);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.setDoubleClickReturnValue (true, initValue);
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void MacroKnob::setValueText (const juce::String& text)
{
    if (text != valueText || scalePosition >= 0.0f)
    {
        valueText = text;
        scalePosition = -1.0f;
        repaint();
    }
}

void MacroKnob::setScale (const juce::String& low, const juce::String& high, float position01, const juce::String& below)
{
    position01 = juce::jlimit (0.0f, 1.0f, position01);
    if (low != scaleLow || high != scaleHigh || below != belowText || std::abs (position01 - scalePosition) > 0.002f)
    {
        scaleLow = low;
        scaleHigh = high;
        belowText = below;
        scalePosition = position01;
        repaint();
    }
}

void MacroKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (kTextHeight);
    const int side = juce::jmin (area.getWidth(), area.getHeight());
    slider.setBounds (area.withSizeKeepingCentre (side, side));
}

void MacroKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().removeFromBottom (kTextHeight);
    g.setColour (theme::text);
    g.setFont (theme::font (16.0f, true));
    g.drawText (name.toUpperCase(), area.removeFromTop (20), juce::Justification::centred);

    // what it does right now
    const auto textColour = isMouseOverOrDragging (true) ? theme::text : theme::dimText;
    g.setFont (theme::font (13.0f));
    if (scalePosition < 0.0f)
    {
        g.setColour (textColour);
        g.drawText (valueText, area.removeFromTop (18), juce::Justification::centred);
        return;
    }

    // a fixed row: low word, eight segments, high word; only the segments light up
    auto row = area.removeFromTop (18);
    g.setColour (textColour);
    g.drawText (scaleLow, row.removeFromLeft (52), juce::Justification::centredRight);
    g.drawText (scaleHigh, row.removeFromRight (52), juce::Justification::centredLeft);
    auto bar = row.reduced (5, 4).toFloat();
    constexpr int segments = 8;
    const float gap = 3.0f, w = (bar.getWidth() - gap * (segments - 1)) / segments;
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    for (int i = 0; i < segments; ++i)
    {
        const auto seg = juce::Rectangle<float> (bar.getX() + (float) i * (w + gap), bar.getY(), w, bar.getHeight());
        // the segment holding the position is partly lit, so the scale moves smoothly
        const float lit = juce::jlimit (0.0f, 1.0f, scalePosition * segments - (float) i);
        g.setColour (theme::outline);
        g.fillRoundedRectangle (seg, 1.5f);
        if (lit > 0.0f)
        {
            g.setColour (accent.withAlpha (0.25f + 0.75f * lit));
            g.fillRoundedRectangle (seg, 1.5f);
        }
    }
    g.setColour (textColour);
    g.setFont (theme::font (12.0f));
    g.drawText (belowText, area, juce::Justification::centred);
}

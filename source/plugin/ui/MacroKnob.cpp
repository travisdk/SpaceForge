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

void MacroKnob::animate (float from, float to)
{
    animFrom = from;
    animTo = to;
    animStartMs = juce::Time::getMillisecondCounterHiRes();
    slider.setValue (from, juce::dontSendNotification); // shown only; the parameter keeps `to`
    startTimerHz (60);
}

void MacroKnob::timerCallback()
{
    const float t = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - animStartMs) / 250.0);
    // a drag takes over at once
    if (slider.isMouseButtonDown() || t >= 1.0f)
    {
        stopTimer();
        if (! slider.isMouseButtonDown())
            slider.setValue (animTo, juce::dontSendNotification);
        return;
    }
    const float eased = t * t * (3.0f - 2.0f * t);
    slider.setValue (animFrom + (animTo - animFrom) * eased, juce::dontSendNotification);
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

void MacroKnob::mouseDown (const juce::MouseEvent& e)
{
    // only a click on the text itself (events from the knob also arrive here)
    if (onTextClick != nullptr && e.eventComponent == this && getLocalBounds().removeFromBottom (kTextHeight).contains (e.getPosition()))
        onTextClick();
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
    // (the words as wide as they are, so narrow knobs keep a readable bar; the row centred)
    auto row = area.removeFromTop (18);
    const auto wordWidth = [&g] (const juce::String& word) {
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), word)) + 2;
    };
    const int lowWidth = wordWidth (scaleLow), highWidth = wordWidth (scaleHigh);
    row = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), lowWidth + highWidth + 90), row.getHeight());
    g.setColour (textColour);
    g.drawText (scaleLow, row.removeFromLeft (lowWidth), juce::Justification::centredRight);
    g.drawText (scaleHigh, row.removeFromRight (highWidth), juce::Justification::centredLeft);
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

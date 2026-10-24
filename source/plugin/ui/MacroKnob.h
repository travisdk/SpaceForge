// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// A big rotary control for one of the fixed macros (from SpaceForge 1): its name underneath and,
// below that, what it is doing right now: in real units ("104 s", "1/8d · 3x"), or for Motion
// and Tone a drawn scale between two fixed words ("mørk ▮▮▮▯▯ lys"), with an optional line
// under it ("pan 2 takter"). Nothing in it moves sideways. Double-click returns to Init.
class MacroKnob : public juce::Component
{
public:
    MacroKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& name,
               const juce::String& hint, juce::Colour accent, float initValue);

    void setValueText (const juce::String& text);
    // shows a scale between two fixed words instead of a value, and a line under it
    void setScale (const juce::String& low, const juce::String& high, float position01, const juce::String& below = {});
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    // what is shown under the knob (for the tests: the same as the DAW's text)
    const juce::String& getValueText() const { return valueText; }

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    static constexpr int kTextHeight = 56;

private:
    juce::String name, valueText, scaleLow, scaleHigh, belowText;
    float scalePosition = -1.0f; // < 0: show valueText
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

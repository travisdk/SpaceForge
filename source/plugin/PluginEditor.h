// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "dsp/Modes.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ui/CloudView.h"
#include "plugin/ui/MacroKnob.h"
#include "plugin/ui/Theme.h"

// One window, one glance (layout and look from SpaceForge 1): the Modes beside the logo, the
// cloud picture with the four macros in front of it, Mix and Freeze underneath. Under every
// control what it does right now, in the same words the DAW shows. Laid out at kWidth x
// kHeight and scaled to the window.
class SpaceForgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpaceForgeEditor (SpaceForgeProcessor&);
    ~SpaceForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 760, kHeight = 540;
    static constexpr int kNumMacros = 4;

    // For screenshots and tests.
    void advanceAnimation (float seconds) { cloud.advance (seconds); }
    const MacroKnob& macro (int index) const { return *macros[(size_t) index]; }
    juce::String mixText() const { return mixValue.getText(); }
    juce::String freezeText() const { return freezeValue.getText(); }
    int shownMode() const;
    void clickMode (int mode) { modeButtons[(size_t) mode]->triggerClick(); }

private:
    void timerCallback() override; // the values under the controls
    CloudView::State cloudState() const;

    SpaceForgeProcessor& processor;
    spaceforge::theme::LookAndFeel lookAndFeel;

    juce::Component content; // everything, laid out at kWidth x kHeight and scaled to the window
    CloudView cloud;
    std::array<std::unique_ptr<MacroKnob>, kNumMacros> macros;

    juce::Slider mix { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::AudioProcessorValueTreeState::SliderAttachment mixAttachment;
    juce::TextButton freeze { "FREEZE" };
    juce::AudioProcessorValueTreeState::ButtonAttachment freezeAttachment;
    juce::Label logo, mixLabel, mixValue, freezeValue;
    std::array<std::unique_ptr<juce::TextButton>, spaceforge::dsp::kNumModes> modeButtons;

    juce::TooltipWindow tooltips { this, 600 };
};

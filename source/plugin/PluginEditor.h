// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "dsp/Modes.h"
#include "plugin/PluginProcessor.h"
#include "presets/CandidateSorter.h"
#include "presets/PresetLibrary.h"
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

    static constexpr int kWidth = 760, kHeight = 576;
    static constexpr int kNumMacros = 4;

    // For screenshots and tests.
    void advanceAnimation (float seconds) { cloud.advance (seconds); }
    const MacroKnob& macro (int index) const { return *macros[(size_t) index]; }
    juce::String mixText() const { return mixValue.getText(); }
    juce::String freezeText() const { return freezeValue.getText(); }
    int shownMode() const;
    void clickMode (int mode) { modeButtons[(size_t) mode]->triggerClick(); }

    // Presets: previous / next step through the library (Init, factory, user), the name opens
    // the list (by Mode), Save stores the controls as a user preset. "*" after the name when a
    // control has been turned since.
    void stepPreset (int delta);
    void loadPreset (int index);
    juce::String presetText() const { return presetName.getButtonText(); }
    const PresetLibrary& library() const { return presetLibrary; }

    // The sorting mode (standalone started with --sort [folder]): candidates one at a time with a
    // test sound, K keeps, S skips, left arrow undoes, 1-5 choose the test sound.
    void startSorting (const juce::File& candidateFolder);
    const CandidateSorter* sorting() const { return sorter.get(); }
    juce::String sortText() const { return sortTitle.getText() + " | " + sortHelp.getText(); }
    bool keyPressed (const juce::KeyPress&) override;
    // The folder after --sort on the standalone's command line (the source tree's candidates
    // when none is given); nothing in a plug-in or without --sort.
    static juce::File sortFolderFromCommandLine();

private:
    void timerCallback() override; // the values under the controls
    CloudView::State cloudState() const;
    void showCandidate();
    void applySizeLimits();
    int designHeight() const { return kHeight + (sorter != nullptr ? kSortHeight : 0); }
    static constexpr int kSortHeight = 64;

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

    void showPresetMenu();
    void askToSave();

    PresetLibrary presetLibrary;
    juce::TextButton presetPrev { juce::String::fromUTF8 ("\xe2\x97\x80") }, presetNext { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::TextButton presetName, presetSave { "GEM" };
    std::unique_ptr<juce::AlertWindow> saveDialog;

    std::unique_ptr<CandidateSorter> sorter;
    juce::Label sortTitle, sortHelp;

    juce::TooltipWindow tooltips { this, 600 };
};

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "dsp/Modes.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ui/CloudView.h"
#include "plugin/ui/MacroKnob.h"
#include "plugin/ui/PresetBrowser.h"
#include "plugin/ui/Theme.h"
#include "presets/CandidateSorter.h"

// One window, one glance (layout and look from SpaceForge 1): the preset browser on the left;
// on the right the preset's name with previous / next, favourite, the dice, undo and Save, the
// Modes under it, the cloud picture with the four macros in front of it, and Mix alone in the
// corner. Under every control what it does right now, in the same words the DAW shows. The gear
// by the logo opens the settings (accent colour, size, animations, tooltips, preset files, About;
// as in SubForge). Laid out at kWidth x kHeight and scaled to the window.
class SpaceForgeEditor : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer
{
public:
    explicit SpaceForgeEditor (SpaceForgeProcessor&);
    ~SpaceForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kWidth = 980, kHeight = 600;
    static constexpr int kCompactWidth = 600; // the presets folded away
    static constexpr int kNumMacros = 4;

    // A button drawn from a path: arrows, the heart, the dice, undo (from SpaceForge 1).
    struct ShapeButton : juce::Button
    {
        enum class Shape { Left, Right, Heart, Dice, Back, Lock, Gear, Sidebar };
        ShapeButton (Shape, const juce::String& tooltip);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        Shape shape;
    };

    // For screenshots and tests.
    void advanceAnimation (float seconds) { cloud.advance (seconds); }
    const MacroKnob& macro (int index) const { return *macros[(size_t) index]; }
    const MacroKnob& smallKnob (int index) const { return *smalls[(size_t) index]; } // 0 = Echo, 1 = Duck
    void refreshValues() { timerCallback(); } // the texts under the controls now, not at the next tick
    void clickEchoNote() { smalls[0]->onTextClick(); }
    juce::String mixText() const { return mixValue.getText(); }
    juce::String presetText() const { return presetName.getText(); }
    int shownMode() const;
    void clickMode (int mode) { modeButtons[(size_t) mode]->triggerClick(); }
    // (at once, not through the message loop as triggerClick does)
    void clickDice() { dice.onClick(); processor.dispatchPendingMessages(); }
    void clickModeLock() { modeLock.onClick(); processor.dispatchPendingMessages(); }
    void clickUndo() { undo.onClick(); processor.dispatchPendingMessages(); }
    PresetBrowser& presetBrowser() { return browser; }

    // Settings, saved per user (processor.settings()). Public for the menu's actions and tests.
    void setAccent (juce::Colour);
    void setUiScale (float scale);
    float uiScale() const { return scale; }
    void setAnimations (bool on);
    bool animationsOn() const { return cloud.isAnimated(); }
    void setTooltips (bool on);
    // The preset browser shown, or folded away (the window gets narrower).
    void setBrowserOpen (bool open);
    bool browserOpen() const { return browser.isVisible(); }
    bool tooltipsOn() const { return tooltips != nullptr; }
    static constexpr float kScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    static juce::String aboutText();

    // The sorting mode (standalone started with --sort [folder]): candidates one at a time with a
    // test sound, K keeps, S skips, left arrow undoes, 1-5 choose the test sound.
    void startSorting (const juce::File& candidateFolder);
    const CandidateSorter* sorting() const { return sorter.get(); }
    juce::String sortText() const { return sortTitle.getText() + " | " + sortHelp.getText(); }
    // The folder after --sort on the standalone's command line (the source tree's candidates
    // when none is given); nothing in a plug-in or without --sort.
    static juce::File sortFolderFromCommandLine();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override; // the values under the controls, the * after the name
    void updateFromPreset();
    CloudView::State cloudState() const;
    void showSaveDialog();
    void showSettingsMenu();
    void importPresets();
    void exportPreset();
    void renamePreset();
    void deletePreset();
    void showAbout();
    void askForName (const juce::String& title, const juce::String& message, const juce::String& initial, const juce::String& button,
                     std::function<void (const juce::String&)> onName);
    void showCandidate();
    bool sortKey (const juce::KeyPress&);
    void applySizeLimits();
    int designHeight() const { return kHeight + (sorter != nullptr ? kSortHeight : 0); }
    int designWidth() const { return browserOpen() ? kWidth : kCompactWidth; }
    static constexpr int kSortHeight = 64;

    SpaceForgeProcessor& processor;
    spaceforge::theme::LookAndFeel lookAndFeel;

    juce::Component content; // everything, laid out at kWidth x kHeight and scaled to the window
    PresetBrowser browser;
    CloudView cloud;
    std::array<std::unique_ptr<MacroKnob>, kNumMacros> macros;
    // The two small ones in the strip above Mix: Echo (sharp repeats, the note by clicking its text) and Duck.
    std::array<std::unique_ptr<MacroKnob>, 2> smalls;

    ShapeButton previous { ShapeButton::Shape::Left, "Previous preset" };
    ShapeButton next { ShapeButton::Shape::Right, "Next preset" };
    ShapeButton favourite { ShapeButton::Shape::Heart, "Favourite" };
    ShapeButton dice { ShapeButton::Shape::Dice, "Dice: Space, Reflect, Motion and Tone to new positions; also the Mode when the lock is open (Shift+click: Mode this time). Mix stays." };
    ShapeButton modeLock { ShapeButton::Shape::Lock, "Mode lock: closed = the dice keeps the Mode, open = the dice picks a Mode too" };
    ShapeButton undo { ShapeButton::Shape::Back, "Undo the last roll (Ctrl+Z)" };
    ShapeButton gear { ShapeButton::Shape::Gear, "Settings" };
    ShapeButton browserToggle { ShapeButton::Shape::Sidebar, "Show or hide the presets" };
    juce::TextButton save { "Save" };
    juce::Label presetName, presetInfo;

    juce::Slider mix { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::AudioProcessorValueTreeState::SliderAttachment mixAttachment;
    juce::Label logo, mixLabel, mixValue;
    std::array<std::unique_ptr<juce::TextButton>, spaceforge::dsp::kNumModes> modeButtons;

    std::unique_ptr<juce::AlertWindow> saveDialog;
    std::unique_ptr<juce::FileChooser> chooser;
    float scale = 1.0f;
    int seenKnobJumps = 0;

    std::unique_ptr<CandidateSorter> sorter;
    juce::Label sortTitle, sortHelp;

    std::unique_ptr<juce::TooltipWindow> tooltips;
};

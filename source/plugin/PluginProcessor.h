// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "analysis/TestSignals.h"
#include "dsp/SpaceEngine.h"
#include "presets/PresetLibrary.h"
#include "presets/Variation.h"

// Granular texture (Clouds) and echoes into the room (Cloud Seed), moved by the four macros
// Space, Echo, Motion and Tone, plus Mix, in one of the Modes. Stereo out, mono or stereo in.
// (Freeze is out for now, kept on the branch feature/freeze; the engine still has it.)
class SpaceForgeProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    SpaceForgeProcessor();
    ~SpaceForgeProcessor() override = default;

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // The longest tail that ends (Space below "∞"); above it, it is endless anyway.
    double getTailLengthSeconds() const override { return 140.0; }

    // The presets as the host's program list (Init, factory, user), so the DAW can step through
    // them too.
    int getNumPrograms() override { return juce::jmax (1, presetLibrary.size()); }
    int getCurrentProgram() override { return juce::jmax (0, currentIndex); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // For the picture, read by the editor: the output's peak (decaying), hits in the input so
    // far, the auto-pan's position, and the host tempo (120 when the host gives none).
    float getOutputLevel() const { return outputLevel.load(); }
    int getOnsetCount() const { return onsetCount.load(); }
    float getPanPosition() const { return panPosition.load(); }
    double getBpm() const { return bpm.load(); }

    // A preset: every control jumps to its value (the DAW sees it), and the sound changes cleanly
    // (SpaceEngine::jumpToMacros: a short fade, nothing of the old tail, no glide); the knobs
    // then glide on screen only (knobJumpCount/From). Message thread, like everything below.
    void applyPreset (const spaceforge::Preset&);
    void loadPreset (int libraryIndex);
    // The next / previous of the presets shown in the browser (from SpaceForge 1).
    void step (int delta, const std::vector<int>& visible);
    const spaceforge::Preset& preset() const { return currentPreset; }
    const juce::String& presetName() const { return currentPreset.name; }
    int presetIndex() const { return currentIndex; }
    // Whether the controls still say what the preset says (the editor shows * when not).
    bool presetUnchanged() const;
    PresetLibrary& library() { return presetLibrary; }
    // After the user folder changed (import, rename, delete): the list again, the current
    // preset found again by its key, the host told.
    void presetsChanged();
    // The user's preferences (accent, size, animations, tooltips, favourites, Mode lock): per
    // user, not per project.
    juce::PropertiesFile& settings() { return userSettings; }
    // The tests use a settings file of their own instead of the user's (set before construction).
    static void setSettingsFileForTests (const juce::File& file) { settingsFileOverride() = file; }
    void setFavourite (const juce::String& key, bool on);
    // Saves the controls as a user preset and makes it the current one; false if it failed.
    bool saveUserPreset (const juce::String& name);

    // The dice: the four macros to new random positions, and with withMode also another Mode
    // (Mix stays); clean switch, the knobs glide on screen. Undo goes back through the last ten
    // rolls. Whether the Mode is locked against the dice is the user's preference (saved with
    // the favourites, not in presets: it is not part of the sound).
    void roll (bool withMode);
    bool isModeLocked() const { return modeLocked; }
    void setModeLocked (bool locked);
    bool canUndoRoll() const { return rolls.canUndo(); }
    void undoRoll();
    int knobJumpCount() const { return jumps; }
    const spaceforge::MacroKnobs& knobJumpFrom() const { return jumpFrom; }

    // The sorting mode's test sounds (standalone only): the input is replaced by one of them,
    // looped. startAudition builds them (message thread, allocates); restartAudition plays the
    // sound from its start once the room has been emptied (after a preset change).
    void startAudition();
    void setAuditionSound (int index) { auditionSound.store (std::clamp (index, 0, (int) auditionSounds.size() - 1)); restartAudition(); }
    void restartAudition() { auditionRestart.store (true); }
    int auditionSoundIndex() const { return auditionSound.load(); }
    juce::String auditionSoundName (int index) const;
    static constexpr int kNumAuditionSounds = 5;

    // Saved with every state, so later versions know what they are reading.
    static constexpr int kStateVersion = 1;
    // A host hands us whatever a project file contains: anything larger is rejected unread.
    static constexpr int kMaxStateBytes = 1 << 20;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int currentMode() const;
    void updateEngine();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* spaceValue = nullptr;
    std::atomic<float>* echoValue = nullptr;
    std::atomic<float>* motionValue = nullptr;
    std::atomic<float>* toneValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* modeValue = nullptr;
    spaceforge::dsp::SpaceEngine engine;

    std::atomic<float> outputLevel { 0.0f }, panPosition { 0.0f };
    std::atomic<int> onsetCount { 0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> jumpRequested { false };
    spaceforge::Preset currentPreset = spaceforge::presets::makeInit();
    int currentIndex = 0;
    PresetLibrary presetLibrary;
    // Per-user preferences (favourites), not per project (from SpaceForge 1).
    static juce::PropertiesFile::Options settingsOptions();
    juce::PropertiesFile userSettings { settingsFile(), settingsOptions() };
    spaceforge::VariationHistory rolls;
    bool modeLocked = true;
    juce::Random random;
    int jumps = 0;
    spaceforge::MacroKnobs jumpFrom {};
    static juce::File& settingsFileOverride()
    {
        static juce::File file;
        return file;
    }
    static juce::File settingsFile()
    {
        return settingsFileOverride() != juce::File() ? settingsFileOverride() : settingsOptions().getDefaultFile();
    }
    spaceforge::MacroKnobs knobs() const;
    void setKnobs (const spaceforge::MacroKnobs&, int mode);

    std::vector<spaceforge::analysis::TestSignal> auditionSounds; // built once, never changed after
    std::atomic<int> auditionSound { -1 };                        // -1: the real input
    std::atomic<bool> auditionRestart { false };
    juce::int64 auditionPos = 0;                                   // audio thread; < 0: silence first
    float inputFast = 0.0f, inputSlow = 0.0f; // the input's envelope, per block (hits)
    int onsetHold = 0;                        // blocks until the next hit may count

    JUCE_DECLARE_WEAK_REFERENCEABLE (SpaceForgeProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceForgeProcessor)
};

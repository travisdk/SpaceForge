// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SpaceEngine.h"

// Granular texture (Clouds) and echoes into the room (Cloud Seed), moved by the four macros
// Space, Echo, Motion and Tone, plus Mix and Freeze, in one of the Modes. Stereo out, mono or
// stereo in.
class SpaceForgeProcessor : public juce::AudioProcessor
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
    // The longest tail that ends (Space below "∞"); above it, and frozen, it is endless anyway.
    double getTailLengthSeconds() const override { return 140.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
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
    std::atomic<float>* freezeValue = nullptr;
    std::atomic<float>* modeValue = nullptr;
    spaceforge::dsp::SpaceEngine engine;

    std::atomic<float> outputLevel { 0.0f }, panPosition { 0.0f };
    std::atomic<int> onsetCount { 0 };
    std::atomic<double> bpm { 120.0 };
    float inputFast = 0.0f, inputSlow = 0.0f; // the input's envelope, per block (hits)
    int onsetHold = 0;                        // blocks until the next hit may count

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceForgeProcessor)
};

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SpaceEngine.h"

// Granular texture (Clouds) and echoes into the room (Cloud Seed), moved by the four macros
// Space, Echo, Motion and Tone, plus Mix and Freeze. Stereo out, mono or stereo in.
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
    // The longest tail without Freeze; frozen it is endless anyway.
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // Saved with every state, so later versions know what they are reading.
    static constexpr int kStateVersion = 1;
    // A host hands us whatever a project file contains: anything larger is rejected unread.
    static constexpr int kMaxStateBytes = 1 << 20;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateEngine();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* spaceValue = nullptr;
    std::atomic<float>* echoValue = nullptr;
    std::atomic<float>* motionValue = nullptr;
    std::atomic<float>* toneValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* freezeValue = nullptr;
    spaceforge::dsp::SpaceEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceForgeProcessor)
};

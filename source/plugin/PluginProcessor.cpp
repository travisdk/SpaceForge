// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/PluginProcessor.h"

SpaceForgeProcessor::SpaceForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool SpaceForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
           && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void SpaceForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Mono in: the single input channel feeds both outputs.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
}

void SpaceForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("SpaceForge2");
    state.setAttribute ("stateVersion", kStateVersion);
    copyXmlToBinary (state, destData);
}

void SpaceForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Nothing to restore yet (no parameters before phase 2); only check that it is ours.
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > kMaxStateBytes)
        return;
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("SpaceForge2"))
        return;
}

juce::AudioProcessorEditor* SpaceForgeProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpaceForgeProcessor();
}

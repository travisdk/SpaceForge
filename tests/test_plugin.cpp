// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The plug-in as a host drives it. Phase 1: stereo and mono input both come out unchanged.

#include "TestUtils.h"
#include "plugin/PluginProcessor.h"

namespace
{
void testPassThrough (const juce::AudioChannelSet& input)
{
    SpaceForgeProcessor processor;
    CHECK (processor.setBusesLayout ({ { input }, { juce::AudioChannelSet::stereo() } }));

    for (const double rate : kSampleRates)
    {
        for (const int block : { 1, 64, 333, 4096 })
        {
            processor.setRateAndBufferSizeDetails (rate, block);
            processor.prepareToPlay (rate, block);

            juce::Random random (7);
            juce::AudioBuffer<float> buffer (2, block), original (2, block);
            for (int i = 0; i < block; ++i)
                for (int ch = 0; ch < input.size(); ++ch)
                    buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
            original.makeCopyOf (buffer);

            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    CHECK (juce::exactlyEqual (buffer.getSample (ch, i), original.getSample (std::min (ch, input.size() - 1), i)));
            processor.releaseResources();
        }
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testPassThrough (juce::AudioChannelSet::stereo());
    testPassThrough (juce::AudioChannelSet::mono());
    return finish ("plugin");
}

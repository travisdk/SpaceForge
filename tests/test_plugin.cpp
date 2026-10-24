// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The plug-in as a host drives it: every rate and block size, mono and stereo input, Mix at 0
// passes the input through untouched, and saved state survives a round trip while damaged or
// hostile state never gets through.

#include "TestUtils.h"
#include "plugin/Defaults.h"
#include "plugin/PluginProcessor.h"

using namespace spaceforge;

namespace
{
float paramValue (SpaceForgeProcessor& p, const char* id)
{
    auto* param = p.parameters().getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void setParam (SpaceForgeProcessor& p, const char* id, float value)
{
    auto* param = p.parameters().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void testProcessing (const juce::AudioChannelSet& input)
{
    SpaceForgeProcessor processor;
    CHECK (processor.setBusesLayout ({ { input }, { juce::AudioChannelSet::stereo() } }));

    for (const double rate : kSampleRates)
    {
        for (const int block : { 1, 64, 333, 4096 })
        {
            processor.setRateAndBufferSizeDetails (rate, block);

            // Mix 0: the input comes out unchanged (mono input on both sides).
            setParam (processor, plugin::kMixId, 0.0f);
            processor.prepareToPlay (rate, block);
            juce::Random random (7);
            juce::AudioBuffer<float> buffer (2, block), original (2, block);
            buffer.clear();
            for (int i = 0; i < block; ++i)
                for (int ch = 0; ch < input.size(); ++ch)
                    buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
            original.makeCopyOf (buffer);
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    CHECK (juce::exactlyEqual (buffer.getSample (ch, i), original.getSample (std::min (ch, input.size() - 1), i)));

            // Default mix, half a second of noise: finite, and the room is clearly there.
            setParam (processor, plugin::kMixId, plugin::kDefaultMix);
            processor.prepareToPlay (rate, block);
            double wetEnergy = 0.0;
            for (int done = 0; done < (int) (rate * 0.5); done += block)
            {
                for (int i = 0; i < block; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, ch < input.size() ? random.nextFloat() * 0.5f - 0.25f : 0.0f);
                original.makeCopyOf (buffer);
                processor.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                    {
                        CHECK (std::isfinite (buffer.getSample (ch, i)));
                        const float d = buffer.getSample (ch, i) - original.getSample (std::min (ch, input.size() - 1), i);
                        wetEnergy += (double) d * d;
                    }
            }
            CHECK (wetEnergy > 1.0); // the output is clearly not just the input
            processor.releaseResources();
        }
    }
}

void testTexts()
{
    // The DAW shows what the knobs do, in the same words as the plug-in.
    SpaceForgeProcessor p;
    auto text = [&p] (const char* id, float value) { return p.parameters().getParameter (id)->getText (value, 64); };
    CHECK (text (plugin::kEchoId, 0.0f) == "intet ekko");
    CHECK (text (plugin::kEchoId, 0.5f) == juce::String::fromUTF8 (dsp::describeEcho (0.5f).c_str()));
    CHECK (text (plugin::kMotionId, 0.0f) == "stille");
    CHECK (text (plugin::kSpaceId, 0.6f) == juce::String::fromUTF8 (dsp::describeSpace (0.6f).c_str()));
    CHECK (text (plugin::kToneId, 0.0f).startsWith (juce::String::fromUTF8 ("m\u00f8rk")));

    // Text back to a value lands where the same text is shown (the DAW's typed-in values).
    for (const char* id : { plugin::kSpaceId, plugin::kEchoId, plugin::kMotionId, plugin::kToneId })
    {
        auto* param = p.parameters().getParameter (id);
        for (int i = 0; i <= 20; ++i)
        {
            const auto shown = param->getText ((float) i / 20.0f, 64);
            CHECK (param->getText (param->getValueForText (shown), 64) == shown);
        }
    }
}

void testStateRoundTrip()
{
    SpaceForgeProcessor a;
    setParam (a, plugin::kSpaceId, 0.21f);
    setParam (a, plugin::kEchoId, 0.42f);
    setParam (a, plugin::kMotionId, 0.63f);
    setParam (a, plugin::kToneId, 0.84f);
    setParam (a, plugin::kMixId, 0.73f);
    setParam (a, plugin::kFreezeId, 1.0f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    SpaceForgeProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_RANGE (paramValue (b, plugin::kMixId), 0.729, 0.731);
    CHECK_RANGE (paramValue (b, plugin::kSpaceId), 0.209, 0.211);
    CHECK_RANGE (paramValue (b, plugin::kEchoId), 0.419, 0.421);
    CHECK_RANGE (paramValue (b, plugin::kMotionId), 0.629, 0.631);
    CHECK_RANGE (paramValue (b, plugin::kToneId), 0.839, 0.841);
    CHECK (paramValue (b, plugin::kFreezeId) > 0.5f);
}

void loadXml (SpaceForgeProcessor& p, const juce::String& xmlText)
{
    juce::MemoryBlock block;
    if (auto xml = juce::parseXML (xmlText))
        juce::AudioProcessor::copyXmlToBinary (*xml, block);
    p.setStateInformation (block.getData(), (int) block.getSize());
}

void testHostileState()
{
    SpaceForgeProcessor p;

    // Not a number, out of range, the wrong type: defaults or clamped values, never garbage.
    loadXml (p, "<PARAMS stateVersion=\"1\"><PARAM id=\"mix\" value=\"nan\"/><PARAM id=\"freeze\" value=\"inf\"/></PARAMS>");
    CHECK_RANGE (paramValue (p, plugin::kMixId), plugin::kDefaultMix - 0.001, plugin::kDefaultMix + 0.001);
    CHECK (paramValue (p, plugin::kFreezeId) < 0.5f);

    loadXml (p, "<PARAMS stateVersion=\"1\"><PARAM id=\"mix\" value=\"1e30\"/></PARAMS>");
    CHECK_RANGE (paramValue (p, plugin::kMixId), 0.999, 1.0);
    loadXml (p, "<PARAMS><PARAM id=\"mix\" value=\"-5\"/></PARAMS>");
    CHECK_RANGE (paramValue (p, plugin::kMixId), 0.0, 0.001);
    loadXml (p, "<PARAMS><PARAM id=\"mix\" value=\"abc\"/></PARAMS>");
    CHECK_RANGE (paramValue (p, plugin::kMixId), 0.0, 1.0);

    // Someone else's state, random bytes, nothing, too much: ignored.
    setParam (p, plugin::kMixId, 0.3f);
    loadXml (p, "<OtherPlugin><PARAM id=\"mix\" value=\"0.9\"/></OtherPlugin>");
    CHECK_RANGE (paramValue (p, plugin::kMixId), 0.299, 0.301);
    juce::Random random (3);
    juce::MemoryBlock junk (4096);
    for (size_t i = 0; i < junk.getSize(); ++i)
        junk[i] = (char) random.nextInt (256);
    p.setStateInformation (junk.getData(), (int) junk.getSize());
    p.setStateInformation (nullptr, 0);
    juce::MemoryBlock huge ((size_t) SpaceForgeProcessor::kMaxStateBytes + 1, true);
    p.setStateInformation (huge.getData(), (int) huge.getSize());
    CHECK_RANGE (paramValue (p, plugin::kMixId), 0.299, 0.301);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testProcessing (juce::AudioChannelSet::stereo());
    testProcessing (juce::AudioChannelSet::mono());
    testTexts();
    testStateRoundTrip();
    testHostileState();
    return finish ("plugin");
}

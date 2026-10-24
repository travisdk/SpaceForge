// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/PluginProcessor.h"
#include "plugin/Defaults.h"
#include "plugin/PluginEditor.h"

using namespace spaceforge;

SpaceForgeProcessor::SpaceForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    spaceValue = apvts.getRawParameterValue (plugin::kSpaceId);
    echoValue = apvts.getRawParameterValue (plugin::kEchoId);
    motionValue = apvts.getRawParameterValue (plugin::kMotionId);
    toneValue = apvts.getRawParameterValue (plugin::kToneId);
    mixValue = apvts.getRawParameterValue (plugin::kMixId);
    freezeValue = apvts.getRawParameterValue (plugin::kFreezeId);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpaceForgeProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // The macros: 0..1, shown as what they do (the same text in the plug-in and the DAW). Text
    // typed into the DAW goes back to the middle of the range showing that text; a plain number
    // is read as percent. Some wrappers hand the text over without decoding UTF-8 (each byte one
    // character), so that reading is tried too.
    auto macro = [&params] (const char* id, const char* name, float init, std::string (*describe) (float)) {
        auto toValue = [describe] (const juce::String& text) {
            const auto trimmed = text.trim();
            const auto utf8 = trimmed.toStdString();
            std::string bytes;
            for (auto p = trimmed.getCharPointer(); ! p.isEmpty(); ++p)
                bytes += (char) (*p < 0x100 ? *p : '?');
            int first = -1, last = -1;
            for (int i = 0; i <= 1000; ++i)
                if (const auto shown = describe ((float) i / 1000.0f); shown == utf8 || shown == bytes)
                {
                    first = first < 0 ? i : first;
                    last = i;
                }
            if (first >= 0)
                return (float) (first + last) / 2000.0f;
            const auto number = text.trim().trimCharactersAtEnd ("% ");
            return number.containsOnly ("0123456789.") && number.isNotEmpty() ? juce::jlimit (0.0f, 1.0f, number.getFloatValue() / 100.0f) : 0.0f;
        };
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), init,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([describe] (float v, int) { return juce::String::fromUTF8 (describe (v).c_str()); })
                .withValueFromStringFunction (toValue)));
    };
    using dsp::MacroPositions;
    macro (plugin::kSpaceId, "Space", MacroPositions::kInitSpace, dsp::describeSpace);
    macro (plugin::kEchoId, "Echo", MacroPositions::kInitEcho, dsp::describeEcho);
    macro (plugin::kMotionId, "Motion", MacroPositions::kInitMotion, dsp::describeMotion);
    macro (plugin::kToneId, "Tone", MacroPositions::kInitTone, dsp::describeTone);
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { plugin::kMixId, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), plugin::kDefaultMix,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; })));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { plugin::kFreezeId, 1 }, "Freeze", false,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool on, int) { return juce::String (on ? "On" : "Off"); })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.trim().equalsIgnoreCase ("on") || t.trim() == "1"; })));
    return { params.begin(), params.end() };
}

void SpaceForgeProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate); // may allocate: off the audio thread
    updateEngine();
    engine.reset(); // start at the current knob values, no glide from defaults
}

void SpaceForgeProcessor::updateEngine()
{
    engine.setMacros ({ spaceValue->load(), echoValue->load(), motionValue->load(), toneValue->load() });
    engine.setMix (mixValue->load());
    engine.setFreeze (freezeValue->load() >= 0.5f);
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
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // Mono in: the single input channel feeds both outputs.
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    updateEngine();
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            const double hostBpm = position->getBpm().orFallback (0.0);
            engine.setTempo (hostBpm, position->getIsPlaying(), position->getPpqPosition().orFallback (0.0));
            if (std::isfinite (hostBpm) && hostBpm >= 20.0 && hostBpm <= 999.0)
                bpm.store (hostBpm);
        }
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    // A hit in the input (for the echo rings): the peak jumps well above its recent level.
    const auto blockSeconds = (float) (numSamples / getSampleRate());
    const float inPeak = buffer.getMagnitude (0, numSamples);
    inputFast = inPeak > inputFast ? inPeak : inputFast * std::exp (-blockSeconds / 0.03f);
    if (onsetHold > 0)
        --onsetHold;
    else if (std::isfinite (inPeak) && inPeak > 0.003f && inPeak > 2.0f * inputSlow)
    {
        onsetCount.fetch_add (1);
        onsetHold = juce::roundToInt (0.08f / blockSeconds);
    }
    inputSlow += (inputFast - inputSlow) * (1.0f - std::exp (-blockSeconds / 0.3f));

    engine.process (left, right, left, right, numSamples);

    const float outPeak = buffer.getMagnitude (0, numSamples);
    const float decayed = outputLevel.load() * std::exp (-blockSeconds / 0.3f);
    outputLevel.store (std::isfinite (outPeak) ? std::max (outPeak, decayed) : 0.0f);
    panPosition.store (engine.panPosition());
}

void SpaceForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Sorted by id, so the same settings always save byte for byte the same.
    auto state = apvts.copyState();
    struct ById
    {
        static int compareElements (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            return a.getProperty ("id").toString().compare (b.getProperty ("id").toString());
        }
    } byId;
    state.sort (byId, nullptr, false);
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("stateVersion", kStateVersion);
        copyXmlToBinary (*xml, destData);
    }
}

void SpaceForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // A project file may be damaged or hostile: limit the size, accept only our own tag, and take
    // each known parameter only if its value is a finite number (clamped into range). Anything
    // missing or invalid falls back to the default. Unknown entries are dropped.
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > kMaxStateBytes)
        return;
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    juce::ValueTree clean (apvts.state.getType());
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;
        const auto& range = ranged->getNormalisableRange();
        double value = range.convertFrom0to1 (ranged->getDefaultValue());
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
        {
            if (child->getStringAttribute ("id") != ranged->getParameterID())
                continue;
            const double stored = child->getDoubleAttribute ("value", std::numeric_limits<double>::quiet_NaN());
            if (std::isfinite (stored))
                value = std::clamp (stored, (double) range.start, (double) range.end);
            break;
        }
        clean.appendChild (juce::ValueTree ("PARAM", { { "id", ranged->getParameterID() }, { "value", value } }), nullptr);
    }
    apvts.replaceState (clean);
    // Tells the host (CLAP: a parameter rescan) that every value may have changed.
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::AudioProcessorEditor* SpaceForgeProcessor::createEditor()
{
    return new SpaceForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpaceForgeProcessor();
}

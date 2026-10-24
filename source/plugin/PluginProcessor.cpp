// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/PluginProcessor.h"
#include "plugin/Defaults.h"
#include "dsp/Modes.h"
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
    modeValue = apvts.getRawParameterValue (plugin::kModeId);
    auto favourites = juce::StringArray::fromLines (userSettings.getValue ("favourites"));
    favourites.removeEmptyStrings();
    presetLibrary.setFavourites (favourites);
    modeLocked = userSettings.getBoolValue ("modeLocked", true);
}

int SpaceForgeProcessor::currentMode() const
{
    const float v = modeValue != nullptr ? modeValue->load() : 0.0f;
    return std::isfinite (v) ? std::clamp ((int) std::lround (v), 0, dsp::kNumModes - 1) : 0;
}

juce::AudioProcessorValueTreeState::ParameterLayout SpaceForgeProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // The macros: 0..1, shown as what they do (the same text in the plug-in and the DAW). Text
    // typed into the DAW goes back to the middle of the range showing that text; a plain number
    // is read as percent. Some wrappers hand the text over without decoding UTF-8 (each byte one
    // character), so that reading is tried too.
    auto macro = [&params] (const char* id, const char* name, float init, std::function<std::string (float)> describe) {
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
    // Space shows the seconds of the Mode in use (each Mode has its own range).
    macro (plugin::kSpaceId, "Space", MacroPositions::kInitSpace, [this] (float v) { return dsp::describeSpace (v, currentMode()); });
    macro (plugin::kEchoId, "Echo", MacroPositions::kInitEcho, dsp::describeEcho);
    macro (plugin::kMotionId, "Motion", MacroPositions::kInitMotion, dsp::describeMotion);
    macro (plugin::kToneId, "Tone", MacroPositions::kInitTone, dsp::describeTone);
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { plugin::kMixId, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), plugin::kDefaultMix,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; })));
    juce::StringArray modeNames;
    for (int m = 0; m < dsp::kNumModes; ++m)
        modeNames.add (dsp::modeSetup (m).name);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { plugin::kModeId, 1 }, "Mode", modeNames, 0));
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
    const dsp::MacroPositions macros { spaceValue->load(), echoValue->load(), motionValue->load(), toneValue->load(), currentMode() };
    if (jumpRequested.exchange (false))
        engine.jumpToMacros (macros);
    else
        engine.setMacros (macros);
    engine.setMix (mixValue->load());
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

    // Sorting: a test sound instead of the input, looped; after a preset change it starts again
    // from the top once the old room is gone (the engine's fade out, 20 ms).
    if (const int sound = auditionSound.load(); sound >= 0 && sound < (int) auditionSounds.size())
    {
        if (auditionRestart.exchange (false))
            auditionPos = -(juce::int64) (0.03 * getSampleRate());
        const auto& audio = auditionSounds[(size_t) sound].audio;
        const auto length = (juce::int64) audio.size();
        for (int i = 0; i < numSamples; ++i, ++auditionPos)
        {
            if (auditionPos >= length)
                auditionPos = 0;
            const bool playing = auditionPos >= 0;
            buffer.setSample (0, i, playing ? audio.l[(size_t) auditionPos] : 0.0f);
            buffer.setSample (1, i, playing ? audio.r[(size_t) auditionPos] : 0.0f);
        }
    }

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
        xml->setAttribute ("preset", currentPreset.name);
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
    // The preset's name (shown, never sound): the controls as loaded are its values, so "*" only
    // appears once something is turned.
    currentPreset.name = presets::sanitiseName (xml->getStringAttribute ("preset", "Init"));
    if (currentPreset.name.isEmpty())
        currentPreset.name = "Init";
    currentPreset.mode = currentMode();
    currentPreset.space = spaceValue->load();
    currentPreset.echo = echoValue->load();
    currentPreset.motion = motionValue->load();
    currentPreset.tone = toneValue->load();
    currentPreset.mix = mixValue->load();
    currentIndex = presetLibrary.indexOf (currentPreset.name);
    rolls.clear();
    sendChangeMessage();
    // Tells the host (CLAP: a parameter rescan) that every value may have changed.
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void SpaceForgeProcessor::applyPreset (const Preset& preset)
{
    jumpFrom = knobs();
    auto set = [this] (const char* id, float value) {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (plugin::kModeId, (float) preset.mode);
    set (plugin::kSpaceId, preset.space);
    set (plugin::kEchoId, preset.echo);
    set (plugin::kMotionId, preset.motion);
    set (plugin::kToneId, preset.tone);
    set (plugin::kMixId, preset.mix);
    currentPreset = preset;
    currentIndex = presetLibrary.indexOfKey (preset.key());
    rolls.clear();
    jumpRequested.store (true); // after the values, so the audio thread jumps to the new ones
    restartAudition();
    ++jumps;
    sendChangeMessage();
}

void SpaceForgeProcessor::loadPreset (int libraryIndex)
{
    if (const auto* p = presetLibrary.at (libraryIndex))
        applyPreset (*p);
}

void SpaceForgeProcessor::step (int delta, const std::vector<int>& visible)
{
    if (visible.empty())
        return;
    const auto it = std::find (visible.begin(), visible.end(), currentIndex);
    const int n = (int) visible.size();
    const int pos = it == visible.end() ? (delta > 0 ? 0 : n - 1) : (((int) (it - visible.begin()) + delta) % n + n) % n;
    loadPreset (visible[(size_t) pos]);
}

void SpaceForgeProcessor::setFavourite (const juce::String& key, bool on)
{
    presetLibrary.setFavourite (key, on);
    userSettings.setValue ("favourites", presetLibrary.getFavourites().joinIntoString ("\n"));
    userSettings.saveIfNeeded();
    sendChangeMessage();
}

bool SpaceForgeProcessor::saveUserPreset (const juce::String& name)
{
    Preset p = currentPreset;
    p.isFactory = false;
    p.name = name;
    p.mode = currentMode();
    const auto k = knobs();
    p.space = k[0];
    p.echo = k[1];
    p.motion = k[2];
    p.tone = k[3];
    p.mix = mixValue->load();
    const int index = presetLibrary.saveUserPreset (p);
    if (index < 0)
        return false;
    currentPreset = *presetLibrary.at (index); // the sound is already this: nothing to switch
    currentIndex = index;
    rolls.clear();
    sendChangeMessage();
    return true;
}

MacroKnobs SpaceForgeProcessor::knobs() const
{
    return { spaceValue->load(), echoValue->load(), motionValue->load(), toneValue->load() };
}

void SpaceForgeProcessor::setKnobs (const MacroKnobs& k, int mode)
{
    jumpFrom = knobs();
    if (mode != currentMode())
        if (auto* param = apvts.getParameter (plugin::kModeId))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) mode));
            param->endChangeGesture();
        }
    const char* ids[] = { plugin::kSpaceId, plugin::kEchoId, plugin::kMotionId, plugin::kToneId };
    for (size_t m = 0; m < k.size(); ++m)
        if (auto* param = apvts.getParameter (ids[m]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (k[m]));
            param->endChangeGesture();
        }
    jumpRequested.store (true);
    ++jumps;
    sendChangeMessage();
}

void SpaceForgeProcessor::roll (bool withMode)
{
    rolls.push ({ knobs(), currentMode() });
    setKnobs (rollMacros (knobs(), random), withMode ? rollMode (currentMode(), dsp::kNumModes, random) : currentMode());
}

void SpaceForgeProcessor::undoRoll()
{
    if (rolls.canUndo())
    {
        const auto step = rolls.pop();
        setKnobs (step.knobs, step.mode);
    }
}

void SpaceForgeProcessor::setModeLocked (bool locked)
{
    modeLocked = locked;
    userSettings.setValue ("modeLocked", locked);
    userSettings.saveIfNeeded();
    sendChangeMessage();
}

bool SpaceForgeProcessor::presetUnchanged() const
{
    Preset now = currentPreset;
    now.mode = currentMode();
    now.space = spaceValue->load();
    now.echo = echoValue->load();
    now.motion = motionValue->load();
    now.tone = toneValue->load();
    now.mix = mixValue->load();
    return now.sameSound (currentPreset);
}

void SpaceForgeProcessor::startAudition()
{
    if (! auditionSounds.empty())
        return;
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    // Each sound followed by 6 s of its tail, then again. The pad first: it shows colour best.
    auditionSounds = { analysis::makePad (rate, 6.0), analysis::makeBleeps (rate, 6.0), analysis::makeSynthFigure (rate, 6.0),
                       analysis::makeClick (rate, 6.0), analysis::makeHiHats (rate, 6.0) };
    jassert ((int) auditionSounds.size() == kNumAuditionSounds);
    auditionSound.store (0);
    restartAudition();
}

juce::String SpaceForgeProcessor::auditionSoundName (int index) const
{
    static const char* names[kNumAuditionSounds] = { "pad", "bleeps", "synth-figur", "klik", "hi-hat" };
    return juce::isPositiveAndBelow (index, kNumAuditionSounds) ? names[index] : "";
}

juce::AudioProcessorEditor* SpaceForgeProcessor::createEditor()
{
    return new SpaceForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpaceForgeProcessor();
}

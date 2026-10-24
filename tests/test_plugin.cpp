// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The plug-in as a host drives it: every rate and block size, mono and stereo input, Mix at 0
// passes the input through untouched, and saved state survives a round trip while damaged or
// hostile state never gets through.

#include "TestUtils.h"
#include "plugin/Defaults.h"
#include "analysis/TestSignals.h"
#include "plugin/PluginEditor.h"

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

void testEditor()
{
    // The window shows under every control what it does, in the same words as the DAW, follows
    // the knobs, and scales with the window. The picture reacts to sound: hits send echo rings.
    SpaceForgeProcessor p;
    auto set = [&p] (const char* id, float value) { p.parameters().getParameter (id)->setValueNotifyingHost (value); };
    set (plugin::kSpaceId, 0.25f);
    set (plugin::kEchoId, 0.5f);
    set (plugin::kMixId, 0.7f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* view = dynamic_cast<SpaceForgeEditor*> (editor.get());
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    auto dawText = [&p] (const char* id) { return p.parameters().getParameter (id)->getCurrentValueAsText(); };
    view->advanceAnimation (0.1f);
    CHECK (view->macro (0).getValueText() == dawText (plugin::kSpaceId));
    CHECK (view->macro (1).getValueText() == dawText (plugin::kEchoId));
    CHECK (view->mixText() == "70 %");
    CHECK (view->presetText() == "Init *"); // knobs turned since Init
    CHECK (p.parameters().getParameter ("freeze") == nullptr); // out for now (branch feature/freeze)

    // The dice moves the four macros (at least two clearly), never Mode or Mix; undo goes back.
    auto knob = [&p] (const char* id) { return p.parameters().getRawParameterValue (id)->load(); };
    const float before[] = { knob (plugin::kSpaceId), knob (plugin::kEchoId), knob (plugin::kMotionId), knob (plugin::kToneId) };
    const float modeBefore = knob (plugin::kModeId);
    view->clickDice();
    int moved = 0;
    const char* ids[] = { plugin::kSpaceId, plugin::kEchoId, plugin::kMotionId, plugin::kToneId };
    for (int m = 0; m < 4; ++m)
        moved += std::abs (knob (ids[m]) - before[m]) >= 0.2f ? 1 : 0;
    CHECK (moved >= 2);
    CHECK (std::abs (knob (plugin::kMixId) - 0.7f) < 1.0e-4f && std::abs (knob (plugin::kModeId) - modeBefore) < 1.0e-4f);
    CHECK (view->macro (0).isAnimating()); // the knobs glide on screen; the parameter is already there
    view->clickDice();
    view->clickUndo();
    view->clickUndo();
    for (int m = 0; m < 4; ++m)
        CHECK (std::abs (knob (ids[m]) - before[m]) < 1.0e-4f);

    // The Mode lock open: the dice picks another Mode too; undo brings the Mode back. Closed
    // again afterwards (it is a saved preference).
    const bool wasLocked = p.isModeLocked();
    if (wasLocked)
        view->clickModeLock();
    CHECK (! p.isModeLocked());
    const auto modeNow = std::lround (knob (plugin::kModeId));
    view->clickDice();
    CHECK (std::lround (knob (plugin::kModeId)) != modeNow);
    view->clickUndo();
    CHECK (std::lround (knob (plugin::kModeId)) == modeNow);
    view->clickModeLock();
    CHECK (p.isModeLocked());
    if (! wasLocked)
        p.setModeLocked (false);

    // A preset from the browser's list: every control jumps, the name shows it unchanged.
    const auto& lib = p.library();
    const int steel = const_cast<PresetLibrary&> (lib).indexOf ("Steel Ricochet");
    CHECK (steel > 0);
    p.loadPreset (steel);
    p.dispatchPendingMessages();
    CHECK (view->presetText() == "Steel Ricochet");
    CHECK (std::lround (knob (plugin::kModeId)) == 2);
    p.step (1, view->presetBrowser().visiblePresets());
    CHECK (p.presetIndex() == steel + 1);

    editor->setSize (SpaceForgeEditor::kWidth * 3 / 2, SpaceForgeEditor::kHeight * 3 / 2);
    CHECK (editor->getWidth() == SpaceForgeEditor::kWidth * 3 / 2);

    // bleeps with Echo on: the picture sends out rings
    p.prepareToPlay (48000.0, 1600);
    const auto sound = analysis::makeBleeps (48000.0, 1.0).audio;
    juce::AudioBuffer<float> buffer (2, 1600);
    juce::MidiBuffer midi;
    set (plugin::kEchoId, 0.5f);
    for (size_t pos = 0; pos + 1600 <= sound.l.size(); pos += 1600)
    {
        buffer.copyFrom (0, 0, &sound.l[pos], 1600);
        buffer.copyFrom (1, 0, &sound.r[pos], 1600);
        p.processBlock (buffer, midi);
        view->advanceAnimation (1.0f / 30.0f);
    }
    std::printf ("Editor: %d hits heard, output level %.2f\n", p.getOnsetCount(), p.getOutputLevel());
    CHECK (p.getOnsetCount() >= 3);
    CHECK (p.getOutputLevel() > 0.01f);
}

void testStateRoundTrip()
{
    SpaceForgeProcessor a;
    a.applyPreset ([] { Preset x; x.name = "Long Iron"; x.mode = 2; return x; }());
    setParam (a, plugin::kSpaceId, 0.21f);
    setParam (a, plugin::kEchoId, 0.42f);
    setParam (a, plugin::kMotionId, 0.63f);
    setParam (a, plugin::kToneId, 0.84f);
    setParam (a, plugin::kMixId, 0.73f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    SpaceForgeProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_RANGE (paramValue (b, plugin::kMixId), 0.729, 0.731);
    CHECK_RANGE (paramValue (b, plugin::kSpaceId), 0.209, 0.211);
    CHECK_RANGE (paramValue (b, plugin::kEchoId), 0.419, 0.421);
    CHECK_RANGE (paramValue (b, plugin::kMotionId), 0.629, 0.631);
    CHECK_RANGE (paramValue (b, plugin::kToneId), 0.839, 0.841);
    CHECK (b.presetName() == "Long Iron" && b.presetUnchanged()); // the name comes back, the values are the project's
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
    loadXml (p, "<PARAMS stateVersion=\"1\" preset=\"../../x\"><PARAM id=\"mix\" value=\"nan\"/><PARAM id=\"freeze\" value=\"inf\"/></PARAMS>");
    CHECK_RANGE (paramValue (p, plugin::kMixId), plugin::kDefaultMix - 0.001, plugin::kDefaultMix + 0.001);
    CHECK (! p.presetName().containsChar ('/'));

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

void testSettings()
{
    // The settings (per user): accent colour, size, animations, tooltips; kept for the next
    // window. The host's program list holds the presets. About credits the engines.
    SpaceForgeProcessor p;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* view = dynamic_cast<SpaceForgeEditor*> (editor.get());
        const auto mint = theme::accentChoices()[4].colour;
        view->setAccent (mint);
        CHECK (theme::glow == mint);
        view->setUiScale (1.5f);
        CHECK (editor->getWidth() == SpaceForgeEditor::kWidth * 3 / 2);
        view->setAnimations (false);
        CHECK (! view->animationsOn());
        view->clickDice(); // the knobs jump straight there (no glide)
        CHECK (! view->macro (0).isAnimating());
        view->setTooltips (false);
        CHECK (! view->tooltipsOn());
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* view = dynamic_cast<SpaceForgeEditor*> (editor.get());
        CHECK (theme::glow == theme::accentChoices()[4].colour);
        CHECK (std::abs (view->uiScale() - 1.5f) < 0.01f && ! view->animationsOn() && ! view->tooltipsOn());
        view->setAccent (theme::accentChoices().front().colour);
        view->setUiScale (1.0f);
        view->setAnimations (true);
        view->setTooltips (true);
    }
    // a hostile settings file: unknown colour and scale fall back
    p.settings().setValue ("accent", "zzz");
    p.settings().setValue ("scale", "1e309");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* view = dynamic_cast<SpaceForgeEditor*> (editor.get());
        CHECK (theme::glow == theme::accentChoices().front().colour);
        CHECK (std::abs (view->uiScale() - 1.0f) < 0.01f);
    }

    CHECK (p.getNumPrograms() == p.library().size() && p.getNumPrograms() >= 49);
    CHECK (p.getProgramName (0) == "Init");
    const int iron = p.library().indexOf ("Long Iron");
    p.setCurrentProgram (iron);
    CHECK (p.getCurrentProgram() == iron && p.presetName() == "Long Iron");

    const auto about = SpaceForgeEditor::aboutText();
    CHECK (about.contains ("Ghost Note Audio") && about.contains ("not a Ghost Note Audio product") && about.contains ("Emilie Gillet"));
    CHECK (about.contains ("Affero"));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    // Never the user's own settings or presets.
    const juce::TemporaryFile settings ("settings"), folder ("presets");
    SpaceForgeProcessor::setSettingsFileForTests (settings.getFile());
    PresetLibrary::setUserFolderForTests (folder.getFile());
    testProcessing (juce::AudioChannelSet::stereo());
    testProcessing (juce::AudioChannelSet::mono());
    testTexts();
    testEditor();
    testStateRoundTrip();
    testHostileState();
    testSettings();
    folder.getFile().deleteRecursively();
    return finish ("plugin");
}

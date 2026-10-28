// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// Presets: the file format round trip, damaged and hostile files (never a crash, never a value
// out of range, never a name that leads out of the folder), a time-limited fuzz of the loader,
// the sorting of candidates (K/S/undo, saved at once, resumed), and a preset applied to the
// plug-in moves every control. The factory presets: Init first, every one valid and different,
// none named after another product, and each as loud as its Mode at Init (±1 dB, as when sorted).

#include <chrono>
#include <cstdlib>
#include "TestUtils.h"
#include "dsp/Modes.h"
#include "plugin/Defaults.h"
#include "plugin/PluginProcessor.h"
#include "analysis/Measure.h"
#include "analysis/Render.h"
#include "presets/CandidateSorter.h"
#include "presets/PresetLibrary.h"

using namespace spaceforge;

namespace
{
bool inRange (const Preset& p)
{
    for (const float v : { p.space, p.reflect, p.motion, p.tone, p.mix })
        if (! (std::isfinite (v) && v >= 0.0f && v <= 1.0f))
            return false;
    return p.mode >= 0 && p.mode < dsp::kNumModes && p.name.isNotEmpty() && p.name == presets::sanitiseName (p.name);
}

void testRoundTrip()
{
    Preset p;
    p.name = "Slow fog";
    p.mode = 3;
    p.space = 0.71f;
    p.reflect = 0.25f;
    p.motion = 0.9f;
    p.tone = 0.12f;
    p.mix = 0.55f;
    const auto back = presets::fromJson (presets::toJson (p));
    CHECK (back.has_value());
    CHECK (back && back->sameSound (p) && back->name == p.name);
    CHECK (presets::toJson (p).contains ("\"mode\": \"" + juce::String (dsp::modeSetup (3).name) + "\""));

    // Echo, its note and Duck are saved with the preset (the note as text); older files without
    // them mean off and 1/8d, and an unknown note falls back to 1/8d.
    p.delay = 0.6f;
    p.delayNote = 4;
    p.duck = 0.3f;
    const auto json = presets::toJson (p);
    CHECK (json.contains ("\"echoNote\": \"1/4d\"") && json.contains ("\"echo\": 0.6") && json.contains ("\"reflect\""));
    const auto both = presets::fromJson (json);
    CHECK (both && both->sameSound (p) && both->delayNote == 4 && std::abs (both->delay - 0.6f) < 0.001f && std::abs (both->duck - 0.3f) < 0.001f);
    const auto old = presets::fromJson ("{\"name\": \"Old\", \"mode\": \"Sky\", \"space\": 0.5, \"reflect\": 0.2, \"motion\": 0.5, \"tone\": 0.5, \"mix\": 0.4}");
    CHECK (old && std::abs (old->delay) < 1.0e-6f && std::abs (old->duck) < 1.0e-6f && old->delayNote == dsp::MacroPositions::kInitDelayNote && std::abs (old->reflect - 0.2f) < 0.001f);
    const auto odd = presets::fromJson ("{\"name\": \"Odd\", \"echo\": \"x\", \"duck\": 1e999, \"echoNote\": \"1/3\"}");
    CHECK (odd && std::abs (odd->delay) < 1.0e-6f && std::abs (odd->duck) < 1.0e-6f && odd->delayNote == dsp::MacroPositions::kInitDelayNote);
}

void testHostile()
{
    const char* bad[] = {
        "", "null", "[]", "42", "{", "{\"name\": \"\"}", "{\"space\": 0.5}",
        "{\"name\": \"x\", \"space\": \"NaN\", \"echo\": 1e999, \"motion\": -5, \"tone\": [1], \"mix\": {}, \"mode\": 3, \"freeze\": \"yes\"}",
        "{\"name\": \"../../../etc/passwd\", \"mode\": \"Nope\"}",
        "{\"name\": \"..\\\\..\\\\windows\\\\x\"}",
        "{\"name\": \"\\u0007ok\", \"space\": 2}",
        "{\"name\": \"....\"}",
    };
    int loaded = 0;
    for (const auto* text : bad)
        if (const auto p = presets::fromJson (juce::String::fromUTF8 (text)))
        {
            ++loaded;
            CHECK (inRange (*p));
            CHECK (! p->name.containsAnyOf ("/\\") && ! p->name.startsWith ("."));
        }
    CHECK (loaded == 4); // the four with a usable name; everything else refused
    const auto weird = presets::fromJson ("{\"name\": \"x\", \"space\": \"NaN\", \"echo\": 1e999, \"motion\": -5}");
    auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
    CHECK (weird && same (weird->space, dsp::MacroPositions::kInitSpace) && same (weird->reflect, dsp::MacroPositions::kInitReflect) && same (weird->motion, 0.0f));
    CHECK (presets::sanitiseName (juce::String::repeatedString ("a", 500)).length() == presets::kMaxNameLength);

    // Too large: refused unread.
    const auto huge = "{\"name\": \"x\", \"pad\": \"" + juce::String::repeatedString ("x", (int) presets::kMaxFileBytes) + "\"}";
    CHECK (! presets::fromJson (huge).has_value());
}

void testFuzz()
{
    // Random damage to a valid file, for at most 20 s: every result is either refused or valid.
    const auto valid = presets::toJson (presets::makeInit());
    juce::Random random (12345);
    const auto stop = std::chrono::steady_clock::now() + std::chrono::seconds (20);
    int runs = 0, loaded = 0;
    for (; runs < 200000 && std::chrono::steady_clock::now() < stop; ++runs)
    {
        auto text = valid;
        for (int edits = 1 + random.nextInt (6); --edits >= 0;)
        {
            const int at = random.nextInt (text.length() + 1);
            switch (random.nextInt (4))
            {
                case 0: text = text.replaceSection (at, 1, juce::String::charToString ((juce::juce_wchar) random.nextInt (0x3000))); break;
                case 1: text = text.replaceSection (at, random.nextInt (8), {}); break;
                case 2: text = text.replaceSection (at, 0, text.substring (random.nextInt (text.length() + 1)).substring (0, 40)); break;
                default: text = text.replaceSection (at, 0, juce::StringArray { "1e309", "-0", "NaN", "\"..\"", "{}", "[", "\\u0000" }[random.nextInt (7)]); break;
            }
        }
        if (const auto p = presets::fromJson (text))
        {
            ++loaded;
            CHECK (inRange (*p));
        }
    }
    std::printf ("Fuzz: %d damaged files, %d still loaded, all valid\n", runs, loaded);
    CHECK (runs > 1000);
}

void testSorter()
{
    const auto folder = juce::File::createTempFile ("sf2-candidates");
    folder.createDirectory();
    for (int i = 1; i <= 3; ++i)
    {
        Preset p;
        p.name = "Candidate " + juce::String (i);
        p.space = 0.2f * (float) i;
        folder.getChildFile ("0-Sky-0" + juce::String (i) + ".json").replaceWithText (presets::toJson (p));
    }
    folder.getChildFile ("broken.json").replaceWithText ("{ not json");
    {
        CandidateSorter sorter (folder);
        CHECK (sorter.size() == 3);
        CHECK (sorter.position() == 0 && sorter.current() != nullptr && sorter.current()->name == "Candidate 1");
        sorter.decide (CandidateSorter::Decision::keep);
        sorter.decide (CandidateSorter::Decision::skip);
        CHECK (sorter.position() == 2);
        CHECK (sorter.undo());
        CHECK (sorter.position() == 1 && sorter.decisionOf (1) == CandidateSorter::Decision::none);
        sorter.decide (CandidateSorter::Decision::keep);
        CHECK (sorter.count (CandidateSorter::Decision::keep) == 2);
    }
    {
        // Saved at once: a new session goes on at the third, with the first two kept.
        CandidateSorter sorter (folder);
        CHECK (sorter.position() == 2);
        CHECK (sorter.decisionOf (0) == CandidateSorter::Decision::keep && sorter.decisionOf (1) == CandidateSorter::Decision::keep);
        sorter.decide (CandidateSorter::Decision::skip);
        CHECK (sorter.finished() && sorter.current() == nullptr);
        CHECK (sorter.undo() && sorter.position() == 2); // back from "done"
    }
    const auto review = juce::JSON::parse (folder.getChildFile ("review.json"));
    CHECK (review.getProperty ("0-Sky-01", {}).toString() == "keep");
    CHECK (review.getProperty ("0-Sky-03", {}).toString().isEmpty());
    folder.deleteRecursively();
}

void testApply()
{
    SpaceForgeProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    Preset p;
    p.name = "Test";
    p.mode = 4;
    p.space = 0.3f;
    p.reflect = 0.6f;
    p.motion = 0.1f;
    p.tone = 0.8f;
    p.mix = 0.7f;
    processor.applyPreset (p);
    auto value = [&processor] (const char* id) { return processor.parameters().getRawParameterValue (id)->load(); };
    CHECK (std::lround (value (plugin::kModeId)) == 4);
    CHECK (std::abs (value (plugin::kSpaceId) - 0.3f) < 1e-4f && std::abs (value (plugin::kReflectId) - 0.6f) < 1e-4f);
    CHECK (std::abs (value (plugin::kMotionId) - 0.1f) < 1e-4f && std::abs (value (plugin::kToneId) - 0.8f) < 1e-4f);
    CHECK (std::abs (value (plugin::kMixId) - 0.7f) < 1e-4f);
    CHECK (processor.presetName() == "Test" && processor.presetUnchanged());
    processor.parameters().getParameter (plugin::kToneId)->setValueNotifyingHost (0.2f);
    CHECK (! processor.presetUnchanged());

    // The sorting mode's test sound replaces the input (silent input, sound out).
    processor.startAudition();
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int block = 0; block < 400; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        peak = std::max (peak, buffer.getMagnitude (0, 256));
    }
    CHECK (peak > 0.05f);
}
void testFactory()
{
    const auto factory = PresetLibrary::loadFactory();
    std::printf ("Factory presets: %zu\n", factory.size());
    CHECK (factory.size() == 49);
    CHECK (! factory.empty() && factory.front().name == "Init" && factory.front().sameSound (presets::makeInit()));

    // Never a name from SuperMassive, SOL or Cloud Seed, and nothing from the sky.
    const char* banned[] = { "supermassive", "sol", "cloud seed", "cloudseed", "valhalla",
                             // Cloud Seed's programs
                             "dark plate", "chorus delay", "dull echoes", "hyperplane", "medium space", "noise in the hallway", "rubi-ka",
                             "small room", "the 90s are back", "looking glass",
                             // stars, galaxies, constellations
                             "galaxy", "nebula", "star", "andromeda", "orion", "gemini", "hydra", "centaurus", "sagittarius", "lyra", "capricorn",
                             "magellan", "triangulum", "cirrus", "aquarius", "pisces", "scorpio", "libra", "pegasus", "cassiopeia", "cygnus",
                             "draco", "perseus", "aries", "taurus", "virgo", "polaris", "sirius", "vega", "quasar", "pulsar", "cosmos", "comet" };
    for (size_t i = 0; i < factory.size(); ++i)
    {
        const auto& p = factory[i];
        CHECK (inRange (p));
        const auto words = " " + p.name.toLowerCase() + " ";
        for (const auto* b : banned)
        {
            const bool hit = juce::String (b).containsChar (' ') ? words.contains (b) : words.contains (" " + juce::String (b) + " ");
            if (hit)
                std::printf ("FAIL banned name: %s (%s)\n", p.name.toRawUTF8(), b);
            CHECK (! hit);
        }
        for (size_t j = i + 1; j < factory.size(); ++j)
        {
            CHECK (p.name.compareIgnoreCase (factory[j].name) != 0);
            CHECK (! p.sameSound (factory[j]));
        }
    }

    // As loud as the Mode at Init: pink noise wet against dry, and the pad against the Mode's.
    // Measured exactly as sf2_candidates did when they were picked (one fresh engine per preset,
    // pink noise then the pad): Cloud Seed's modulation starts at random phases and a reset
    // does not restart them, which moves the pad's level by a few tenths of a dB.
    constexpr double rate = 48000.0;
    const auto pink = analysis::makePinkNoise (rate, 12.0, 0.125f, 3);
    const auto pad = analysis::makePad (rate, 8.0).audio;
    const auto from = (size_t) (6.0 * rate);
    auto fresh = [] {
        auto engine = std::make_unique<dsp::SpaceEngine>();
        engine->prepare (rate);
        return engine;
    };
    auto render = [&] (dsp::SpaceEngine& engine, const dsp::MacroPositions& k, const analysis::Stereo& input) {
        engine.setMacros (k);
        engine.setTempo (analysis::kTestBpm, true, 0.0);
        return analysis::render (engine, input, rate);
    };
    double padInit[dsp::kNumModes];
    for (int m = 0; m < dsp::kNumModes; ++m)
        padInit[m] = analysis::rmsDb (render (*fresh(), { 0.5f, 0.0f, 0.5f, 0.5f, m }, pad), 0, pad.size());
    double worst = 0.0;
    for (const auto& p : factory)
    {
        if (p.name == "Init")
            continue;
        const dsp::MacroPositions k { p.space, p.reflect, p.motion, p.tone, p.mode };
        auto engine = fresh();
        const double pinkDb = analysis::rmsDb (render (*engine, k, pink), from, pink.size()) - analysis::rmsDb (pink, from, pink.size());
        const double padDb = analysis::rmsDb (render (*engine, k, pad), 0, pad.size()) - padInit[p.mode];
        if (std::abs (pinkDb) > 1.0 || std::abs (padDb) > 1.0)
            std::printf ("  %s: pink %+.2f pad %+.2f dB\n", p.name.toRawUTF8(), pinkDb, padDb);
        worst = std::max ({ worst, std::abs (pinkDb), std::abs (padDb) });
        CHECK_RANGE (pinkDb, -1.05, 1.05);
        CHECK_RANGE (padDb, -1.05, 1.05);
    }
    std::printf ("Factory levels: largest difference from the Mode at Init %.2f dB\n", worst);
}
void testImportRenameDelete()
{
    // Import: real presets come in (written anew, checked), a taken name gets " 2", anything else
    // is refused by name. Rename moves the file; delete goes to the trash (a test trash here).
    const auto from = juce::File::createTempFile ("sf2-import");
    from.createDirectory();
    Preset a;
    a.name = "Init"; // taken by the factory Init
    a.space = 0.33f;
    from.getChildFile ("a.json").replaceWithText (presets::toJson (a));
    from.getChildFile ("b.json").replaceWithText ("{\"name\": \"../evil\", \"space\": \"NaN\", \"mode\": \"Hall\", \"payload\": \"x\"}");
    from.getChildFile ("c.json").replaceWithText ("not a preset");
    PresetLibrary lib;
    const int before = lib.size();
    juce::StringArray refused;
    const int imported = lib.importFiles (from.findChildFiles (juce::File::findFiles, false, "*.json"), refused);
    CHECK (imported == 2);
    CHECK (refused.size() == 1 && refused[0] == "c.json");
    CHECK (lib.size() == before + 2);
    const int copy = lib.indexOf ("Init 2"), evil = lib.indexOf ("evil");
    CHECK (copy > 0 && evil > 0);
    if (copy > 0)
        CHECK (std::abs (lib.at (copy)->space - 0.33f) < 1.0e-3f && ! lib.at (copy)->isFactory);
    if (evil > 0)
    {
        CHECK (lib.at (evil)->file.isAChildOf (PresetLibrary::userFolder()));
        CHECK (! lib.at (evil)->file.loadFileAsString().contains ("payload")); // only the checked values arrive
    }

    const int renamed = lib.renameUserPreset (copy, "Mine");
    CHECK (renamed > 0 && lib.at (renamed)->name == "Mine" && lib.indexOf ("Init 2") < 0);
    CHECK (PresetLibrary::userFolder().getChildFile ("Mine.json").existsAsFile());
    CHECK (lib.renameUserPreset (0, "Nope") < 0); // factory presets never change

#if JUCE_LINUX // (elsewhere the file would go to the user's real trash)
    const juce::TemporaryFile trash ("trash");
    ::setenv ("XDG_DATA_HOME", trash.getFile().getFullPathName().toRawUTF8(), 1); // (Linux: where the trash is)
    CHECK (lib.deleteUserPreset (lib.indexOf ("Mine")));
    CHECK (lib.indexOf ("Mine") < 0 && ! PresetLibrary::userFolder().getChildFile ("Mine.json").existsAsFile());
    CHECK (! lib.deleteUserPreset (0));
    trash.getFile().deleteRecursively();
#endif
    from.deleteRecursively();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    // Never the user's own settings or presets.
    const juce::TemporaryFile settings ("settings"), folder ("presets");
    SpaceForgeProcessor::setSettingsFileForTests (settings.getFile());
    PresetLibrary::setUserFolderForTests (folder.getFile());
    testRoundTrip();
    testHostile();
    testFuzz();
    testSorter();
    testApply();
    testFactory();
    testImportRenameDelete();
    folder.getFile().deleteRecursively();
    return finish ("presets");
}

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// sf2_render: renders the test sounds through SpaceForge 2's engine to WAV files and prints the
// measurements against the targets. Offline, no plug-in host needed.
//
//   sf2_render [--out DIR] [--no-wav] [--macro name=value ...] [--set name=value ...]
//   sf2_render --table        what every knob does at every position, in real units
//   sf2_render --calibrate    measures each knob's level and the Space decay times, prints tables
//   sf2_render --sweeps       WAV files with each knob at 0, 25, 50, 75 and 100 % (renders/makroer)
//
// --macro sets a knob (space, echo, motion, tone; 0..1). --set changes one fixed voicing value
// for this run only (for trying things out), e.g. --set lowShelfDb=-1.2

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include "analysis/Character.h"
#include "analysis/Render.h"
#include "analysis/Wav.h"
#include "plugin/Defaults.h"

using namespace spaceforge;

namespace
{
constexpr double kRate = 48000.0;

std::map<std::string, double*> voicingFields (dsp::SpaceVoicing& v)
{
    return { { "crossFeed", &v.crossFeed }, { "inputLowCutHz", &v.inputLowCutHz }, { "earlyFeedback", &v.earlyFeedback },
             { "earlyModMs", &v.earlyModMs }, { "earlyModHz", &v.earlyModHz }, { "earlyLevelDb", &v.earlyLevelDb },
             { "preDelayMs", &v.preDelayMs }, { "earlyDelayMs", &v.earlyDelayMs }, { "lineSizeMs", &v.lineSizeMs },
             { "lateDiffuseDelayMs", &v.lateDiffuseDelayMs }, { "lateDiffuseFeedback", &v.lateDiffuseFeedback },
             { "lateLevelDb", &v.lateLevelDb }, { "lowShelfHz", &v.lowShelfHz }, { "lowShelfDb", &v.lowShelfDb },
             { "highShelfHz", &v.highShelfHz }, { "crossSeed", &v.crossSeed }, { "monoBassHz", &v.monoBassHz },
             { "wetGainDb", &v.wetGainDb }, { "texture.position", &v.texture.position }, { "texture.size", &v.texture.size },
             { "texture.stereoSpread", &v.texture.stereoSpread }, { "texture.density", &v.texture.density },
             { "texture.texture", &v.texture.texture }, { "texture.feedback", &v.texture.feedback },
             { "texture.gainDb", &v.texture.gainDb }, { "skyLimitDb", &v.skyLimitDb } };
}

std::map<std::string, int*> voicingIntFields (dsp::SpaceVoicing& v)
{
    return { { "earlyStages", &v.earlyStages }, { "lineCount", &v.lineCount }, { "lateStages", &v.lateStages },
             { "seedTap", &v.seedTap }, { "seedDiffusion", &v.seedDiffusion }, { "seedDelay", &v.seedDelay },
             { "seedPostDiffusion", &v.seedPostDiffusion } };
}

bool split (const std::string& setting, std::string& name, double& value)
{
    const auto eq = setting.find ('=');
    if (eq == std::string::npos)
        return false;
    name = setting.substr (0, eq);
    value = std::atof (setting.c_str() + eq + 1);
    return true;
}

bool applySetting (dsp::SpaceVoicing& v, const std::string& setting)
{
    std::string name;
    double value = 0.0;
    if (! split (setting, name, value))
        return false;
    if (auto f = voicingFields (v); f.count (name))
        return *f[name] = value, true;
    if (auto f = voicingIntFields (v); f.count (name))
        return *f[name] = (int) value, true;
    return false;
}

float* macroField (dsp::MacroPositions& m, const std::string& name)
{
    if (name == "space") return &m.space;
    if (name == "echo") return &m.echo;
    if (name == "motion") return &m.motion;
    if (name == "tone") return &m.tone;
    return nullptr;
}

bool applyMacro (dsp::MacroPositions& m, const std::string& setting)
{
    std::string name;
    double value = 0.0;
    if (! split (setting, name, value))
        return false;
    if (auto* f = macroField (m, name))
        return *f = (float) value, true;
    return false;
}

const char* kMacroNames[] = { "space", "echo", "motion", "tone" };

std::unique_ptr<dsp::SpaceEngine> makeEngine (const dsp::SpaceVoicing& voicing, const dsp::MacroPositions& macros)
{
    std::srand (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setVoicing (voicing);
    engine->setMacros (macros);
    engine->prepare (kRate);
    engine->setTempo (analysis::kTestBpm, true, 0.0);
    return engine;
}

// Wet level of steady pink noise against the dry level (the last 6 of 12 s).
double wetLevel (dsp::SpaceEngine& engine, const analysis::Stereo& pink)
{
    engine.setTempo (analysis::kTestBpm, true, 0.0);
    const auto from = (size_t) (6.0 * kRate);
    return analysis::rmsDb (analysis::render (engine, pink, kRate), from, pink.size()) - analysis::rmsDb (pink, from, pink.size());
}

void printTable()
{
    std::printf ("Hvad knapperne gør, pr. position (de andre knapper på Init):\n\n");
    std::printf ("SPACE   tekst      decay-indst.  bredde  sky ind    sky ud     niveau-komp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.space = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-9s  %6.1f s      %4.2f    %+5.1f dB   %+5.1f dB   %+5.1f dB\n", i * 10, dsp::describeSpace (x).c_str(),
                     s.spaceDecaySeconds, s.spaceWidth, s.spaceSkyFeedDb, s.spaceSkyOutDb, s.spaceGainDb);
    }
    std::printf ("\nECHO    tekst          send   feedback  niveau-komp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.echo = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-13s  %4.2f   %4.2f      %+5.1f dB\n", i * 10, dsp::describeEcho (x).c_str(), s.echoSend, s.echoFeedback, s.echoGainDb);
    }
    std::printf ("\nMOTION  tekst                                 mod.     pan      niveau-komp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.motion = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-36s  %4.1f ct  %4.2f    %+5.1f dB\n", i * 10, dsp::describeMotion (x).c_str(),
                     analysis::modulationCents (s.motionLineModMs, s.motionLineModHz), s.motionPanDepth, s.motionGainDb);
    }
    std::printf ("\nTONE    tekst              dæmpning  diskant  input-top  ekko-dæmpning  finish  niveau-komp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.tone = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-17s  %5.0f Hz  %+4.1f dB  %5.0f Hz   %5.0f Hz       %+4.1f dB %+5.1f dB\n", i * 10, dsp::describeTone (x).c_str(),
                     s.toneLowpassHz, s.toneHighShelfDb, s.toneInputHighCutHz, s.toneEchoDampingHz, s.toneTiltDb, s.toneGainDb);
    }
}

void calibrate (const dsp::SpaceVoicing& voicing)
{
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 3);

    // Each knob's overall wet level against Init (with the current compensation tables), and the
    // suggested tables that cancel what is left.
    for (const auto* name : kMacroNames)
    {
        std::printf ("%s: wet level against dry, current table applied:\n  ", name);
        std::string suggestion = "  {";
        for (int i = 0; i <= 10; ++i)
        {
            dsp::MacroPositions p;
            *macroField (p, name) = (float) i / 10.0f;
            auto engine = makeEngine (voicing, p);
            const double level = wetLevel (*engine, pink);
            const auto s = dsp::mapMacros (p);
            const double current = std::string (name) == "space" ? s.spaceGainDb : std::string (name) == "echo" ? s.echoGainDb
                                 : std::string (name) == "motion" ? s.motionGainDb : s.toneGainDb;
            std::printf (" %+.2f", level);
            char text[32];
            std::snprintf (text, sizeof (text), " %.2f%s", current - level, i < 10 ? "," : " }");
            suggestion += text;
            std::fflush (stdout);
        }
        std::printf ("\n%s  <- suggested\n", suggestion.c_str());
    }

    // Space: the real tail at each position, sky included, of a noise burst (the sky's tail
    // depends on the level; a click is far softer than music). Slow: some 15 minutes.
    std::printf ("space: measured T60 at 1 kHz:\n  {");
    for (int i = 0; i <= 10; ++i)
    {
        dsp::MacroPositions p;
        p.space = (float) i / 10.0f;
        auto engine = makeEngine (voicing, p);
        const auto ir = analysis::render (*engine, analysis::makeNoiseBurst (kRate, 90.0).audio, kRate);
        std::printf (" %.2f%s", analysis::reverbTimePerOctave (ir, kRate)[3], i < 10 ? "," : " }  <- kSpaceMeasuredT60\n");
        std::fflush (stdout);
    }
}

void sweeps (const dsp::SpaceVoicing& voicing, const std::string& outDir)
{
    // Each knob from left to right, the others at Init; the sounds that show that knob best.
    const std::map<std::string, std::vector<std::string>> sounds { { "space", { "klik", "pad" } }, { "echo", { "figur", "pad" } },
                                                                   { "motion", { "figur", "pad" } }, { "tone", { "hihat", "pad" } } };
    std::filesystem::create_directories (outDir);
    const auto signals = analysis::allTestSignals (kRate, 20.0);
    for (const auto* name : kMacroNames)
        for (const auto& sound : sounds.at (name))
            for (const int percent : { 0, 25, 50, 75, 100 })
            {
                dsp::MacroPositions p;
                *macroField (p, name) = (float) percent / 100.0f;
                auto engine = makeEngine (voicing, p);
                for (const auto& signal : signals)
                    if (signal.name == sound)
                    {
                        char file[128];
                        std::snprintf (file, sizeof (file), "%s/%s_%03d_%s.wav", outDir.c_str(), name, percent, sound.c_str());
                        analysis::writeWav (file, analysis::render (*engine, signal.audio, kRate), kRate);
                    }
                std::printf ("  %s %3d %% %s\n", name, percent, sound.c_str());
                std::fflush (stdout);
            }
}
} // namespace

int main (int argc, char** argv)
{
    std::string outDir = "renders";
    bool writeFiles = true, table = false, calibration = false, sweep = false;
    dsp::SpaceVoicing voicing;
    dsp::MacroPositions macros;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--out") == 0 && i + 1 < argc)
            outDir = argv[++i];
        else if (std::strcmp (argv[i], "--no-wav") == 0)
            writeFiles = false;
        else if (std::strcmp (argv[i], "--table") == 0)
            table = true;
        else if (std::strcmp (argv[i], "--calibrate") == 0)
            calibration = true;
        else if (std::strcmp (argv[i], "--sweeps") == 0)
            sweep = true;
        else if (std::strcmp (argv[i], "--set") == 0 && i + 1 < argc && applySetting (voicing, argv[i + 1]))
            ++i;
        else if (std::strcmp (argv[i], "--macro") == 0 && i + 1 < argc && applyMacro (macros, argv[i + 1]))
            ++i;
        else
        {
            std::fprintf (stderr, "usage: sf2_render [--out DIR] [--no-wav] [--macro name=value ...] [--set name=value ...]\n"
                                  "       sf2_render --table | --calibrate | --sweeps\n");
            return 2;
        }
    }
    if (table)
        return printTable(), 0;
    if (calibration)
        return calibrate (voicing), 0;
    if (sweep)
        return sweeps (voicing, outDir + "/makroer"), 0;

    auto engine = makeEngine (voicing, macros);
    const float mix = plugin::kDefaultMix;
    std::printf ("Knapper: Space %s, Echo %s, Motion %s, Tone %s\n\n", dsp::describeSpace (macros.space).c_str(), dsp::describeEcho (macros.echo).c_str(),
                 dsp::describeMotion (macros.motion).c_str(), dsp::describeTone (macros.tone).c_str());

    if (writeFiles)
    {
        std::filesystem::create_directories (outDir);
        std::printf ("Lydfiler i %s/ (48 kHz, 24 bit):\n", outDir.c_str());
        auto write = [&] (const std::string& name, const analysis::Stereo& s) {
            const auto path = outDir + "/" + name + ".wav";
            if (! analysis::writeWav (path, s, kRate))
                std::fprintf (stderr, "kunne ikke skrive %s\n", path.c_str());
        };
        for (const auto& signal : analysis::allTestSignals (kRate, 20.0))
        {
            analysis::RenderOptions wet, mixed;
            mixed.mix = mix;
            write (signal.name + "_1_toer", signal.audio);
            engine->setTempo (analysis::kTestBpm, true, 0.0);
            write (signal.name + "_2_vaad", analysis::render (*engine, signal.audio, kRate, wet));
            engine->setTempo (analysis::kTestBpm, true, 0.0);
            write (signal.name + "_3_mix", analysis::render (*engine, signal.audio, kRate, mixed));
            std::printf ("  %-6s  %s\n", signal.name.c_str(), signal.description.c_str());
        }
        // Freeze: the pad is frozen at 3 s, held for 10 s, then released.
        analysis::RenderOptions freeze;
        freeze.freezeOnSeconds = 3.0;
        freeze.freezeOffSeconds = 13.0;
        write ("pad_4_freeze", analysis::render (*engine, analysis::makePad (kRate, 30.0).audio, kRate, freeze));
        std::printf ("  pad_4_freeze  Pad'en fryses ved 3 s, holdes i 10 s og slippes igen.\n");
        std::printf ("  (_1_toer = uden effekt, _2_vaad = kun effekten, _3_mix = standard-mix %d %%)\n\n", (int) (mix * 100.0f + 0.5f));
    }

    // CPU: 20 s of pink noise in 256-sample blocks, the time against real time.
    {
        const auto pink = analysis::makePinkNoise (kRate, 20.0, 0.125f, 5);
        analysis::RenderOptions options;
        options.blockSize = 256;
        const auto start = std::chrono::steady_clock::now();
        analysis::render (*engine, pink, kRate, options);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::printf ("CPU: %.1f %% af én kerne ved 48 kHz (20 s lyd på %.2f s)\n\n", seconds / 20.0 * 100.0, seconds);
    }

    const auto c = analysis::measureCharacter (*engine, kRate, mix);
    std::printf ("Halelængde pr. oktav (T60):\n ");
    for (size_t b = 0; b < analysis::kOctaveBands.size(); ++b)
        std::printf ("  %5.0f Hz", analysis::kOctaveBands[b]);
    std::printf ("\n ");
    for (const double t : c.t60Band)
        std::printf ("  %6.1f s", t);
    std::printf ("\n\nLysstyrke (spektralt tyngdepunkt): tør %.0f Hz, hale %.0f Hz\n", c.centroidDry, c.centroidWet);
    std::printf ("Ideel hale til sammenligning: ringen %.1f dB, ujævnhed %.2f dB\n", c.baseline.ringingDb, c.baseline.flutterDb);
    std::printf ("Clouds' ændring af synth-figuren: %.2f dB (rummets egen tilfældighed: %.2f dB)\n\n", c.textureChangeDb, c.randomChangeDb);

    int misses = 0;
    const auto targets = analysis::targetsFor (c);
    std::printf ("%-55s %9s   %s\n", "Måling", "Værdi", "Mål");
    for (const auto& t : targets)
    {
        const bool ok = t.value >= t.lo && t.value <= t.hi;
        misses += ok ? 0 : 1;
        std::printf ("%-55s %7.2f %-4s %s %g..%g\n", t.what.c_str(), t.value, t.unit.c_str(), ok ? "ok " : "-- ", t.lo, t.hi);
    }
    std::printf ("\n%d af %zu mål ramt.\n", (int) targets.size() - misses, targets.size());
    return 0;
}

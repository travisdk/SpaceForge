// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// sf2_render: renders the test sounds through SpaceForge's engine to WAV files and prints the
// measurements against the targets. Offline, no plug-in host needed.
//
//   sf2_render [--out DIR] [--no-wav] [--macro name=value ...] [--set name=value ...]
//   sf2_render --table        what every knob does at every position, in real units
//   sf2_render --calibrate    measures each knob's level and the Space decay times, prints tables
//   sf2_render --sweeps       WAV files with each knob at 0, 25, 50, 75 and 100 % (renders/makroer)
//   sf2_render --room         Room: hi-hat and clap at Space 0, 50 and 100 % and the Room presets
//                             (renders/room, run from the project folder), with a table
//   sf2_render --echo-duck    a bleep through Echo and Duck at 0, 50 and 100 % in three Modes
//                             (renders/echo-duck), with a table of what was measured
//
// --macro sets a knob (space, reflect, echo, motion, tone; 0..1). --set changes one fixed voicing value
// for this run only (for trying things out), e.g. --set lowShelfDb=-1.2

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <xmmintrin.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <memory>
#include <string>
#include "analysis/Character.h"
#include "analysis/Measure.h"
#include "analysis/Render.h"
#include "analysis/Wav.h"
#include "dsp/Modes.h"
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
    if (name == "reflect") return &m.reflect;
    if (name == "echo") return &m.delay; // the Echo knob
    if (name == "duck") return &m.duck;
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

const char* kMacroNames[] = { "space", "reflect", "motion", "tone" };

std::unique_ptr<dsp::SpaceEngine> makeEngine (const dsp::SpaceVoicing& voicing, const dsp::MacroPositions& macros)
{
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
    std::printf ("What the knobs do, per position (the other knobs at Init):\n\n");
    std::printf ("SPACE   text       decay setting width   cloud in   cloud out  level comp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.space = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-9s  %6.1f s      %4.2f    %+5.1f dB   %+5.1f dB   %+5.1f dB\n", i * 10, dsp::describeSpace (x).c_str(),
                     s.spaceDecaySeconds, s.spaceWidth, s.spaceSkyFeedDb, s.spaceSkyOutDb, s.spaceGainDb);
    }
    std::printf ("\nECHO    text           send   feedback  level comp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.reflect = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-13s  %4.2f   %4.2f      %+5.1f dB\n", i * 10, dsp::describeReflect (x).c_str(), s.reflectSend, s.reflectFeedback, s.reflectGainDb);
    }
    std::printf ("\nMOTION  text                                  mod.     pan      level comp.\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float x = (float) i / 10.0f;
        dsp::MacroPositions p;
        p.motion = x;
        const auto s = dsp::mapMacros (p);
        std::printf ("  %3d %%  %-36s  %4.1f ct  %4.2f    %+5.1f dB\n", i * 10, dsp::describeMotion (x).c_str(),
                     analysis::modulationCents (s.motionLineModMs, s.motionLineModHz), s.motionPanDepth, s.motionGainDb);
    }
    std::printf ("\nTONE    text               damping   treble   input top  echo damping  finish  level comp.\n");
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
            const double current = std::string (name) == "space" ? s.spaceGainDb : std::string (name) == "reflect" ? s.reflectGainDb
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

// An engine in one Mode (its voicing changed by `adjust`, for calibrating).
std::unique_ptr<dsp::SpaceEngine> makeModeEngine (int mode, dsp::MacroPositions macros, const std::function<void (dsp::SpaceVoicing&)>& adjust = {})
{
    auto engine = std::make_unique<dsp::SpaceEngine>();
    macros.mode = mode;
    engine->setMacros (macros);
    engine->prepare (kRate);
    if (adjust)
    {
        auto v = engine->voicing();
        adjust (v);
        engine->setVoicing (v);
        engine->reset();
    }
    engine->setTempo (analysis::kTestBpm, true, 0.0);
    return engine;
}

// T60 at 1 kHz of a noise burst (the sky's tail depends on the level; a click is far softer
// than music), with `tail` seconds after it.
double burstT60 (dsp::SpaceEngine& engine, double tail, int mode)
{
    // A Mode without a sky (Room) does not depend on the level, and a noise burst cannot show a tail
    // under about 0.3 s: its T60 is measured on an impulse.
    if (! dsp::modeSetup (mode).hasSky())
        return analysis::reverbTimePerOctave (analysis::render (engine, analysis::makeImpulse (kRate, tail).audio, kRate), kRate)[3];
    const auto ir = analysis::render (engine, analysis::makeNoiseBurst (kRate, tail).audio, kRate);
    return analysis::reverbTimePerOctave (ir, kRate)[3];
}

std::string row (const std::array<double, 11>& values)
{
    std::string text = "{";
    for (size_t i = 0; i < values.size(); ++i)
    {
        char number[32];
        std::snprintf (number, sizeof (number), " %.2f%s", values[i], i + 1 < values.size() ? "," : " }");
        text += number;
    }
    return text;
}

// One Mode, in two passes (each needs the tables the one before wrote; tools/modes/apply_calibration.py
// puts the lines starting with "TABLE" into source/dsp/ModeTables.h).
//   pass 1: Clouds' level, and Space at 50 %: a medium room (the Mode's mediumSeconds, 12 s but Room).
//   pass 2: the Mode's level at Init, each knob's level compensation, the Space text's T60s.
//   pass 3: pass 2 without the T60s (levels only).
void calibrateMode (int mode, int pass)
{
    namespace cal = dsp::calibrated;
    const auto m = (size_t) mode;
    const auto& setup = dsp::modeSetup (mode);
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 3);
    const auto from = (size_t) (6.0 * kRate);
    std::printf ("Mode %d (%s), pass %d\n", mode, setup.name, pass);

    if (pass == 1)
    {
        // Clouds' grains as loud as what goes in (steady pink noise)
        dsp::TextureEngine texture;
        texture.prepare (kRate);
        auto tv = dsp::modeVoicing (mode).texture;
        texture.setVoicing (tv);
        texture.reset();
        analysis::Stereo grains { std::vector<float> (pink.size()), std::vector<float> (pink.size()) };
        for (size_t pos = 0; pos + 256 <= pink.size(); pos += 256)
            texture.process (&pink.l[pos], &pink.r[pos], &grains.l[pos], &grains.r[pos], 256);
        const double textureGain = tv.gainDb - (analysis::rmsDb (grains, from, pink.size()) - analysis::rmsDb (pink, from, pink.size()));
        std::printf ("TABLE kTextureGainDb %d %.2f\n", mode, textureGain);

        // Space at 50 %: the sky's feed or the room's decay, whichever carries this Mode's tail
        const double target = setup.mediumSeconds;
        double lo = setup.skyCarries ? -45.0 : std::log (0.2);
        double hi = setup.skyCarries ? 0.0 : std::log (60.0);
        double decay = cal::kDecayHalfSeconds[m], feed = cal::kSkyFeedHalfDb[m], t60 = 0.0;
        for (int step = 0; step < 9; ++step)
        {
            const double mid = 0.5 * (lo + hi);
            (setup.skyCarries ? feed : decay) = setup.skyCarries ? mid : std::exp (mid);
            dsp::setSpaceCalibrationOverride (mode, decay, feed);
            dsp::MacroPositions p;
            p.space = 0.5f;
            auto engine = makeModeEngine (mode, p, [textureGain] (dsp::SpaceVoicing& v) { v.texture.gainDb = textureGain; });
            t60 = burstT60 (*engine, 40.0, mode);
            (t60 < target ? lo : hi) = mid;
            std::printf ("  decay %.2f s, sky %.1f dB: T60 %.2f s\n", decay, feed, t60);
            std::fflush (stdout);
        }
        std::printf ("TABLE kDecayHalfSeconds %d %.3f\nTABLE kSkyFeedHalfDb %d %.2f\n", mode, decay, mode, feed);
        return;
    }

    // pass 2: levels (pink noise) against the dry signal, the current tables applied
    auto level = [&] (const dsp::MacroPositions& p) {
        auto engine = makeModeEngine (mode, p);
        return wetLevel (*engine, pink);
    };
    const double init = level ({});
    std::printf ("TABLE kModeGainDb %d %.2f\n", mode, cal::kModeGainDb[m] - init);
    const std::array<const std::array<std::array<double, 11>, dsp::kNumModes>*, 4> tables { &cal::kSpaceGainDb, &cal::kReflectGainDb,
                                                                                         &cal::kMotionGainDb, &cal::kToneGainDb };
    const char* tableNames[] = { "kSpaceGainDb", "kReflectGainDb", "kMotionGainDb", "kToneGainDb" };
    for (int k = 0; k < 4; ++k)
    {
        std::array<double, 11> suggestion {};
        for (int i = 0; i <= 10; ++i)
        {
            dsp::MacroPositions p;
            *macroField (p, kMacroNames[k]) = (float) i / 10.0f;
            suggestion[(size_t) i] = (*tables[(size_t) k])[m][(size_t) i] - (level (p) - init);
        }
        std::printf ("TABLE %s %d %s\n", tableNames[k], mode, row (suggestion).c_str());
        std::fflush (stdout);
    }
    if (pass == 3)
        return; // levels only
    std::array<double, 11> t60 {};
    for (int i = 0; i <= 10; ++i)
    {
        dsp::MacroPositions p;
        p.space = (float) i / 10.0f;
        auto engine = makeModeEngine (mode, p);
        t60[(size_t) i] = burstT60 (*engine, 90.0, mode);
        std::fflush (stdout);
    }
    std::printf ("TABLE kSpaceMeasuredT60 %d %s\n", mode, row (t60).c_str());
}

// Every Mode with the same knobs (Space 50 %, Echo 0, Motion 50 %, Tone 50 %): how long, how
// bright, how much movement, how quickly dense, how different from each other, and short renders
// to listen to in <outDir> (mix 40 %, all at the same level).
void modesReport (const std::string& outDir)
{
    std::filesystem::create_directories (outDir);
    const auto pad = analysis::makePad (kRate, 12.0).audio;
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 3);
    std::vector<analysis::Stereo> pads;
    std::printf ("Modes ved Space 50 %%, Echo 0, Motion 50 %%, Tone 50 %%:\n\n");
    std::printf ("  %-9s %9s %12s %13s %11s %10s %9s\n", "Mode", "T60 1 kHz", "treble/mid", "centroid", "motion", "dense after", "level");
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        dsp::MacroPositions p;
        p.space = 0.5f;
        p.motion = 0.5f;
        p.tone = 0.5f;
        auto engine = makeModeEngine (mode, p);
        const auto ir = analysis::render (*engine, analysis::makeNoiseBurst (kRate, 40.0).audio, kRate);
        const auto t = analysis::reverbTimePerOctave (ir, kRate);
        const auto wet = analysis::render (*engine, pad, kRate);
        const double centroid = analysis::spectralCentroid (wet, kRate, (size_t) (4.0 * kRate), (size_t) (9.0 * kRate));
        const double movement = analysis::tailMovementDb (wet, kRate, (size_t) (4.0 * kRate), (size_t) (12.0 * kRate));
        const double dense = analysis::densityTimeMs (ir, kRate);
        const double level = wetLevel (*engine, pink);
        std::printf ("  %-9s %7.1f s %10.2f x %10.0f Hz %8.2f dB %7.0f ms %+7.1f dB\n", dsp::modeSetup (mode).name, t[3], t[5] / t[3], centroid, movement,
                     dense, level);
        std::fflush (stdout);
        pads.push_back (wet);

        // to listen to
        char prefix[64];
        std::snprintf (prefix, sizeof (prefix), "%s/%d_%s_", outDir.c_str(), mode + 1, dsp::modeSetup (mode).name);
        analysis::RenderOptions mixed;
        mixed.mix = plugin::kDefaultMix;
        analysis::writeWav (std::string (prefix) + "pad.wav", analysis::render (*engine, analysis::makePad (kRate, 10.0).audio, kRate, mixed), kRate);
        analysis::writeWav (std::string (prefix) + "bleeps.wav", analysis::render (*engine, analysis::makeBleeps (kRate, 10.0).audio, kRate, mixed), kRate);
        analysis::writeWav (std::string (prefix) + "figur.wav", analysis::render (*engine, analysis::makeSynthFigure (kRate, 10.0).audio, kRate, mixed), kRate);
    }

    // How different: the pad's tail, band by band over time (dB), each Mode against each other.
    // For scale: the same Mode with another random start.
    dsp::MacroPositions p;
    p.space = 0.5f;
    auto other = std::make_unique<dsp::SpaceEngine>();
    other->setModulationSeed (99);
    other->setMacros (p);
    other->prepare (kRate);
    const auto from = (size_t) (0.5 * kRate), to = (size_t) (12.0 * kRate);
    std::printf ("\nDifference in the pad's tail, dB (same Mode with other randomness: %.1f dB):\n  %-9s", analysis::spectroTemporalDifferenceDb (analysis::render (*other, pad, kRate), pads[0], kRate, from, to), "");
    for (int b = 0; b < dsp::kNumModes; ++b)
        std::printf (" %8s", dsp::modeSetup (b).name);
    std::printf ("\n");
    for (int a = 0; a < dsp::kNumModes; ++a)
    {
        std::printf ("  %-9s", dsp::modeSetup (a).name);
        for (int b = 0; b < dsp::kNumModes; ++b)
            if (b <= a)
                std::printf (" %8s", "");
            else
                std::printf (" %8.1f", analysis::spectroTemporalDifferenceDb (pads[(size_t) a], pads[(size_t) b], kRate, from, to));
        std::printf ("\n");
    }
    std::printf ("\nLydfiler i %s/: <nr>_<Mode>_pad/bleeps/figur.wav (mix 40 %%)\n", outDir.c_str());
}

// Echo and Duck (the small controls) on a bleep, at 0, 50 and 100 %, in a few Modes. The table
// says what each does: Echo, the first repeats (left at the note, right at 4/3 of it) against
// just before them, and the number of repeats; Duck, how far the wet signal sinks while the
// bleeps play and that it is back later.
void echoDuck (const dsp::SpaceVoicing& voicing, const std::string& outDir)
{
    std::filesystem::create_directories (outDir);
    const auto bleeps = analysis::makeBleeps (kRate, 12.0).audio;
    const double beat = 60.0 / analysis::kTestBpm, t = dsp::delayNoteBeats (dsp::MacroPositions::kInitDelayNote) * beat;
    const auto peakDb = [] (const std::vector<float>& side, double from, double to) {
        float p = 1.0e-9f;
        for (size_t i = (size_t) (from * kRate); i < std::min (side.size(), (size_t) (to * kRate)); ++i)
            p = std::max (p, std::abs (side[i]));
        return 20.0 * std::log10 (p);
    };
    std::printf ("Echo: first bleep at 0 s, 1/8d at %.0f BPM = %.2f s on the left, %.2f s on the right\n", analysis::kTestBpm, t, t * 4.0 / 3.0);
    std::printf ("%-9s %-4s %-12s %14s %14s %12s\n", "Mode", "Echo", "text", "left repeat", "right repeat", "left vs dry");
    std::printf ("%-9s %-4s %-12s %14s %14s %12s\n", "", "", "", "over before", "over before", "(peak)");
    for (const int mode : { 0, 1, 5 })
        for (const int percent : { 0, 50, 100 })
        {
            dsp::MacroPositions p;
            p.mode = mode;
            p.space = 0.5f;
            p.delay = (float) percent / 100.0f;
            auto engine = makeEngine (voicing, p);
            const auto wet = analysis::render (*engine, bleeps, kRate);
            engine->setTempo (analysis::kTestBpm, true, 0.0);
            analysis::RenderOptions mixed;
            mixed.mix = plugin::kDefaultMix;
            char file[160];
            std::snprintf (file, sizeof (file), "%s/echo_%s_%03d.wav", outDir.c_str(), dsp::modeSetup (mode).name, percent);
            analysis::writeWav (file, analysis::render (*engine, bleeps, kRate, mixed), kRate);
            // the first bleep alone: its repeats against what the room does just before them
            const double l = peakDb (wet.l, t - 0.004, t + 0.012) - peakDb (wet.l, t - 0.06, t - 0.02);
            const double r = peakDb (wet.r, t * 4.0 / 3.0 - 0.004, t * 4.0 / 3.0 + 0.012) - peakDb (wet.r, t * 4.0 / 3.0 - 0.06, t * 4.0 / 3.0 - 0.02);
            const double vsDry = peakDb (wet.l, t - 0.004, t + 0.012) - peakDb (bleeps.l, 0.0, 0.08);
            std::printf ("%-9s %3d%% %-12s %+11.1f dB %+11.1f dB %+9.1f dB\n", dsp::modeSetup (mode).name, percent,
                         dsp::describeDelay ((float) percent / 100.0f, p.delayNote).c_str(), l, r, vsDry);
        }

    std::printf ("\nDuck: the wet level while the bleeps play (0 to 2 s) and 8 to 11 s later, against Duck 0\n");
    std::printf ("%-9s %-5s %-6s %12s %12s\n", "Mode", "Duck", "text", "playing", "later");
    for (const int mode : { 0, 1, 5 })
    {
        double base[2] = { 0.0, 0.0 };
        for (const int percent : { 0, 50, 100 })
        {
            dsp::MacroPositions p;
            p.mode = mode;
            p.space = 0.5f;
            p.duck = (float) percent / 100.0f;
            auto engine = makeEngine (voicing, p);
            const auto wet = analysis::render (*engine, bleeps, kRate);
            const double playing = analysis::rmsDb (wet, 0, (size_t) (2.0 * kRate)), later = analysis::rmsDb (wet, (size_t) (8.0 * kRate), (size_t) (11.0 * kRate));
            if (percent == 0)
                base[0] = playing, base[1] = later;
            analysis::RenderOptions mixed;
            mixed.mix = plugin::kDefaultMix;
            char file[160];
            std::snprintf (file, sizeof (file), "%s/duck_%s_%03d.wav", outDir.c_str(), dsp::modeSetup (mode).name, percent);
            analysis::writeWav (file, analysis::render (*engine, bleeps, kRate, mixed), kRate);
            std::printf ("%-9s %4d%% %-6s %+9.1f dB %+9.1f dB\n", dsp::modeSetup (mode).name, percent, dsp::describeDuck ((float) percent / 100.0f).c_str(),
                         playing - base[0], later - base[1]);
        }
    }
    std::printf ("\nWAV files in %s/: echo_<Mode>_<percent>.wav and duck_<Mode>_<percent>.wav (a bleep, Space 50 %%, Mix %d %%)\n", outDir.c_str(),
                 (int) (plugin::kDefaultMix * 100.0f + 0.5f));
}

// Room: hi-hat and clap at Space 0, 50 and 100 %, and the presets in Room (read from
// presets/factory), mix 40 %. The table: the Space text, and how long after the last hit the wet
// signal is 40 dB under its loudest part.
void roomRenders (const std::string& outDir)
{
    std::filesystem::create_directories (outDir);
    const int room = dsp::kNumModes - 1;
    const auto hats = analysis::makeHiHats (kRate, 4.0), claps = analysis::makeClaps (kRate, 4.0);
    // the last audible moment of the dry sound (1 % of its peak)
    const auto hitsEnd = [] (const analysis::TestSignal& s) {
        float peak = 0.0f;
        for (const float x : s.audio.l)
            peak = std::max (peak, std::abs (x));
        size_t last = 0;
        for (size_t i = 0; i < s.audio.l.size(); ++i)
            if (std::abs (s.audio.l[i]) > 0.01f * peak)
                last = i;
        return last;
    };
    const auto tail40 = [&] (const analysis::Stereo& wet, size_t end) {
        std::vector<double> db;
        for (size_t at = 0; at + 480 <= wet.size(); at += 480)
            db.push_back (analysis::rmsDb (wet, at, at + 480));
        const double top = *std::max_element (db.begin(), db.end());
        size_t last = end / 480;
        while (last + 1 < db.size() && db[last + 1] > top - 40.0)
            ++last;
        return (double) (last * 480 - std::min (last * 480, end)) / kRate * 1000.0;
    };
    const auto render = [&] (const std::string& label, const dsp::MacroPositions& p, const analysis::TestSignal& sound, const std::string& spaceText) {
        auto engine = makeModeEngine (room, p);
        const auto wet = analysis::render (*engine, sound.audio, kRate);
        engine->setTempo (analysis::kTestBpm, true, 0.0);
        analysis::RenderOptions mixed;
        mixed.mix = plugin::kDefaultMix;
        analysis::writeWav (outDir + "/" + label + "_" + sound.name + ".wav", analysis::render (*engine, sound.audio, kRate, mixed), kRate);
        std::printf ("%-18s %-6s Space %-7s %8.0f ms\n", label.c_str(), sound.name.c_str(), spaceText.c_str(), tail40 (wet, hitsEnd (sound)));
    };
    // Ringing and flutter against an ideal tail of the same T60 (limits as in tests/test_modes.cpp:
    // 1.5 dB ringing; flutter 1 dB, 2 dB at a Space under 25 %). Tiled Corridor's flutter is meant.
    std::vector<std::string> smoothRows;
    const auto smooth = [&] (const std::string& label, const dsp::MacroPositions& p, bool flutterIntended) {
        auto engine = makeModeEngine (room, p);
        const auto ir = analysis::render (*engine, analysis::makeImpulse (kRate, 3.0).audio, kRate);
        const double t60 = analysis::reverbTimePerOctave (ir, kRate)[3];
        engine = makeModeEngine (room, p);
        const auto tail = analysis::render (*engine, analysis::makeNoiseBurst (kRate, 3.0).audio, kRate);
        const size_t from = (size_t) (0.35 * kRate), to = (size_t) ((0.35 + std::max (0.3, 1.6 * t60)) * kRate);
        const double ringing = analysis::ringingDb (tail, kRate, from, to) - analysis::noiseBaseline (tail, from, to, std::max (t60, 0.1), kRate).ringingDb;
        const double flutter = analysis::shortFlutterDb (ir), over = flutter - analysis::idealShortFlutterDb (ir, std::max (t60, 0.1), kRate);
        const double limit = p.space < 0.25f ? 2.0 : 1.0;
        char row[200];
        std::snprintf (row, sizeof (row), "%-18s %5.2f s   ringing %+4.1f dB (max 1.5) %s   flutter %+4.1f dB (max %.0f) %s", label.c_str(), t60, ringing,
                       ringing < 1.5 ? "ok" : "TOO MUCH", over, limit, flutterIntended ? "intended" : (flutter >= 0.0 && over < limit ? "ok" : "TOO MUCH"));
        smoothRows.push_back (row);
    };
    std::printf ("%-18s %-6s %-13s %s\n", "Room", "sound", "Space text", "wet 40 dB under its loudest, after the last hit ends");
    for (const int percent : { 0, 50, 100 })
        for (const auto* sound : { &hats, &claps })
        {
            dsp::MacroPositions p;
            p.space = (float) percent / 100.0f;
            render ("space" + std::to_string (percent), p, *sound, dsp::describeSpace (p.space, room));
        }
    for (const int percent : { 0, 50 })
    {
        dsp::MacroPositions p;
        p.space = (float) percent / 100.0f;
        smooth ("Space " + std::to_string (percent) + " %", p, false);
    }
    const auto number = [] (const std::string& text, const char* key, double fallback) {
        std::smatch m;
        return std::regex_search (text, m, std::regex (std::string ("\"") + key + "\"\\s*:\\s*([-0-9.eE]+)")) ? std::atof (m[1].str().c_str()) : fallback;
    };
    for (const auto& entry : std::filesystem::directory_iterator ("presets/factory"))
    {
        std::ifstream in (entry.path());
        const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        if (text.find ("\"mode\": \"Room\"") == std::string::npos)
            continue;
        dsp::MacroPositions p;
        p.space = (float) number (text, "space", p.space);
        p.reflect = (float) number (text, "reflect", p.reflect);
        p.motion = (float) number (text, "motion", p.motion);
        p.tone = (float) number (text, "tone", p.tone);
        p.delay = (float) number (text, "echo", p.delay);
        for (int n = 0; n < dsp::kNumDelayNotes; ++n)
            if (text.find (std::string ("\"echoNote\": \"") + dsp::delayNoteName (n) + "\"") != std::string::npos)
                p.delayNote = n;
        for (const auto* sound : { &hats, &claps })
            render (entry.path().stem().string(), p, *sound, dsp::describeSpace (p.space, room));
        smooth (entry.path().stem().string(), p, entry.path().stem() == "Tiled Corridor");
    }
    std::printf ("\n");
    for (const auto& row : smoothRows)
        std::printf ("%s\n", row.c_str());
    std::printf ("\nWAV files in %s/ (mix %d %%)\n", outDir.c_str(), (int) (plugin::kDefaultMix * 100.0f + 0.5f));
}

void sweeps (const dsp::SpaceVoicing& voicing, const std::string& outDir)
{
    // Each knob from left to right, the others at Init; the sounds that show that knob best.
    const std::map<std::string, std::vector<std::string>> sounds { { "space", { "klik", "pad" } }, { "reflect", { "figur", "pad" } },
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
    bool writeFiles = true, table = false, calibration = false, sweep = false, echoDuckRenders = false, roomRendersOn = false;
    int calibrateModeIndex = -1, calibratePass = 1;
    bool modes = false;
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
        else if (std::strcmp (argv[i], "--calibrate-mode") == 0 && i + 2 < argc)
        {
            calibrateModeIndex = std::atoi (argv[++i]);
            calibratePass = std::atoi (argv[++i]);
        }
        else if (std::strcmp (argv[i], "--mode") == 0 && i + 1 < argc)
            macros.mode = std::clamp (std::atoi (argv[++i]), 0, dsp::kNumModes - 1);
        else if (std::strcmp (argv[i], "--modes") == 0)
            modes = true;
        else if (std::strcmp (argv[i], "--sweeps") == 0)
            sweep = true;
        else if (std::strcmp (argv[i], "--echo-duck") == 0)
            echoDuckRenders = true;
        else if (std::strcmp (argv[i], "--room") == 0)
            roomRendersOn = true;
        else if (std::strcmp (argv[i], "--set") == 0 && i + 1 < argc && applySetting (voicing, argv[i + 1]))
            ++i;
        else if (std::strcmp (argv[i], "--macro") == 0 && i + 1 < argc && applyMacro (macros, argv[i + 1]))
            ++i;
        else
        {
            std::fprintf (stderr, "usage: sf2_render [--out DIR] [--no-wav] [--mode N] [--macro name=value ...] [--set name=value ...]\n"
                                  "       sf2_render --table | --calibrate | --sweeps | --echo-duck | --room | --modes | --calibrate-mode N PASS\n");
            return 2;
        }
    }
    // As in the plug-in (juce::ScopedNoDenormals): tiny numbers are flushed to zero.
    _mm_setcsr (_mm_getcsr() | 0x8040u); // flush-to-zero and denormals-are-zero
    if (calibrateModeIndex >= 0)
        return calibrateMode (std::clamp (calibrateModeIndex, 0, dsp::kNumModes - 1), calibratePass), 0;
    if (modes)
        return modesReport (outDir + "/modes"), 0;
    if (table)
        return printTable(), 0;
    if (calibration)
        return calibrate (voicing), 0;
    if (sweep)
        return sweeps (voicing, outDir + "/makroer"), 0;
    if (echoDuckRenders)
        return echoDuck (voicing, outDir + "/echo-duck"), 0;
    if (roomRendersOn)
        return roomRenders (outDir + "/room"), 0;

    auto engine = makeEngine (voicing, macros);
    const float mix = plugin::kDefaultMix;
    std::printf ("Knapper: Space %s, Echo %s, Motion %s, Tone %s\n\n", dsp::describeSpace (macros.space, macros.mode).c_str(), dsp::describeReflect (macros.reflect).c_str(),
                 dsp::describeMotion (macros.motion).c_str(), dsp::describeTone (macros.tone).c_str());

    if (writeFiles)
    {
        std::filesystem::create_directories (outDir);
        std::printf ("Lydfiler i %s/ (48 kHz, 24 bit):\n", outDir.c_str());
        auto write = [&] (const std::string& name, const analysis::Stereo& s) {
            const auto path = outDir + "/" + name + ".wav";
            if (! analysis::writeWav (path, s, kRate))
                std::fprintf (stderr, "could not write %s\n", path.c_str());
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
        std::printf ("  pad_4_freeze  The pad is frozen at 3 s, held for 10 s and released again.\n");
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
        std::printf ("CPU: %.1f %% of one core at 48 kHz (20 s of audio in %.2f s)\n\n", seconds / 20.0 * 100.0, seconds);
    }

    const auto c = analysis::measureCharacter (*engine, kRate, mix);
    std::printf ("Tail length per octave (T60):\n ");
    for (size_t b = 0; b < analysis::kOctaveBands.size(); ++b)
        std::printf ("  %5.0f Hz", analysis::kOctaveBands[b]);
    std::printf ("\n ");
    for (const double t : c.t60Band)
        std::printf ("  %6.1f s", t);
    std::printf ("\n\nBrightness (spectral centroid): dry %.0f Hz, tail %.0f Hz\n", c.centroidDry, c.centroidWet);
    std::printf ("Ideal tail for comparison: ringing %.1f dB, unevenness %.2f dB\n", c.baseline.ringingDb, c.baseline.flutterDb);
    std::printf ("Clouds' change to the synth figure: %.2f dB (the room's own randomness: %.2f dB)\n\n", c.textureChangeDb, c.randomChangeDb);

    int misses = 0;
    const auto targets = analysis::targetsFor (c);
    std::printf ("%-55s %9s   %s\n", "Measurement", "Value", "Target");
    for (const auto& t : targets)
    {
        const bool ok = t.value >= t.lo && t.value <= t.hi;
        misses += ok ? 0 : 1;
        std::printf ("%-55s %7.2f %-4s %s %g..%g\n", t.what.c_str(), t.value, t.unit.c_str(), ok ? "ok " : "-- ", t.lo, t.hi);
    }
    std::printf ("\n%d of %zu targets hit.\n", (int) targets.size() - misses, targets.size());
    return 0;
}

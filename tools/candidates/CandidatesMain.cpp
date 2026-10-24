// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// sf2_candidates: preset candidates for one Mode, for Henrik to sort by ear (K/S in the
// standalone's sorting mode). Random knob positions (always the same ones), each measured, and
// only those that pass everything are kept:
//   - level: within ±1 dB, wet against dry on pink noise and the pad against the Mode at Init
//   - no hiss: silent input gives silence, and a figure 60 dB softer gives a tail 60 dB softer:
//     whatever does not follow the signal down (noise) stays 40 dB below the tail. (Not "30 dB
//     softer gives exactly 30 dB softer": Spectral's phase vocoder is a little level-dependent,
//     and that follows the signal, it is not hiss.)
//   - nothing broken: no clipping, nothing that is not a number
//   - not too alike: from the passing ones, the most different are picked one by one; each must
//     differ from every one already picked in sound (octave bands over time) and in at least two
//     knobs by 20 % or more
// One Mode per run (the engines share a random generator, so runs go side by side as processes):
//
//   sf2_candidates --mode N [--tries 400] [--keep 14] [--out presets/candidates]
//
// Writes <out>/<N>-<Mode>-<nn>.json (the preset format) and <out>/<N>-<Mode>.tsv (the
// measurements, for the report). Candidates of the Mode already in <out> stay as they are (Henrik
// may have sorted them): they count as picked, and new ones are numbered after them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <xmmintrin.h>
#include "analysis/Measure.h"
#include "analysis/Render.h"
#include "dsp/Modes.h"
#include "plugin/Defaults.h"

using namespace spaceforge;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kMinKnobStep = 0.2;   // "two knobs clearly different"
constexpr double kMinSoundDb = 1.5;    // octave bands over time, average difference

size_t at (double seconds) { return (size_t) (seconds * kRate); }

struct Candidate
{
    dsp::MacroPositions knobs;
    std::string existing; // file name (without .json) of a candidate already in the folder
    double pinkDb = 0, padDb = 0, peakDb = 0, silenceDb = 0, hissMarginDb = 0, t60 = 0, centroid = 0;
    analysis::BandFrames pad, figure;
    bool passes = false;
    std::string why; // the first measurement it failed
};

// Cloud Seed starts its modulation at random phases (std::rand) and a reset does not restart
// them: renders that must match sample for sample each get a fresh engine.
std::unique_ptr<dsp::SpaceEngine> makeEngine()
{
    std::srand (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->prepare (kRate);
    return engine;
}

analysis::Stereo renderAt (dsp::SpaceEngine& engine, const dsp::MacroPositions& knobs, const analysis::Stereo& input, float mix = 1.0f)
{
    engine.setMacros (knobs);
    engine.setTempo (analysis::kTestBpm, true, 0.0);
    analysis::RenderOptions options;
    options.mix = mix;
    return analysis::render (engine, input, kRate, options);
}

double peakDb (const analysis::Stereo& s)
{
    float peak = 0.0f;
    for (const auto* channel : { &s.l, &s.r })
        for (const float x : *channel)
            peak = std::isfinite (x) ? std::max (peak, std::abs (x)) : 1.0e9f;
    return 20.0 * std::log10 (std::max (peak, 1.0e-12f));
}

analysis::Stereo scaled (analysis::Stereo s, float db)
{
    const float gain = std::pow (10.0f, db / 20.0f);
    for (auto* channel : { &s.l, &s.r })
        for (auto& x : *channel)
            x *= gain;
    return s;
}

double knobDistance (const dsp::MacroPositions& a, const dsp::MacroPositions& b, int& clearlyDifferent)
{
    const double d[] = { a.space - b.space, a.echo - b.echo, a.motion - b.motion, a.tone - b.tone };
    clearlyDifferent = 0;
    double sum = 0;
    for (const double x : d)
    {
        clearlyDifferent += std::abs (x) >= kMinKnobStep ? 1 : 0;
        sum += x * x;
    }
    return std::sqrt (sum);
}

double soundDistance (const Candidate& a, const Candidate& b)
{
    return 0.5 * (analysis::bandFramesDifferenceDb (a.pad, b.pad) + analysis::bandFramesDifferenceDb (a.figure, b.figure));
}

std::string tidy (float v)
{
    char text[16];
    std::snprintf (text, sizeof text, "%.2f", (double) v);
    return text;
}

// A knob position from a file this tool wrote ("space": 0.42).
float readKnob (const std::string& json, const std::string& key)
{
    const auto at = json.find ("\"" + key + "\":");
    return at == std::string::npos ? 0.5f : std::clamp ((float) std::atof (json.c_str() + at + key.size() + 3), 0.0f, 1.0f);
}

std::string presetJson (const std::string& name, const dsp::MacroPositions& k)
{
    return "{\n    \"name\": \"" + name + "\",\n    \"mode\": \"" + dsp::modeSetup (k.mode).name + "\",\n    \"space\": " + tidy (k.space)
           + ",\n    \"echo\": " + tidy (k.echo) + ",\n    \"motion\": " + tidy (k.motion) + ",\n    \"tone\": " + tidy (k.tone)
           + ",\n    \"mix\": " + tidy (plugin::kDefaultMix) + "\n}\n";
}
} // namespace

int main (int argc, char** argv)
{
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals, as the plug-in does
    int mode = -1, tries = 400, keep = 14;
    std::filesystem::path out = "presets/candidates";
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (! std::strcmp (argv[i], "--mode"))
            mode = std::atoi (argv[i + 1]);
        else if (! std::strcmp (argv[i], "--tries"))
            tries = std::atoi (argv[i + 1]);
        else if (! std::strcmp (argv[i], "--keep"))
            keep = std::atoi (argv[i + 1]);
        else if (! std::strcmp (argv[i], "--out"))
            out = argv[i + 1];
    }
    if (mode < 0 || mode >= dsp::kNumModes || tries < 1 || keep < 1)
    {
        std::fprintf (stderr, "usage: sf2_candidates --mode 0..%d [--tries N] [--keep N] [--out DIR]\n", dsp::kNumModes - 1);
        return 1;
    }
    std::filesystem::create_directories (out);
    const std::string modeName = dsp::modeSetup (mode).name;

    auto engine = makeEngine();
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 3);
    const double pinkDry = analysis::rmsDb (pink, at (6.0), pink.size());
    const auto pad = analysis::makePad (kRate, 8.0).audio;
    const auto figure = analysis::makeSynthFigure (kRate, 8.0).audio;
    // A long tail: 30 s was too short for tails that hold themselves (Space near "∞" showed 25 s).
    const auto burst = analysis::makeNoiseBurst (kRate, 120.0).audio;
    const auto softLoud = scaled (figure, -30.0f), softQuiet = scaled (figure, -90.0f);
    const analysis::Stereo silence { std::vector<float> (at (4.0)), std::vector<float> (at (4.0)) };

    // The Mode at Init's knobs (Space 50 %): the pad's level the candidates are compared with.
    dsp::MacroPositions init;
    init.space = 0.5f;
    init.mode = mode;
    const double padInit = analysis::rmsDb (renderAt (*engine, init, pad), 0, pad.size());

    // The Mode's candidates already in the folder, measured first and kept whatever they measure.
    const std::string prefix = std::to_string (mode) + "-" + modeName + "-";
    std::vector<std::pair<std::string, dsp::MacroPositions>> existing;
    int lastNumber = 0;
    for (const auto& entry : std::filesystem::directory_iterator (out))
    {
        const auto stem = entry.path().stem().string();
        if (entry.path().extension() != ".json" || stem.rfind (prefix, 0) != 0)
            continue;
        std::ifstream file (entry.path());
        const std::string json ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());
        existing.push_back ({ stem, { readKnob (json, "space"), readKnob (json, "echo"), readKnob (json, "motion"), readKnob (json, "tone"), mode } });
        lastNumber = std::max (lastNumber, std::atoi (stem.c_str() + prefix.size()));
    }
    std::sort (existing.begin(), existing.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    std::mt19937 random (1000u + (unsigned) mode);
    std::uniform_int_distribution<int> percent (0, 100);
    std::vector<Candidate> all;
    int failed[4] = {};
    for (int t = -(int) existing.size(); t < tries; ++t)
    {
        Candidate c;
        engine = makeEngine();
        if (t < 0)
        {
            c.existing = existing[existing.size() + (size_t) t].first;
            c.knobs = existing[existing.size() + (size_t) t].second;
        }
        else
            c.knobs = { percent (random) / 100.0f, percent (random) / 100.0f, percent (random) / 100.0f, percent (random) / 100.0f, mode };
        const bool keepAnyway = ! c.existing.empty();
        c.pinkDb = analysis::rmsDb (renderAt (*engine, c.knobs, pink), at (6.0), pink.size()) - pinkDry;
        if (std::abs (c.pinkDb) > 1.0 && ! keepAnyway)
        {
            ++failed[0];
            continue;
        }
        const auto padOut = renderAt (*engine, c.knobs, pad);
        const auto figureOut = renderAt (*engine, c.knobs, figure);
        c.padDb = analysis::rmsDb (padOut, 0, padOut.size()) - padInit;
        if (std::abs (c.padDb) > 1.0 && ! keepAnyway)
        {
            ++failed[0];
            continue;
        }
        // Peaks as heard, at the default mix (the test sounds peak at -7 dBFS).
        c.peakDb = std::max (peakDb (renderAt (*engine, c.knobs, pad, plugin::kDefaultMix)), peakDb (renderAt (*engine, c.knobs, figure, plugin::kDefaultMix)));
        c.silenceDb = peakDb (renderAt (*engine, c.knobs, silence));
        const auto a = renderAt (*makeEngine(), c.knobs, softLoud), b = renderAt (*makeEngine(), c.knobs, softQuiet);
        analysis::Stereo rest { a.l, a.r };
        const float lift = std::pow (10.0f, 60.0f / 20.0f);
        for (size_t i = 0; i < rest.size(); ++i)
        {
            rest.l[i] -= b.l[i] * lift;
            rest.r[i] -= b.r[i] * lift;
        }
        c.hissMarginDb = analysis::rmsDb (a, at (4.0), a.size()) - analysis::rmsDb (rest, at (4.0), rest.size());
        if (std::getenv ("SF2_VERBOSE"))
            std::fprintf (stderr, "\n  %.2f %.2f %.2f %.2f pink %+.1f pad %+.1f peak %.1f silence %.0f hiss %.0f", c.knobs.space, c.knobs.echo, c.knobs.motion, c.knobs.tone, c.pinkDb, c.padDb, c.peakDb, c.silenceDb, c.hissMarginDb);
        if (! (c.peakDb < -0.5) && ! keepAnyway)
        {
            ++failed[1];
            continue;
        }
        if ((c.silenceDb > -120.0 || c.hissMarginDb < 40.0) && ! keepAnyway)
        {
            ++failed[2];
            continue;
        }
        c.pad = analysis::bandFrames (padOut, kRate, 0, padOut.size());
        c.figure = analysis::bandFrames (figureOut, kRate, 0, figureOut.size());
        c.centroid = analysis::spectralCentroid (padOut, kRate, at (4.0), at (8.0));
        c.passes = true;
        all.push_back (std::move (c));
        if (t >= 0)
            std::fprintf (stderr, "\r%s: %d/%d tried, %zu pass", modeName.c_str(), t + 1, tries, all.size());
    }
    std::fprintf (stderr, "\n");

    // The most different ones, one by one: those already in the folder first (else the one
    // nearest the middle of every knob), then always the one farthest (in sound) from all picked
    // so far, if it is far enough and two knobs differ.
    std::vector<size_t> picked;
    std::vector<double> nearest (all.size(), 1.0e9);
    std::vector<bool> blocked (all.size(), false);
    auto pick = [&] (size_t chosen) {
        picked.push_back (chosen);
        blocked[chosen] = true;
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (blocked[i])
                continue;
            int clearly = 0;
            knobDistance (all[i].knobs, all[chosen].knobs, clearly);
            nearest[i] = std::min (nearest[i], soundDistance (all[i], all[chosen]));
            if (clearly < 2 || nearest[i] < kMinSoundDb)
                blocked[i] = true;
        }
    };
    for (size_t i = 0; i < existing.size(); ++i)
        pick (i);
    if (picked.empty() && ! all.empty())
    {
        size_t first = 0;
        for (size_t i = 0; i < all.size(); ++i)
        {
            int n = 0;
            const dsp::MacroPositions middle { 0.5f, 0.5f, 0.5f, 0.5f, mode };
            if (knobDistance (all[i].knobs, middle, n) < knobDistance (all[first].knobs, middle, n))
                first = i;
        }
        pick (first);
    }
    while ((int) picked.size() < keep)
    {
        double best = -1.0;
        size_t bestIndex = 0;
        for (size_t i = 0; i < all.size(); ++i)
            if (! blocked[i] && nearest[i] > best)
                best = nearest[i], bestIndex = i;
        if (best < 0.0)
            break;
        pick (bestIndex);
    }

    // T60 only for the picked ones (a long render each).
    std::ofstream tsv (out / (std::to_string (mode) + "-" + modeName + ".tsv"));
    tsv << "file\tmode\tspace\techo\tmotion\ttone\tpinkDb\tpadDb\tpeakDb\tsilenceDb\thissMarginDb\tt60\tcentroidHz\tnearestDb\n";
    // The new ones numbered after those already there, by Space.
    std::sort (picked.begin() + (std::ptrdiff_t) existing.size(), picked.end(), [&all] (size_t a, size_t b) { return all[a].knobs.space < all[b].knobs.space; });
    for (size_t n = 0; n < picked.size(); ++n)
    {
        auto& c = all[picked[n]];
        c.t60 = analysis::reverbTimePerOctave (renderAt (*engine, c.knobs, burst), kRate)[3];
        double near = 1.0e9;
        for (const size_t other : picked)
            if (other != picked[n])
                near = std::min (near, soundDistance (c, all[other]));
        auto name = c.existing;
        if (name.empty())
        {
            char number[24];
            std::snprintf (number, sizeof number, "%02d", ++lastNumber);
            name = prefix + number;
            std::ofstream (out / (name + ".json")) << presetJson ("Kandidat " + modeName + " " + number, c.knobs);
        }
        tsv << name << '\t' << modeName << '\t' << tidy (c.knobs.space) << '\t' << tidy (c.knobs.echo) << '\t' << tidy (c.knobs.motion) << '\t'
            << tidy (c.knobs.tone) << '\t' << c.pinkDb << '\t' << c.padDb << '\t' << c.peakDb << '\t' << c.silenceDb << '\t' << c.hissMarginDb
            << '\t' << c.t60 << '\t' << c.centroid << '\t' << near << '\n';
    }
    std::printf ("%s: %zu already there, %d tried, failed level %d, clipping %d, hiss %d; %zu passed, %zu picked\n", modeName.c_str(), existing.size(), tries, failed[0], failed[1],
                 failed[2], all.size(), picked.size());
    return 0;
}

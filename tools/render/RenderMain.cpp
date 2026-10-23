// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// sf2_render: renders the test sounds through SpaceForge 2's engine to WAV files and prints the
// measurements against the targets. Offline, no plug-in host needed.
//
//   sf2_render [--out DIR] [--no-wav] [--set name=value ...]
//
// --set changes one voicing value for this run only (for trying things out), e.g.
//   sf2_render --set decaySeconds=18 --set lowpassHz=7000

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
    return { { "crossFeed", &v.crossFeed }, { "inputLowCutHz", &v.inputLowCutHz }, { "inputHighCutHz", &v.inputHighCutHz },
             { "preDelayMs", &v.preDelayMs }, { "earlyDelayMs", &v.earlyDelayMs }, { "earlyFeedback", &v.earlyFeedback },
             { "earlyModMs", &v.earlyModMs }, { "earlyModHz", &v.earlyModHz }, { "earlyLevelDb", &v.earlyLevelDb },
             { "lineSizeMs", &v.lineSizeMs }, { "decaySeconds", &v.decaySeconds }, { "lineModMs", &v.lineModMs },
             { "lineModHz", &v.lineModHz }, { "lateDiffuseDelayMs", &v.lateDiffuseDelayMs },
             { "lateDiffuseFeedback", &v.lateDiffuseFeedback }, { "lateDiffuseModMs", &v.lateDiffuseModMs },
             { "lateDiffuseModHz", &v.lateDiffuseModHz }, { "lateLevelDb", &v.lateLevelDb }, { "lowShelfHz", &v.lowShelfHz },
             { "lowShelfDb", &v.lowShelfDb }, { "highShelfHz", &v.highShelfHz }, { "highShelfDb", &v.highShelfDb },
             { "lowpassHz", &v.lowpassHz }, { "crossSeed", &v.crossSeed }, { "monoBassHz", &v.monoBassHz },
             { "wetGainDb", &v.wetGainDb } };
}

std::map<std::string, int*> voicingIntFields (dsp::SpaceVoicing& v)
{
    return { { "earlyStages", &v.earlyStages }, { "lineCount", &v.lineCount }, { "lateStages", &v.lateStages },
             { "seedTap", &v.seedTap }, { "seedDiffusion", &v.seedDiffusion }, { "seedDelay", &v.seedDelay },
             { "seedPostDiffusion", &v.seedPostDiffusion } };
}

bool applySetting (dsp::SpaceVoicing& v, const std::string& setting)
{
    const auto eq = setting.find ('=');
    if (eq == std::string::npos)
        return false;
    const auto name = setting.substr (0, eq);
    const double value = std::atof (setting.c_str() + eq + 1);
    if (auto f = voicingFields (v); f.count (name))
        return *f[name] = value, true;
    if (auto f = voicingIntFields (v); f.count (name))
        return *f[name] = (int) value, true;
    return false;
}
} // namespace

int main (int argc, char** argv)
{
    std::string outDir = "renders";
    bool writeFiles = true;
    dsp::SpaceVoicing voicing;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--out") == 0 && i + 1 < argc)
            outDir = argv[++i];
        else if (std::strcmp (argv[i], "--no-wav") == 0)
            writeFiles = false;
        else if (std::strcmp (argv[i], "--set") == 0 && i + 1 < argc && applySetting (voicing, argv[i + 1]))
            ++i;
        else
        {
            std::fprintf (stderr, "usage: sf2_render [--out DIR] [--no-wav] [--set name=value ...]\n");
            return 2;
        }
    }

    std::srand (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setVoicing (voicing);
    engine->prepare (kRate);
    const float mix = plugin::kDefaultMix;

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
            write (signal.name + "_2_vaad", analysis::render (*engine, signal.audio, kRate, wet));
            write (signal.name + "_3_mix", analysis::render (*engine, signal.audio, kRate, mixed));
            std::printf ("  %-6s  %s\n", signal.name.c_str(), signal.description.c_str());
        }
        // Freeze: the pad is frozen at 3 s, held for 10 s, then released.
        analysis::RenderOptions freeze;
        freeze.freezeOnSeconds = 3.0;
        freeze.freezeOffSeconds = 13.0;
        write ("pad_4_freeze", analysis::render (*engine, analysis::makePad (kRate, 30.0).audio, kRate, freeze));
        std::printf ("  pad_4_freeze  Pad'en fryses ved 3 s, holdes i 10 s og slippes igen.\n");
        std::printf ("  (_1_toer = uden effekt, _2_vaad = kun rummet, _3_mix = standard-mix %d %%)\n\n", (int) (mix * 100.0f + 0.5f));
    }

    const auto c = analysis::measureCharacter (*engine, kRate, mix);
    std::printf ("Halelængde pr. oktav (T60):\n ");
    for (size_t b = 0; b < analysis::kOctaveBands.size(); ++b)
        std::printf ("  %5.0f Hz", analysis::kOctaveBands[b]);
    std::printf ("\n ");
    for (const double t : c.t60Band)
        std::printf ("  %6.1f s", t);
    std::printf ("\n\nLysstyrke (spektralt tyngdepunkt): tør %.0f Hz, hale %.0f Hz\n", c.centroidDry, c.centroidWet);
    std::printf ("Ideel hale til sammenligning: ringen %.1f dB, ujævnhed %.2f dB\n\n", c.baseline.ringingDb, c.baseline.flutterDb);

    int misses = 0;
    std::printf ("%-55s %9s   %s\n", "Måling", "Værdi", "Mål");
    for (const auto& t : analysis::targetsFor (c))
    {
        const bool ok = t.value >= t.lo && t.value <= t.hi;
        misses += ok ? 0 : 1;
        std::printf ("%-55s %7.2f %-4s %s %g..%g\n", t.what.c_str(), t.value, t.unit.c_str(), ok ? "ok " : "-- ", t.lo, t.hi);
    }
    std::printf ("\n%d af %zu mål ramt.\n", (int) analysis::targetsFor (c).size() - misses, analysis::targetsFor (c).size());
    return 0;
}

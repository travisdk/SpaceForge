// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// sf2_regress: proves that a change leaves the sound of existing presets untouched.
//
//   sf2_regress dump DIR [preset folders...]    renders every preset (JSON) through every test sound
//                                               and writes the raw samples to DIR
//   sf2_regress compare DIR_A DIR_B             the difference between two dumps in dB against the
//                                               signal (-inf: bit for bit the same), one row per preset
//
// The engine gets the same random start (Cloudseed::ModPhaseRandom::Seed) every time, so two dumps of the same code
// are identical.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <xmmintrin.h>
#include "analysis/Render.h"
#include "dsp/Modes.h"
#include "plugin/Defaults.h"

using namespace spaceforge;
namespace fs = std::filesystem;

namespace
{
constexpr double kRate = 48000.0;

std::string slurp (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

double number (const std::string& text, const char* key, double fallback)
{
    const std::regex re (std::string ("\"") + key + "\"\\s*:\\s*(-?[0-9.eE+-]+)");
    std::smatch m;
    return std::regex_search (text, m, re) ? std::atof (m[1].str().c_str()) : fallback;
}

int dump (const fs::path& out, const std::vector<fs::path>& folders)
{
    fs::create_directories (out);
    const auto signals = analysis::allTestSignals (kRate, 6.0);
    int count = 0;
    for (const auto& folder : folders)
        for (const auto& entry : fs::directory_iterator (folder))
        {
            if (entry.path().extension() != ".json")
                continue;
            const auto text = slurp (entry.path());
            std::smatch m;
            int mode = 0;
            if (std::regex_search (text, m, std::regex ("\"mode\"\\s*:\\s*\"([^\"]*)\"")))
                for (int i = 0; i < dsp::kNumModes; ++i)
                    if (m[1].str() == dsp::modeSetup (i).name)
                        mode = i;
            dsp::MacroPositions p;
            p.mode = mode;
            p.space = (float) number (text, "space", p.space);
            p.reflect = (float) number (text, "reflect", p.reflect);
            p.motion = (float) number (text, "motion", p.motion);
            p.tone = (float) number (text, "tone", p.tone);
            const float mix = (float) number (text, "mix", plugin::kDefaultMix);

            Cloudseed::ModPhaseRandom::Seed (1);
            dsp::SpaceEngine engine;
            engine.setMacros (p);
            engine.prepare (kRate);
            for (const auto& signal : signals)
            {
                engine.setTempo (analysis::kTestBpm, true, 0.0);
                analysis::RenderOptions options;
                options.mix = mix;
                const auto audio = analysis::render (engine, signal.audio, kRate, options);
                const auto name = folder.filename().string() + "_" + entry.path().stem().string() + "_" + signal.name + ".f32";
                std::ofstream file (out / name, std::ios::binary);
                file.write ((const char*) audio.l.data(), (std::streamsize) (audio.l.size() * sizeof (float)));
                file.write ((const char*) audio.r.data(), (std::streamsize) (audio.r.size() * sizeof (float)));
            }
            ++count;
            std::printf ("\r%d presets", count);
            std::fflush (stdout);
        }
    std::printf ("\n");
    return 0;
}

std::vector<float> floats (const fs::path& p)
{
    const auto s = slurp (p);
    std::vector<float> v (s.size() / sizeof (float));
    std::memcpy (v.data(), s.data(), v.size() * sizeof (float));
    return v;
}

int compare (const fs::path& a, const fs::path& b)
{
    double worst = -std::numeric_limits<double>::infinity();
    int files = 0, identical = 0, missing = 0;
    std::string worstName;
    std::vector<fs::path> names;
    for (const auto& e : fs::directory_iterator (a))
        names.push_back (e.path().filename());
    std::sort (names.begin(), names.end());
    for (const auto& name : names)
    {
        if (! fs::exists (b / name))
        {
            ++missing;
            continue;
        }
        const auto x = floats (a / name), y = floats (b / name);
        if (x.size() != y.size())
        {
            ++missing;
            continue;
        }
        double diff = 0.0, ref = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            diff += (double) (x[i] - y[i]) * (x[i] - y[i]);
            ref += (double) x[i] * x[i];
        }
        const double db = diff <= 0.0 ? -std::numeric_limits<double>::infinity() : 10.0 * std::log10 (diff / std::max (ref, 1.0e-30));
        ++files;
        identical += diff <= 0.0 ? 1 : 0;
        if (db > worst)
        {
            worst = db;
            worstName = name.string();
        }
    }
    std::printf ("%d renders compared, %d bit for bit identical, %d missing\n", files, identical, missing);
    std::printf ("largest difference: %.1f dB against the signal (%s)\n", worst, worstName.c_str());
    std::printf ("limit: -100 dB  ->  %s\n", worst < -100.0 && missing == 0 ? "PASS" : "FAIL");
    return worst < -100.0 && missing == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    _mm_setcsr (_mm_getcsr() | 0x8040u);
    if (argc >= 3 && std::strcmp (argv[1], "dump") == 0)
    {
        std::vector<fs::path> folders;
        for (int i = 3; i < argc; ++i)
            folders.push_back (argv[i]);
        return dump (argv[2], folders);
    }
    if (argc == 4 && std::strcmp (argv[1], "compare") == 0)
        return compare (argv[2], argv[3]);
    std::fprintf (stderr, "usage: sf2_regress dump DIR FOLDER... | compare DIR_A DIR_B\n");
    return 2;
}

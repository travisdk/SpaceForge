// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The Modes (Henrik's rules): Space at 50 % is a medium room in every Mode and the text shows the
// measured seconds; every Mode is as loud as the others; no Mode adds hiss; every knob works in
// its direction with an audible minimum in every Mode; a Mode change is clean (no click, nothing
// of the old tail, no allocation), and so is a preset change within a Mode.

// GCC flags free() inside the replacement operator delete as "mismatched" with new-expressions
// once inlined; these are the replacement functions themselves, so the warning does not apply.
#if defined(__GNUC__) && ! defined(__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <pmmintrin.h>
#include <xmmintrin.h>
#include "TestUtils.h"
#include "analysis/Measure.h"
#include "analysis/Render.h"
#include "analysis/TestSignals.h"
#include "dsp/Modes.h"

using namespace spaceforge;

namespace
{
std::atomic<bool> countAllocations { false };
std::atomic<int> allocations { 0 };
} // namespace

void* operator new (std::size_t size)
{
    if (countAllocations.load())
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double kRate = 48000.0;
size_t at (double seconds) { return (size_t) (seconds * kRate); }

dsp::MacroPositions knobs (int mode, float space = 0.5f, float echo = 0.0f, float motion = 0.5f, float tone = 0.5f)
{
    return { space, echo, motion, tone, mode };
}

std::unique_ptr<dsp::SpaceEngine> makeEngine (const dsp::MacroPositions& macros)
{
    std::srand (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setMacros (macros);
    engine->prepare (kRate);
    engine->setTempo (analysis::kTestBpm, true, 0.0);
    return engine;
}

double burstT60 (dsp::SpaceEngine& engine, double tail)
{
    return analysis::reverbTimePerOctave (analysis::render (engine, analysis::makeNoiseBurst (kRate, tail).audio, kRate), kRate)[3];
}

void testMediumAtHalf()
{
    // Space at 50 % is a medium room (10..15 s) in every Mode, and its text is what is measured.
    std::printf ("Space at 50 %%: T60 measured / shown, per Mode:\n");
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        auto engine = makeEngine (knobs (mode));
        const double t60 = burstT60 (*engine, 40.0);
        const double shown = std::atof (dsp::describeSpace (0.5f, mode).c_str());
        std::printf ("  %-9s %5.1f s / %s\n", dsp::modeSetup (mode).name, t60, dsp::describeSpace (0.5f, mode).c_str());
        CHECK_RANGE (t60, 10.0, 15.0);
        CHECK_RANGE (t60 / shown, 0.85, 1.15);
    }
}

void testEqualLevels()
{
    // Every Mode as loud as the others (pink noise at Init's knobs, Space at 50 %): within 1 dB.
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 3);
    const auto from = at (6.0);
    std::printf ("Level against dry (pink noise), per Mode:");
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        auto engine = makeEngine (knobs (mode));
        const double level = analysis::rmsDb (analysis::render (*engine, pink, kRate), from, pink.size()) - analysis::rmsDb (pink, from, pink.size());
        std::printf (" %s %+.1f", dsp::modeSetup (mode).name, level);
        CHECK_RANGE (level, -1.0, 1.0);
    }
    std::printf (" dB\n");
}

void testNoHissInAnyMode()
{
    // The same figure 30 dB softer gives the same tail 30 dB softer in every Mode (Tone at its
    // brightest): what is left after lifting it back is far below the tail.
    std::printf ("Soft input, Tone at max, the engine's own noise below the tail, per Mode:");
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        analysis::Stereo outs[2];
        int k = 0;
        for (const float levelDb : { -42.0f, -72.0f })
        {
            auto figure = analysis::makeSynthFigure (kRate, 8.0).audio;
            const float gain = std::pow (10.0f, (levelDb + 12.0f) / 20.0f);
            for (auto* channel : { &figure.l, &figure.r })
                for (auto& x : *channel)
                    x *= gain;
            auto engine = makeEngine (knobs (mode, 0.5f, 0.1f, 0.2f, 1.0f));
            outs[k++] = analysis::render (*engine, figure, kRate);
        }
        analysis::Stereo rest { outs[0].l, outs[0].r };
        const float lift = std::pow (10.0f, 30.0f / 20.0f);
        for (size_t i = 0; i < rest.l.size(); ++i)
        {
            rest.l[i] -= outs[1].l[i] * lift;
            rest.r[i] -= outs[1].r[i] * lift;
        }
        const double margin = analysis::rmsDb (outs[0], at (4.0), rest.l.size()) - analysis::rmsDb (rest, at (4.0), rest.l.size());
        std::printf (" %s %.0f", dsp::modeSetup (mode).name, margin);
        CHECK (margin > 60.0);
    }
    std::printf (" dB\n");
}

void testKnobsInEveryMode()
{
    // Each knob from 0 to 100 % in every Mode, the others at Init: Space makes the tail clearly
    // longer, Tone clearly brighter, Echo brings repeats, Motion moves the room from side to side.
    const auto pad = analysis::makePad (kRate, 8.0).audio;
    const auto bleeps = analysis::makeBleeps (kRate, 3.0).audio;
    auto swing = [] (const analysis::Stereo& s) {
        double lo = 1e9, hi = -1e9;
        for (double from = 1.0; from + 0.1 <= 4.0; from += 0.1)
        {
            analysis::Stereo l { s.l, s.l }, r { s.r, s.r };
            const double b = analysis::rmsDb (l, at (from), at (from + 0.1)) - analysis::rmsDb (r, at (from), at (from + 0.1));
            lo = std::min (lo, b);
            hi = std::max (hi, b);
        }
        return hi - lo;
    };
    std::printf ("Knobs 0 -> 100 %% per Mode: Space T60, Tone centre (Hz), Echo 0.4 s after a bleep (dB), Motion swing (dB):\n");
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        auto e0 = makeEngine (knobs (mode, 0.0f)), e1 = makeEngine (knobs (mode, 1.0f));
        const double space0 = burstT60 (*e0, 10.0), space1 = burstT60 (*e1, 40.0);

        auto t0 = makeEngine (knobs (mode, 0.5f, 0.0f, 0.5f, 0.0f)), t1 = makeEngine (knobs (mode, 0.5f, 0.0f, 0.5f, 1.0f));
        const double tone0 = analysis::spectralCentroid (analysis::render (*t0, pad, kRate), kRate, at (3.0), at (6.0));
        const double tone1 = analysis::spectralCentroid (analysis::render (*t1, pad, kRate), kRate, at (3.0), at (6.0));

        // Echo: the peak where the first repeat of the first bleep lands (1/8 dotted at the test
        // tempo, on either side: ping-pong) against the room alone there
        auto x0 = makeEngine (knobs (mode, 0.2f, 0.0f)), x1 = makeEngine (knobs (mode, 0.2f, 1.0f));
        const double repeat = 0.75 * 60.0 / analysis::kTestBpm;
        auto repeatPeak = [&] (const analysis::Stereo& s) {
            return 20.0 * std::log10 (std::max (peakOf (s.l, at (repeat - 0.005), at (repeat + 0.04)), peakOf (s.r, at (repeat - 0.005), at (repeat + 0.04))) + 1.0e-9);
        };
        const double echo0 = repeatPeak (analysis::render (*x0, bleeps, kRate));
        const double echo1 = repeatPeak (analysis::render (*x1, bleeps, kRate));

        auto m0 = makeEngine (knobs (mode, 0.5f, 0.0f, 0.0f)), m1 = makeEngine (knobs (mode, 0.5f, 0.0f, 1.0f));
        const double motion0 = swing (analysis::render (*m0, pad, kRate)), motion1 = swing (analysis::render (*m1, pad, kRate));

        std::printf ("  %-9s Space %5.1f -> %6.1f s   Tone %5.0f -> %5.0f   Echo %+5.1f   Motion %4.1f -> %4.1f\n", dsp::modeSetup (mode).name, space0, space1,
                     tone0, tone1, echo1 - echo0, motion0, motion1);
        CHECK (space1 > 3.0 * space0);
        CHECK (tone1 > 1.3 * tone0); // the tail's centre a good third higher: clearly brighter
        CHECK (echo1 - echo0 > 6.0);
        CHECK (motion1 > motion0 + 6.0);
    }
}

void testCleanSwitch()
{
    // A Mode change in the middle of a pad: no click (the high frequencies around it stay below
    // the pad's own), nothing of the old Mode's tail afterwards (the input stops at the change),
    // and no allocation on the audio thread.
    const auto pad = analysis::makePad (kRate, 0.0).audio;
    analysis::Stereo input { std::vector<float> (at (3.0)), std::vector<float> (at (3.0)) };
    for (size_t i = 0; i < std::min (input.l.size(), at (1.5)); ++i)
    {
        input.l[i] = pad.l[i % pad.l.size()];
        input.r[i] = pad.r[i % pad.r.size()];
    }
    auto highPeak = [] (const analysis::Stereo& s, size_t from, size_t to) {
        float peak = 0.0f;
        for (size_t i = std::max (from, (size_t) 2); i < to; ++i)
            peak = std::max (peak, std::abs (s.l[i] - 2.0f * s.l[i - 1] + s.l[i - 2]));
        return peak;
    };
    std::printf ("Mode change at 1.5 s (input stops there): click / tail left 0.1 s later (dB against before):\n");
    // to == kNumModes: a preset in the same Mode (jumpToMacros), every knob somewhere else.
    for (int to = 1; to <= dsp::kNumModes; ++to)
    {
        const bool preset = to == dsp::kNumModes;
        auto engine = makeEngine (knobs (0, 0.9f));
        engine->setMix (1.0f);
        engine->reset();
        analysis::Stereo out { std::vector<float> (input.l.size()), std::vector<float> (input.l.size()) };
        allocations = 0;
        for (size_t pos = 0; pos + 256 <= input.l.size(); pos += 256)
        {
            countAllocations = pos >= at (1.4);
            if (pos >= at (1.5) && preset && pos < at (1.5) + 256)
                engine->jumpToMacros (knobs (0, 0.3f, 0.6f, 0.9f, 0.1f));
            else if (pos >= at (1.5) && ! preset)
                engine->setMacros (knobs (to, 0.9f));
            engine->process (&input.l[pos], &input.r[pos], &out.l[pos], &out.r[pos], 256);
        }
        countAllocations = false;
        const double before = analysis::rmsDb (out, at (1.3), at (1.5));
        const double after = analysis::rmsDb (out, at (1.6), at (1.7));
        const double click = 20.0 * std::log10 (highPeak (out, at (1.5), at (1.6)) / std::max (highPeak (out, at (1.2), at (1.5)), 1.0e-9f));
        std::printf ("  Sky -> %-9s click %+5.1f dB, tail %+6.1f dB, allocations %d\n", preset ? "preset" : dsp::modeSetup (to).name, click, after - before, allocations.load());
        CHECK (click < 0.0);
        CHECK (after - before < -40.0);
        CHECK (allocations.load() == 0);
    }
}
} // namespace

int main()
{
    _mm_setcsr (_mm_getcsr() | 0x8040u); // as in the plug-in: denormals flushed to zero
    testMediumAtHalf();
    testEqualLevels();
    testNoHissInAnyMode();
    testKnobsInEveryMode();
    testCleanSwitch();
    return finish ("modes");
}

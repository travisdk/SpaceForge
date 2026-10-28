// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The macro contract: every knob works in its direction with a clearly audible minimum effect,
// keeps the level within 3 dB (also on other material and in random combinations), shows what it
// really does, and never clicks when it jumps.

#include <cstdlib>
#include <functional>
#include <memory>
#include "TestUtils.h"
#include "analysis/Measure.h"
#include "analysis/Render.h"

using namespace spaceforge;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kPositions[] = { 0, 25, 50, 75, 100 };

std::unique_ptr<dsp::SpaceEngine> makeEngine (const dsp::MacroPositions& macros, bool grains = true)
{
    std::srand (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    dsp::SpaceVoicing voicing;
    voicing.grainsEnabled = grains;
    engine->setVoicing (voicing);
    engine->setMacros (macros);
    engine->prepare (kRate);
    engine->setTempo (analysis::kTestBpm, true, 0.0);
    return engine;
}

dsp::MacroPositions with (float dsp::MacroPositions::*knob, double value)
{
    dsp::MacroPositions p;
    p.*knob = (float) value;
    return p;
}

// Wet only, block by block; `during` may change the engine before each block.
analysis::Stereo renderWet (dsp::SpaceEngine& engine, const analysis::Stereo& input, const std::function<void (size_t)>& during = {})
{
    engine.setMix (1.0f);
    engine.setFreeze (false);
    engine.setTempo (analysis::kTestBpm, true, 0.0);
    engine.reset();
    analysis::Stereo out { std::vector<float> (input.size()), std::vector<float> (input.size()) };
    for (size_t pos = 0; pos < input.size(); pos += 256)
    {
        if (during)
            during (pos);
        const int n = (int) std::min ((size_t) 256, input.size() - pos);
        engine.process (&input.l[pos], &input.r[pos], &out.l[pos], &out.r[pos], n);
    }
    return out;
}

size_t at (double seconds) { return (size_t) (seconds * kRate); }

void testLevels()
{
    // Pink noise other than the calibration's, and the pad: within 3 dB of Init everywhere.
    std::printf ("Level against Init (dB), pink noise / pad:\n");
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 11);
    const auto pad = analysis::makePad (kRate, 2.0).audio;
    auto init = makeEngine ({});
    const double pinkInit = analysis::rmsDb (renderWet (*init, pink), at (6.0), pink.size());
    const double padInit = analysis::rmsDb (renderWet (*init, pad), at (1.0), at (4.0));
    const std::pair<const char*, float dsp::MacroPositions::*> knobs[] = {
        { "space", &dsp::MacroPositions::space }, { "reflect", &dsp::MacroPositions::reflect },
        { "motion", &dsp::MacroPositions::motion }, { "tone", &dsp::MacroPositions::tone } };
    for (const auto& [name, knob] : knobs)
    {
        std::printf ("  %-6s", name);
        for (const int percent : kPositions)
        {
            auto engine = makeEngine (with (knob, percent / 100.0));
            const double p = analysis::rmsDb (renderWet (*engine, pink), at (6.0), pink.size()) - pinkInit;
            const double q = analysis::rmsDb (renderWet (*engine, pad), at (1.0), at (4.0)) - padInit;
            std::printf ("  %3d%%: %+5.1f / %+5.1f", percent, p, q);
            CHECK_RANGE (p, -1.5, 1.5);
            CHECK_RANGE (q, -3.0, 3.0);
        }
        std::printf ("\n");
    }

    // Random combinations of all four knobs.
    std::srand (5);
    std::printf ("  random combinations:");
    for (int i = 0; i < 6; ++i)
    {
        const dsp::MacroPositions p { (float) std::rand() / (float) RAND_MAX, (float) std::rand() / (float) RAND_MAX,
                                      (float) std::rand() / (float) RAND_MAX, (float) std::rand() / (float) RAND_MAX };
        auto engine = makeEngine (p);
        const double level = analysis::rmsDb (renderWet (*engine, pink), at (6.0), pink.size()) - pinkInit;
        std::printf (" %+.1f", level);
        CHECK_RANGE (level, -3.0, 3.0);
    }
    std::printf (" dB\n");
}

void testSpace()
{
    // Longer to the right, by a factor of at least 10 over the range; the text shows the real
    // tail, sky included, of a noise burst (within 15 %; "∞" means it holds itself for more than
    // 80 s); narrower to the left.
    std::printf ("Space: T60 at 1 kHz (measured / shown), L/R correlation above 300 Hz:\n");
    double previous = 0.0, first = 0.0, corrLeft = 0.0, corrRight = 0.0;
    for (const int percent : kPositions)
    {
        const double x = percent / 100.0;
        auto engine = makeEngine (with (&dsp::MacroPositions::space, x));
        const auto text = dsp::describeSpace ((float) x);
        const bool endless = text == "∞";
        const double expected = endless ? 0.0 : std::atof (text.c_str());
        const auto ir = renderWet (*engine, analysis::makeNoiseBurst (kRate, endless ? 90.0 : std::min (90.0, 2.0 + 2.5 * expected)).audio);
        const double t60 = analysis::reverbTimePerOctave (ir, kRate)[3];
        const auto burst = renderWet (*engine, analysis::makeNoiseBurst (kRate, 3.0).audio);
        const double corr = analysis::correlationAbove (burst, kRate, 300.0, at (0.4), at (1.5));
        std::printf ("  %3d%%: %6.2f s / %-7s corr %+.2f\n", percent, t60, text.c_str(), corr);
        CHECK (t60 > previous);
        if (endless)
            CHECK (t60 > 80.0);
        else
            CHECK_RANGE (t60 / expected, 0.85, 1.15);
        if (percent == 0)
        {
            first = t60;
            corrLeft = corr;
        }
        corrRight = corr;
        previous = t60;
    }
    CHECK (previous / first >= 10.0);
    CHECK (corrLeft > corrRight + 0.1);
}

void testEcho()
{
    // A click at 125 BPM: the first echo (1/8 dotted = 0.36 s) appears, on the left, and more
    // repeats remain with more Echo. Without grains, so the echoes are not hidden by them.
    std::printf ("Echo: first echo (left, against just before it), level at the 5th echo:\n");
    const double t = dsp::kReflectBeats * 60.0 / analysis::kTestBpm;
    const auto click = analysis::makeClick (kRate, 5.0).audio;
    double previousFifth = -1000.0;
    for (const int percent : kPositions)
    {
        auto engine = makeEngine (with (&dsp::MacroPositions::reflect, percent / 100.0), false);
        const auto out = renderWet (*engine, click);
        analysis::Stereo left { out.l, out.l }, right { out.r, out.r };
        const double echo = analysis::rmsDb (left, at (t), at (t + 0.03)) - analysis::rmsDb (left, at (t - 0.05), at (t - 0.01));
        const double balance = analysis::rmsDb (left, at (t), at (t + 0.03)) - analysis::rmsDb (right, at (t), at (t + 0.03));
        const double fifth = analysis::rmsDb (out, at (5 * t), at (5 * t + 0.03));
        std::printf ("  %3d%% %-13s: first %+5.1f dB (L-R %+5.1f dB), 5th %6.1f dB\n", percent, dsp::describeReflect (percent / 100.0f).c_str(), echo, balance, fifth);
        if (percent == 0)
            CHECK (echo < 3.0); // no echo: nothing stands out
        else
        {
            CHECK (echo > 6.0);    // a clearly audible echo
            CHECK (balance > 3.0); // ping-pong: the first repeat on the left
            CHECK (fifth >= previousFifth - 0.5);
        }
        previousFifth = fifth;
    }

    // The echo follows the tempo: at 100 BPM it is at 0.45 s.
    auto engine = makeEngine (with (&dsp::MacroPositions::reflect, 0.5), false);
    engine->setMix (1.0f);
    engine->reset();
    engine->setTempo (100.0, true, 0.0);
    analysis::Stereo out { std::vector<float> (click.size()), std::vector<float> (click.size()) };
    for (size_t pos = 0; pos < click.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, click.size() - pos);
        engine->process (&click.l[pos], &click.r[pos], &out.l[pos], &out.r[pos], n);
    }
    analysis::Stereo left { out.l, out.l };
    const double slow = dsp::kReflectBeats * 60.0 / 100.0;
    const double echo100 = analysis::rmsDb (left, at (slow), at (slow + 0.03)) - analysis::rmsDb (left, at (slow - 0.05), at (slow - 0.01));
    std::printf ("  at 100 BPM: echo at %.2f s %+5.1f dB\n", slow, echo100);
    CHECK (echo100 > 6.0);
}

void testMotion()
{
    // The room moves more and more between left and right (auto-pan) over the whole knob, also
    // with the transport stopped; the modulation deepens (checked in the table). How much the
    // figure changes is reported for reference.
    std::printf ("Motion: change of the figure against Motion 0 (x random), L/R swing of the pad:\n");
    const auto figure = analysis::makeSynthFigure (kRate, 3.0).audio;
    const auto pad = analysis::makePad (kRate, 2.0).audio;
    auto still = makeEngine (with (&dsp::MacroPositions::motion, 0.0));
    const auto figureStill = renderWet (*still, figure);
    std::srand (99);
    auto other = std::make_unique<dsp::SpaceEngine>();
    other->setMacros (with (&dsp::MacroPositions::motion, 0.0));
    other->prepare (kRate);
    const double random = analysis::spectroTemporalDifferenceDb (renderWet (*other, figure), figureStill, kRate, 0, at (4.0));

    auto swingOf = [&] (const analysis::Stereo& s) {
        std::vector<double> balance;
        for (double from = 1.0; from + 0.1 <= 4.0; from += 0.1)
        {
            analysis::Stereo l { s.l, s.l }, r { s.r, s.r };
            balance.push_back (analysis::rmsDb (l, at (from), at (from + 0.1)) - analysis::rmsDb (r, at (from), at (from + 0.1)));
        }
        const auto [lo, hi] = std::minmax_element (balance.begin(), balance.end());
        return *hi - *lo;
    };

    double swingStill = 0.0, previous = 0.0;
    for (const int percent : kPositions)
    {
        auto engine = makeEngine (with (&dsp::MacroPositions::motion, percent / 100.0));
        const double change = analysis::spectroTemporalDifferenceDb (renderWet (*engine, figure), figureStill, kRate, 0, at (4.0)) / random;
        const double swing = swingOf (renderWet (*engine, pad));
        std::printf ("  %3d%% %-38s change %4.2f x, swing %4.1f dB\n", percent, dsp::describeMotion (percent / 100.0f).c_str(), change, swing);
        (void) change; // reported only: Motion has no grains since phase 4
        // The room itself drifts a few dB between left and right; the auto-pan must clearly
        // stand out from that already at Init (nothing was heard below 75 %).
        // (Up to 25 % the pan is slow, 4 bars, and the sky's own drift can hide it in this short
        // window; from there on every step must swing more.)
        if (percent == 0)
            swingStill = swing;
        else if (percent > 25)
            CHECK (swing > previous);
        if (percent == 50)
            CHECK (swing > swingStill + 6.0);
        if (percent == 100)
            CHECK (swing > swingStill + 10.0);
        previous = swing;
    }

    // Transport stopped: the pan runs on freely.
    auto engine = makeEngine (with (&dsp::MacroPositions::motion, 1.0));
    engine->setMix (1.0f);
    engine->reset();
    engine->setTempo (125.0, false, 0.0);
    analysis::Stereo out { std::vector<float> (pad.size()), std::vector<float> (pad.size()) };
    for (size_t pos = 0; pos < pad.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, pad.size() - pos);
        engine->setTempo (125.0, false, 0.0);
        engine->process (&pad.l[pos], &pad.r[pos], &out.l[pos], &out.r[pos], n);
    }
    std::printf ("  transport stopped, Motion 100 %%: swing %.1f dB\n", swingOf (out));
    CHECK (swingOf (out) > swingStill + 10.0);
}

void testTone()
{
    // Brighter to the right: the tail's centre of gravity rises clearly, the treble lasts longer.
    std::printf ("Tone: tail brightness, T60 at 4 kHz against 1 kHz:\n");
    double previous = 0.0, first = 0.0;
    for (const int percent : kPositions)
    {
        auto engine = makeEngine (with (&dsp::MacroPositions::tone, percent / 100.0), false);
        const auto tail = renderWet (*engine, analysis::makeNoiseBurst (kRate, 4.0).audio);
        const double centroid = analysis::spectralCentroid (tail, kRate, at (0.6), at (3.0));
        const auto ir = renderWet (*engine, analysis::makeImpulse (kRate, 25.0).audio);
        const auto t = analysis::reverbTimePerOctave (ir, kRate);
        std::printf ("  %3d%% %-17s: %5.0f Hz, %.2f\n", percent, dsp::describeTone (percent / 100.0f).c_str(), centroid, t[5] / t[3]);
        CHECK (centroid > previous);
        if (percent == 0)
            first = centroid;
        previous = centroid;
    }
    CHECK (previous / first >= 2.5);
}

void testNoClicks()
{
    // Each knob jumping from one end to the other in the middle of the pad: the high frequencies
    // around the jump stay within 6 dB of the same moment without a jump (at either end).
    std::printf ("Jumps 0 -> 100 %% at 2 s, high-frequency peak against no jump:\n");
    const auto pad = analysis::makePad (kRate, 2.0).audio;
    auto highPeak = [] (const analysis::Stereo& s) {
        // Second difference: a crude high-pass that makes clicks stand out.
        float peak = 0.0f;
        for (size_t i = at (1.95); i < at (2.25); ++i)
            peak = std::max (peak, std::abs (s.l[i] - 2.0f * s.l[i - 1] + s.l[i - 2]));
        return toDb (peak);
    };
    const std::pair<const char*, float dsp::MacroPositions::*> knobs[] = {
        { "space", &dsp::MacroPositions::space }, { "reflect", &dsp::MacroPositions::reflect },
        { "motion", &dsp::MacroPositions::motion }, { "tone", &dsp::MacroPositions::tone } };
    for (const auto& [name, knob] : knobs)
    {
        auto steadyLeft = makeEngine (with (knob, 0.0)), steadyRight = makeEngine (with (knob, 1.0));
        const double reference = std::max (highPeak (renderWet (*steadyLeft, pad)), highPeak (renderWet (*steadyRight, pad)));
        auto jumping = makeEngine (with (knob, 0.0));
        const auto jumped = renderWet (*jumping, pad, [&, k = knob] (size_t pos) {
            if (pos >= at (2.0))
                jumping->setMacros (with (k, 1.0));
        });
        const double peak = highPeak (jumped);
        std::printf ("  %-6s %+5.1f dB\n", name, peak - reference);
        CHECK (peak - reference < 6.0);
    }
}

void testNoHiddenEcho()
{
    // With Echo at 0 nothing may come back as a copy of a short bleep (an echo or a double hit):
    // between 20 and 450 ms after each bleep the output may not resemble the bleep. Every Motion
    // position, no exceptions. Both failures were heard on bleeps (phase 4). The same
    // measure with Echo on proves it would catch one.
    // Measured with short noise hits shaped like the bleeps (gone after ~40 ms): a tone would
    // resemble the room's own ringing, noise only resembles a real copy of itself.
    std::printf ("No copies of a short hit with Echo at 0 (resemblance 20-450 ms later, 1 = exact copy):\n");
    auto bleeps = analysis::makeBleeps (kRate, 1.0).audio;
    uint32_t seed = 77;
    for (int n = 0; n < 4; ++n)
        for (size_t i = 0; i < at (0.08); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const double time = (double) i / kRate;
            bleeps.l[at (0.5 * n) + i] = bleeps.r[at (0.5 * n) + i] = (float) (0.5 * ((double) seed / 4294967295.0 * 2.0 - 1.0) * std::exp (-time / 0.008));
        }
    auto worstCopy = [&] (dsp::SpaceEngine& engine) {
        const auto out = renderWet (engine, bleeps);
        double worst = 0.0;
        for (int n = 0; n < 4; ++n)
        {
            const std::vector<float> hit (bleeps.l.begin() + (long) at (0.5 * n), bleeps.l.begin() + (long) (at (0.5 * n) + at (0.08)));
            worst = std::max (worst, analysis::copyStrength (out, hit, at (0.5 * n), kRate, 0.02, 0.45));
        }
        return worst;
    };
    for (const int percent : { 0, 10, 25, 40, 50, 60, 75, 90, 100 })
    {
        auto engine = makeEngine (with (&dsp::MacroPositions::motion, percent / 100.0));
        const double copy = worstCopy (*engine);
        std::printf ("  Motion %3d%%: %.2f\n", percent, copy);
        CHECK (copy < 0.25);
    }
    auto echo = makeEngine (with (&dsp::MacroPositions::reflect, 0.3));
    const double withEcho = worstCopy (*echo);
    std::printf ("  (Echo 30 %%: %.2f, must be caught)\n", withEcho);
    CHECK (withEcho > 0.4);
}

void testTexts()
{
    CHECK (dsp::describeReflect (0.0f) == "no echo");
    CHECK (dsp::describeMotion (0.0f) == "still");
    CHECK (dsp::describeMotion (1.0f).find ("pan ½ bar") != std::string::npos);
    CHECK (dsp::describeMotion (0.5f).find ("pan 2 bars") != std::string::npos);
    CHECK (dsp::describeTone (0.0f) == "dark ●○○○○ bright");
    CHECK (dsp::describeTone (1.0f) == "dark ●●●●● bright");
}
} // namespace

int main()
{
    testTexts();
    testNoHiddenEcho();
    testNoClicks();
    testLevels();
    testSpace();
    testEcho();
    testMotion();
    testTone();
    return finish ("macros");
}

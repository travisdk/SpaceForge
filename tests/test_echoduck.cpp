// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The two small controls. Echo: sharp repeats on the beat (left at the note, right at 4/3 of it),
// more of them to the right, the note fixed by the knob, the level kept within 3 dB. Duck: the
// wet signal gives way by the shown dB while something plays and is back in the pauses. Both at
// 0 are off: no repeat, no change (the presets' bit-for-bit proof is sf2_regress).

#include <cstdlib>
#include <memory>
#include "TestUtils.h"
#include "analysis/Measure.h"
#include "analysis/Render.h"
#include "dsp/Modes.h"

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

dsp::MacroPositions knobs (int mode, float delay, float duck, int note = dsp::MacroPositions::kInitDelayNote)
{
    dsp::MacroPositions p;
    p.mode = mode;
    p.space = 0.5f;
    p.delay = delay;
    p.duck = duck;
    p.delayNote = note;
    return p;
}

analysis::Stereo renderWet (dsp::SpaceEngine& engine, const analysis::Stereo& input)
{
    engine.setMix (1.0f);
    engine.setFreeze (false);
    engine.setTempo (analysis::kTestBpm, true, 0.0);
    engine.reset();
    analysis::Stereo out { std::vector<float> (input.size()), std::vector<float> (input.size()) };
    for (size_t pos = 0; pos < input.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, input.size() - pos);
        engine.process (&input.l[pos], &input.r[pos], &out.l[pos], &out.r[pos], n);
    }
    return out;
}

size_t at (double seconds) { return (size_t) (seconds * kRate); }
double beatSeconds() { return 60.0 / analysis::kTestBpm; }

// The peak of one side in a window (a repeat is a sharp event, so its peak stands out).
double peakDb (const std::vector<float>& side, double from, double to)
{
    return toDb (peakOf (side, at (from), at (to)));
}

void testEcho()
{
    // A click: the left repeat falls at the note (1/8d = 0.36 s at 125 BPM), the right one at 4/3
    // of it (0.48 s = 1/4); each stands out of what is just before it, and the repeats of the
    // left side are counted until they are 24 dB under the first one: that is the number the
    // knob shows.
    std::printf ("Echo (1/8d): first repeat left and right against just before it, repeats counted, shown text:\n");
    const double t = dsp::delayNoteBeats (2) * beatSeconds();
    const auto click = analysis::makeClick (kRate, 8.0).audio;
    int previousCount = 0;
    for (const int percent : kPositions)
    {
        auto engine = makeEngine (knobs (0, percent / 100.0f, 0.0f), false);
        const auto out = renderWet (*engine, click);
        const double first = peakDb (out.l, t - 0.004, t + 0.012);
        const double beforeL = peakDb (out.l, t - 0.06, t - 0.02);
        const double firstR = peakDb (out.r, t * 4.0 / 3.0 - 0.004, t * 4.0 / 3.0 + 0.012);
        const double beforeR = peakDb (out.r, t * 4.0 / 3.0 - 0.06, t * 4.0 / 3.0 - 0.02);
        int count = 0;
        for (int n = 1; n < 40; ++n)
            if (peakDb (out.l, n * t - 0.004, n * t + 0.012) > first - 24.0)
                ++count;
            else
                break;
        const auto text = dsp::describeDelay (percent / 100.0f, 2);
        std::printf ("  %3d%% %-10s: L %+5.1f dB, R %+5.1f dB over before, %2d repeats\n", percent, text.c_str(), first - beforeL, firstR - beforeR, count);
        if (percent == 0)
            CHECK (first - beforeL < 6.0); // nothing comes back on the beat
        else
        {
            CHECK (first - beforeL > 10.0);  // sharp: clearly audible
            CHECK (firstR - beforeR > 10.0);
            CHECK (count >= previousCount); // more to the right
            // the shown count is what can be heard (within one)
            CHECK (std::abs (count - dsp::delayRepeats (dsp::mapMacros (knobs (0, percent / 100.0f, 0.0f)).delayFeedback)) <= 1);
        }
        if (percent > 0)
            previousCount = count;
    }
    CHECK (previousCount >= 6); // 100 %: many repeats

    // The note is fixed by its own choice, never by the knob: 1/4 puts the left repeat at 0.48 s,
    // and the knob's position does not move it.
    for (const float knob : { 0.3f, 0.9f })
    {
        auto engine = makeEngine (knobs (0, knob, 0.0f, 3), false);
        const auto out = renderWet (*engine, click);
        const double tq = dsp::delayNoteBeats (3) * beatSeconds();
        CHECK (peakDb (out.l, tq - 0.004, tq + 0.012) - peakDb (out.l, tq - 0.06, tq - 0.02) > 10.0);
        CHECK (peakDb (out.l, t - 0.004, t + 0.012) - peakDb (out.l, t - 0.06, t - 0.02) < 6.0);
    }
    CHECK (dsp::describeDelay (0.5f, 2).rfind ("1/8d · ", 0) == 0);
    CHECK (dsp::describeDelay (0.0f, 2) == "off · 1/8d");

    // It follows the tempo: at 100 BPM the left repeat is at 0.45 s.
    auto engine = makeEngine (knobs (0, 0.5f, 0.0f), false);
    engine->setMix (1.0f);
    engine->reset();
    engine->setTempo (100.0, true, 0.0);
    analysis::Stereo out { std::vector<float> (click.size()), std::vector<float> (click.size()) };
    for (size_t pos = 0; pos < click.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, click.size() - pos);
        engine->process (&click.l[pos], &click.r[pos], &out.l[pos], &out.r[pos], n);
    }
    const double slow = dsp::delayNoteBeats (2) * 0.6;
    CHECK (peakDb (out.l, slow - 0.004, slow + 0.012) - peakDb (out.l, slow - 0.06, slow - 0.02) > 10.0);
}

void testEchoLevel()
{
    // Echo must not make the whole wet signal louder or softer by more than 3 dB (pink noise,
    // after 6 s, against Echo at 0), in every Mode.
    std::printf ("Echo level against 0 (dB), pink noise, per Mode:\n");
    const auto pink = analysis::makePinkNoise (kRate, 12.0, 0.125f, 11);
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        auto zero = makeEngine (knobs (mode, 0.0f, 0.0f));
        const double base = analysis::rmsDb (renderWet (*zero, pink), at (6.0), pink.size());
        std::printf ("  %-10s", dsp::modeSetup (mode).name);
        for (const int percent : kPositions)
        {
            auto engine = makeEngine (knobs (mode, percent / 100.0f, 0.0f));
            const double level = analysis::rmsDb (renderWet (*engine, pink), at (6.0), pink.size()) - base;
            std::printf (" %+5.1f", level);
            CHECK_RANGE (level, -3.0, 3.0);
        }
        std::printf ("\n");
    }
}

void testEchoAudible()
{
    // A single bleep through the whole plugin (Mix at its default 40 %): how far the first left
    // repeat stands up from the room's own tail in the same window without Echo, and how loud it
    // is against the bleep itself in the output. This is what is heard in a track.
    std::printf("Echo audibility (final output, Mix 40%%, 1/8d, one bleep): first repeat over the room alone / against the bleep (dB):\n");
    auto bleep = analysis::makeBleeps (kRate, 6.0).audio;
    for (size_t i = at (0.3); i < bleep.size(); ++i)
        bleep.l[i] = bleep.r[i] = 0.0f;
    const double t = dsp::delayNoteBeats (2) * beatSeconds();
    for (int mode = 0; mode < dsp::kNumModes; ++mode)
    {
        auto render = [&] (float delay) {
            auto engine = makeEngine (knobs (mode, delay, 0.0f));
            engine->setMix (0.4f);
            engine->setTempo (analysis::kTestBpm, true, 0.0);
            engine->reset();
            analysis::Stereo out { std::vector<float> (bleep.size()), std::vector<float> (bleep.size()) };
            for (size_t pos = 0; pos < bleep.size(); pos += 256)
            {
                const int n = (int) std::min ((size_t) 256, bleep.size() - pos);
                engine->process (&bleep.l[pos], &bleep.r[pos], &out.l[pos], &out.r[pos], n);
            }
            return out;
        };
        const auto off = render (0.0f);
        std::printf ("  %-10s", dsp::modeSetup (mode).name);
        for (const int percent : { 25, 50, 100 })
        {
            const auto on = render (percent / 100.0f);
            const double hit = peakDb (on.l, 0.0, 0.05);
            const double rep = peakDb (on.l, t, t + 0.05), room = peakDb (off.l, t, t + 0.05);
            std::printf ("  %3d%%: %+5.1f / %+5.1f", percent, rep - room, rep - hit);
            CHECK (rep - room > 9.0); // stands clearly up out of the room
            CHECK (rep - hit > -9.0); // and is no faint ghost of the bleep
        }
        std::printf ("\n");
    }
}

void testDuck()
{
    // Pink noise for 4 s, then silence: while it plays the wet signal is pulled down by the shown
    // dB (within 2 dB); the tail in the pause comes back (10 s later it is the same as without Duck).
    std::printf ("Duck: wet level while playing, and in the pause, against Duck 0 (dB):\n");
    analysis::Stereo input = analysis::makePinkNoise (kRate, 4.0, 0.125f, 11);
    input.l.resize (at (16.0), 0.0f);
    input.r.resize (at (16.0), 0.0f);
    for (const int mode : { 0, 2, 4 })
    {
        auto zero = makeEngine (knobs (mode, 0.0f, 0.0f));
        const auto base = renderWet (*zero, input);
        const double playing0 = analysis::rmsDb (base, at (2.0), at (4.0));
        const double pause0 = analysis::rmsDb (base, at (12.0), at (15.0));
        double previous = 1.0;
        std::printf ("  %-10s", dsp::modeSetup (mode).name);
        for (const int percent : kPositions)
        {
            auto engine = makeEngine (knobs (mode, 0.0f, percent / 100.0f));
            const auto out = renderWet (*engine, input);
            const double playing = analysis::rmsDb (out, at (2.0), at (4.0)) - playing0;
            const double pause = analysis::rmsDb (out, at (12.0), at (15.0)) - pause0;
            std::printf ("  %3d%% %-6s %+5.1f / %+5.1f", percent, dsp::describeDuck (percent / 100.0f).c_str(), playing, pause);
            CHECK (playing <= previous + 0.1); // more to the right: lower
            CHECK_RANGE (playing, -12.0 * percent / 100.0 - 2.0, -12.0 * percent / 100.0 + 2.0);
            CHECK_RANGE (pause, -1.0, 1.0);    // and back in the pause
            previous = playing;
        }
        std::printf ("\n");
    }
    CHECK (dsp::describeDuck (0.0f) == "off");
    CHECK (dsp::describeDuck (0.5f) == "6 dB");
    CHECK (dsp::describeDuck (1.0f) == "12 dB");
}

void testOffIsOff()
{
    // At 0 Echo and Duck change nothing, even when they have been moved and moved back.
    const auto pad = analysis::makePad (kRate, 4.0).audio;
    auto plain = makeEngine (knobs (0, 0.0f, 0.0f));
    const auto a = renderWet (*plain, pad);
    auto moved = makeEngine (knobs (0, 0.0f, 0.0f, 4));
    const auto b = renderWet (*moved, pad);
    CHECK (a.l == b.l && a.r == b.r); // the note does not matter while Echo is off
}
} // namespace

int main()
{
    testEcho();
    testEchoLevel();
    testEchoAudible();
    testDuck();
    testOffIsOff();
    return finish ("echoduck");
}

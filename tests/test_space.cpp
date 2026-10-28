// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The room engine (Cloud Seed with Mix and Freeze). The character targets must
// all be met, the sound must not depend on the host's sample rate or block size, and processing
// must never allocate, produce non-finite values or let the dry signal change.

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
#include "TestUtils.h"
#include "analysis/Character.h"
#include "analysis/Render.h"
#include "analysis/TestSignals.h"
#include "plugin/Defaults.h"

using namespace spaceforge;

// Counts allocations while armed, to prove the audio path never allocates.
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
std::unique_ptr<dsp::SpaceEngine> makeEngine (double rate)
{
    Cloudseed::ModPhaseRandom::Seed (1); // Cloud Seed seeds its modulation phases from Cloudseed::ModPhaseRandom
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->prepare (rate);
    return engine;
}

// The same with Space at 50 % (a medium room in every Mode): Init's room holds itself, so a
// tail that must end or be measured steadily needs a finite one.
std::unique_ptr<dsp::SpaceEngine> makeMediumEngine (double rate)
{
    dsp::MacroPositions medium;
    medium.space = 0.5f;
    Cloudseed::ModPhaseRandom::Seed (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setMacros (medium);
    engine->prepare (rate);
    return engine;
}

double maxDifference (const analysis::Stereo& a, const analysis::Stereo& b)
{
    double d = 0.0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        d = std::max ({ d, (double) std::abs (a.l[i] - b.l[i]), (double) std::abs (a.r[i] - b.r[i]) });
    return d;
}

void testCharacterTargets()
{
    // The room's own character (measured without the sky) where its decay setting is the one
    // tuned in phase 2 (9.5 s): Space 80 % in Sky's scale since the Modes (Init now holds itself).
    std::printf ("Character at 48 kHz, Space 80 %% (the room as in phase 2), against the targets:\n");
    dsp::MacroPositions room;
    room.space = 0.8f;
    Cloudseed::ModPhaseRandom::Seed (1);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setMacros (room);
    engine->prepare (48000.0);
    const auto c = analysis::measureCharacter (*engine, 48000.0, plugin::kDefaultMix);
    for (const auto& t : analysis::targetsFor (c))
    {
        std::printf ("  %-55s %8.2f %s\n", t.what.c_str(), t.value, t.unit.c_str());
        CHECK_RANGE (t.value, t.lo, t.hi);
    }
}

void testSampleRates()
{
    // Measured with a noise burst, not a click: a one-sample click carries less energy the higher
    // the rate (3 dB at 96 kHz), and the sky's tail depends on how loud the sound was.
    std::printf ("The same room at every sample rate (T60 at 1 kHz of a noise burst, wet level of pink noise):\n");
    double t48 = 0.0, level48 = 0.0;
    for (const double rate : { 48000.0, 44100.0, 96000.0 })
    {
        auto engine = makeMediumEngine (rate);
        const auto ir = analysis::render (*engine, analysis::makeNoiseBurst (rate, 30.0).audio, rate);
        const double t1k = analysis::reverbTimePerOctave (ir, rate)[3];
        const auto pink = analysis::makePinkNoise (rate, 10.0, 0.125f, 3);
        const double level = analysis::rmsDb (analysis::render (*engine, pink, rate), (size_t) (5.0 * rate), pink.size())
                             - analysis::rmsDb (pink, (size_t) (5.0 * rate), pink.size());
        std::printf ("  %6.0f Hz: T60 %.2f s, wet %+.2f dB against dry\n", rate, t1k, level);
        if (t48 <= 0.0)
        {
            t48 = t1k;
            level48 = level;
        }
        // (±15 %: the sky makes the tail some 70 s long, where a tenth more or less is not heard)
        CHECK_RANGE (t1k / t48, 0.85, 1.15);
        CHECK_RANGE (level - level48, -1.0, 1.0);
    }
}

void testBlockSizes()
{
    // The fixed internal chunk makes the output identical whatever block size the host uses.
    const double rate = 48000.0;
    const auto burst = analysis::makeNoiseBurst (rate, 2.0).audio;
    analysis::Stereo first;
    for (const int block : { 512, 1, 64, 333, 4096 })
    {
        auto engine = makeEngine (rate);
        analysis::RenderOptions options;
        options.blockSize = block;
        options.mix = 0.5f;
        const auto out = analysis::render (*engine, burst, rate, options);
        if (first.size() == 0)
            first = out;
        CHECK (maxDifference (out, first) < 1.0e-6);
    }
}

void testMix()
{
    const double rate = 48000.0;
    auto engine = makeEngine (rate);
    const auto burst = analysis::makeNoiseBurst (rate, 1.0).audio;

    // Mix 0: the dry signal exactly, not delayed.
    analysis::RenderOptions dry;
    dry.mix = 0.0f;
    CHECK (maxDifference (analysis::render (*engine, burst, rate, dry), burst) <= 0.0);

    // Mix 1: no dry signal at all (the wet signal starts one chunk late, so the first samples are silent).
    const auto impulse = analysis::makeImpulse (rate, 1.0).audio;
    const auto wet = analysis::render (*engine, impulse, rate);
    CHECK (peakOf (wet.l, 0, (size_t) engine->wetLatency()) <= 0.0f);
    CHECK (peakOf (wet.l) > 0.0f);
}

void testFreezeReleases()
{
    // After Freeze is released the room decays again: at least 10 dB down 15 s later (the sky
    // keeps the tail alive far longer than the room alone, which fell 20 dB).
    const double rate = 48000.0;
    auto engine = makeMediumEngine (rate);
    analysis::RenderOptions options;
    options.freezeOnSeconds = 3.0;
    options.freezeOffSeconds = 8.0;
    const auto out = analysis::render (*engine, analysis::makePad (rate, 20.0).audio, rate, options);
    const double held = analysis::rmsDb (out, (size_t) (7.0 * rate), (size_t) (8.0 * rate));
    const double later = analysis::rmsDb (out, (size_t) (23.0 * rate), (size_t) (24.0 * rate));
    std::printf ("Freeze released at 8 s: %.1f dB before, %.1f dB 15 s later\n", held, later);
    CHECK (later < held - 10.0);
}

void testNoHissWhenSoft()
{
    // The engine adds no noise of its own: the same sounds 30 dB softer give the same tail,
    // 30 dB softer, so what is left after lifting it back is far below the tail. heard
    // white noise playing softly: Clouds rounded to 16 bits and Cloud Seed zeroed every sample
    // below -90 dBFS; that left 6-28 dB of hiss in the treble (fixed at the source, THIRD_PARTY.md).
    // With Tone at its brightest, where hiss would show most.
    const double rate = 48000.0;
    analysis::Stereo outs[2];
    int k = 0;
    for (const float levelDb : { -42.0f, -72.0f })
    {
        auto figure = analysis::makeSynthFigure (rate, 8.0).audio;
        const float gain = std::pow (10.0f, (levelDb + 12.0f) / 20.0f);
        for (auto* channel : { &figure.l, &figure.r })
            for (auto& x : *channel)
                x *= gain;
        auto engine = makeEngine (rate);
        engine->setMacros ({ dsp::MacroPositions::kInitSpace, 0.1f, 0.2f, 1.0f });
        engine->reset();
        outs[k++] = analysis::render (*engine, figure, rate);
    }
    const float lift = std::pow (10.0f, 30.0f / 20.0f);
    analysis::Stereo rest { outs[0].l, outs[0].r };
    for (size_t i = 0; i < rest.l.size(); ++i)
    {
        rest.l[i] -= outs[1].l[i] * lift;
        rest.r[i] -= outs[1].r[i] * lift;
    }
    const auto from = (size_t) (4.0 * rate), to = rest.l.size();
    const double tail = analysis::rmsDb (outs[0], from, to), noise = analysis::rmsDb (rest, from, to);
    std::printf ("Soft input (-42 dB), Tone at max: tail %.1f dBFS, the engine's own noise %.1f dBFS\n", tail, noise);
    CHECK (noise < tail - 60.0);
}

void testHostileInput()
{
    const double rate = 48000.0;
    auto engine = makeEngine (rate);
    analysis::Stereo nasty { std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f) };
    for (size_t i = 0; i < nasty.size(); i += 7)
    {
        nasty.l[i] = std::numeric_limits<float>::quiet_NaN();
        nasty.r[i] = (i % 2 == 0) ? std::numeric_limits<float>::infinity() : 1.0e6f;
    }
    analysis::RenderOptions options;
    options.mix = 1.0f;
    const auto out = analysis::render (*engine, nasty, rate, options);
    CHECK (allFinite (out.l) && allFinite (out.r));
}

void testNoAllocations()
{
    const double rate = 48000.0;
    auto engine = makeEngine (rate);
    const auto pad = analysis::makePad (rate, 1.0).audio;
    analysis::Stereo out { std::vector<float> (pad.size()), std::vector<float> (pad.size()) };

    allocations = 0;
    countAllocations = true;
    for (size_t pos = 0; pos + 256 <= pad.size(); pos += 256)
    {
        engine->setMix ((float) (pos % 3000) / 3000.0f);
        engine->setFreeze ((pos / 48000) % 2 == 1);
        const auto x = (float) (pos % 7000) / 7000.0f;
        engine->setMacros ({ x, 1.0f - x, x, 0.5f * x });
        engine->setTempo (90.0 + 60.0 * x, pos % 2 == 0, (double) pos / 24000.0);
        engine->process (&pad.l[pos], &pad.r[pos], &out.l[pos], &out.r[pos], 256);
    }
    countAllocations = false;
    std::printf ("Allocations while processing (with every knob, Freeze and the tempo moving): %d\n", allocations.load());
    CHECK (allocations.load() == 0);
}
} // namespace

int main()
{
    testNoAllocations();
    testMix();
    testBlockSizes();
    testNoHissWhenSoft();
    testHostileInput();
    testFreezeReleases();
    testSampleRates();
    testCharacterTargets();
    return finish ("space");
}

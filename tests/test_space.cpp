// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The room engine (Cloud Seed with Mix and Freeze). The character targets from CLAUDE.md must
// all be met, the sound must not depend on the host's sample rate or block size, and processing
// must never allocate, produce non-finite values or let the dry signal change.

// GCC flags free() inside the replacement operator delete as "mismatched" with new-expressions
// once inlined; these are the replacement functions themselves, so the warning does not apply.
#if defined(__GNUC__) && ! defined(__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include "TestUtils.h"
#include "analysis/Character.h"
#include "analysis/Render.h"
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
    std::srand (1); // Cloud Seed seeds its modulation phases from std::rand
    auto engine = std::make_unique<dsp::SpaceEngine>();
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
    std::printf ("Character at 48 kHz against the targets:\n");
    auto engine = makeEngine (48000.0);
    const auto c = analysis::measureCharacter (*engine, 48000.0, plugin::kDefaultMix);
    for (const auto& t : analysis::targetsFor (c))
    {
        std::printf ("  %-55s %8.2f %s\n", t.what.c_str(), t.value, t.unit.c_str());
        CHECK_RANGE (t.value, t.lo, t.hi);
    }
}

void testSampleRates()
{
    std::printf ("The same room at every sample rate (T60 at 1 kHz, wet level of pink noise):\n");
    double t48 = 0.0, level48 = 0.0;
    for (const double rate : { 48000.0, 44100.0, 96000.0 })
    {
        auto engine = makeEngine (rate);
        const auto ir = analysis::render (*engine, analysis::makeImpulse (rate, 25.0).audio, rate);
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
        CHECK_RANGE (t1k / t48, 0.9, 1.1);
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
    // After Freeze is released the room decays again: at least 20 dB down 15 s later.
    const double rate = 48000.0;
    auto engine = makeEngine (rate);
    analysis::RenderOptions options;
    options.freezeOnSeconds = 3.0;
    options.freezeOffSeconds = 8.0;
    const auto out = analysis::render (*engine, analysis::makePad (rate, 20.0).audio, rate, options);
    const double held = analysis::rmsDb (out, (size_t) (7.0 * rate), (size_t) (8.0 * rate));
    const double later = analysis::rmsDb (out, (size_t) (23.0 * rate), (size_t) (24.0 * rate));
    std::printf ("Freeze released at 8 s: %.1f dB before, %.1f dB 15 s later\n", held, later);
    CHECK (later < held - 20.0);
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
        engine->process (&pad.l[pos], &pad.r[pos], &out.l[pos], &out.r[pos], 256);
    }
    countAllocations = false;
    std::printf ("Allocations while processing (with Mix and Freeze moving): %d\n", allocations.load());
    CHECK (allocations.load() == 0);
}
} // namespace

int main()
{
    testNoAllocations();
    testMix();
    testBlockSizes();
    testHostileInput();
    testFreezeReleases();
    testSampleRates();
    testCharacterTargets();
    return finish ("space");
}

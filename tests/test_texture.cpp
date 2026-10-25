// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// The granular texture: Clouds at 32 kHz inside any host rate. The sample-rate converter must be
// clean (no audible error, no aliasing), the texture must sound the same at every host rate, the
// output must never run dry, and no setting of Clouds may produce non-finite values.

#include <cmath>
#include <memory>
#include <utility>
#include "TestUtils.h"
#include "analysis/Measure.h"
#include "dsp/Resampler.h"
#include "dsp/TextureEngine.h"

using namespace spaceforge;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine (double hz, double rate, size_t n)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i)
        v[i] = (float) (0.5 * std::sin (2.0 * kPi * hz * (double) i / rate));
    return v;
}

std::vector<float> convert (const std::vector<float>& in, double from, double to)
{
    dsp::Resampler r;
    r.prepare (from, to);
    std::vector<float> out;
    out.reserve ((size_t) ((double) in.size() * to / from) + 16);
    for (const float x : in)
        r.push (x, x, [&out] (float l, float) { out.push_back (l); });
    return out;
}

void testRoundTrip()
{
    // Host rate -> 32 kHz -> host rate on a 1 kHz sine: the error against the best-aligned
    // original must be at least 60 dB down.
    std::printf ("Sample-rate conversion, 1 kHz sine there and back (error against the original):\n");
    for (const double rate : kSampleRates)
    {
        const auto n = (size_t) rate;
        const auto original = sine (1000.0, rate, n);
        const auto back = convert (convert (original, rate, 32000.0), 32000.0, rate);
        double best = 1.0e9;
        for (size_t lag = 0; lag < 400; ++lag)
        {
            double err = 0.0, sig = 0.0;
            for (size_t i = n / 4; i < n / 2; ++i)
            {
                const double d = (double) back[i + lag] - original[i];
                err += d * d;
                sig += (double) original[i] * original[i];
            }
            best = std::min (best, 10.0 * std::log10 (err / sig));
        }
        std::printf ("  %6.0f Hz: %.1f dB\n", rate, best);
        CHECK (best < -60.0);
    }
}

void testNoAliasing()
{
    // A tone above what 32 kHz can hold must vanish going down, not fold back as a false tone.
    std::printf ("Tone above 16 kHz into 32 kHz (level left, should be gone):\n");
    for (const double rate : kSampleRates)
    {
        const double hz = rate > 60000.0 ? 30000.0 : 19000.0;
        const auto in = sine (hz, rate, (size_t) rate);
        const auto down = convert (in, rate, 32000.0);
        const double level = toDb (rmsOf (down, down.size() / 4)) - toDb (rmsOf (in));
        std::printf ("  %6.0f Hz, %5.0f Hz tone: %.1f dB\n", rate, hz, level);
        CHECK (level < -70.0);
    }
}

void testTextureAtEveryRate()
{
    // Pink noise through the grains: the same level at every host rate, never a gap.
    std::printf ("Grains at every host rate (pink noise in, level out against in, gaps):\n");
    double level48 = 0.0;
    bool first = true;
    for (const double rate : { 48000.0, 44100.0, 96000.0 })
    {
        auto texture = std::make_unique<dsp::TextureEngine>();
        texture->prepare (rate);
        const auto pink = analysis::makePinkNoise (rate, 8.0, 0.125f, 9);
        analysis::Stereo out { std::vector<float> (pink.size()), std::vector<float> (pink.size()) };
        for (size_t pos = 0; pos + 128 <= pink.size(); pos += 128)
            texture->process (&pink.l[pos], &pink.r[pos], &out.l[pos], &out.r[pos], 128);
        const double level = analysis::rmsDb (out, (size_t) (3.0 * rate), pink.size()) - analysis::rmsDb (pink, (size_t) (3.0 * rate), pink.size());
        std::printf ("  %6.0f Hz: %+.2f dB, latency %d samples, gaps %d\n", rate, level, texture->latency(), texture->underruns());
        if (std::exchange (first, false))
            level48 = level;
        CHECK (allFinite (out.l) && allFinite (out.r));
        CHECK (texture->underruns() == 0);
        CHECK_RANGE (level, -1.5, 1.5);
        CHECK_RANGE (level - level48, -1.0, 1.0);
    }
}

void testExtremeSettings()
{
    // Every Clouds setting at both ends (where table lookups hit their edges), with a loud input.
    const double rate = 48000.0;
    const auto pink = analysis::makePinkNoise (rate, 2.0, 0.7f, 4);
    for (int corner = 0; corner < 64; ++corner)
    {
        dsp::TextureVoicing v;
        v.position = corner & 1 ? 1.0 : 0.0;
        v.size = corner & 2 ? 1.0 : 0.0;
        v.density = corner & 4 ? 1.0 : 0.0;
        v.texture = corner & 8 ? 1.0 : 0.0;
        v.stereoSpread = corner & 16 ? 1.0 : 0.0;
        v.feedback = corner & 32 ? 1.0 : 0.0;
        dsp::TextureEngine texture;
        texture.setVoicing (v);
        texture.reset();
        analysis::Stereo out { std::vector<float> (pink.size()), std::vector<float> (pink.size()) };
        for (size_t pos = 0; pos + 256 <= pink.size(); pos += 256)
            texture.process (&pink.l[pos], &pink.r[pos], &out.l[pos], &out.r[pos], 256);
        CHECK (allFinite (out.l) && allFinite (out.r));
        // Bounded: Clouds' 16-bit full scale times the calibration (+9.5 dB, x2 headroom = 6),
        // with room for the converter's overshoot. Feedback 1 drives Clouds to full scale by design.
        CHECK (peakOf (out.l) < 12.0f);
    }
}
} // namespace

int main()
{
    testRoundTrip();
    testNoAliasing();
    testTextureAtEveryRate();
    testExtremeSettings();
    return finish ("texture");
}

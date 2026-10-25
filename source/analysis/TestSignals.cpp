// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "analysis/TestSignals.h"

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace spaceforge::analysis
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Small deterministic generator (the standard library's distributions differ between platforms).
struct Noise
{
    uint32_t state;
    explicit Noise (uint32_t seed) : state (seed * 2654435761u + 1u) {}
    float white() // -1..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((double) state / 4294967295.0 * 2.0 - 1.0);
    }
};

// Paul Kellet's pink noise filter on white noise.
struct Pink
{
    Noise noise;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    explicit Pink (uint32_t seed) : noise (seed) {}
    float next()
    {
        const double w = noise.white();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        return (float) (out * 0.11);
    }
};

struct OnePoleLowPass
{
    double a = 0, z = 0;
    void setup (double hz, double rate) { a = std::exp (-2.0 * kPi * hz / rate); }
    float process (float x) { return (float) (z = x * (1.0 - a) + z * a); }
};

// Band-limited sawtooth (sum of harmonics below the limit): no aliasing, fine for offline use.
double saw (double phase, double hz, double limitHz)
{
    double sum = 0.0;
    for (int k = 1; k * hz < limitHz; ++k)
        sum += std::sin (2.0 * kPi * k * phase) / k;
    return sum * (2.0 / kPi);
}

double noteHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

Stereo silence (double sampleRate, double seconds)
{
    const auto n = (size_t) std::lround (seconds * sampleRate);
    return { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
}

void normaliseRms (Stereo& s, size_t from, size_t to, float rms)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += 0.5 * ((double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i]);
    const double now = std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    if (now <= 0.0)
        return;
    const auto g = (float) (rms / now);
    for (size_t i = 0; i < s.size(); ++i)
    {
        s.l[i] *= g;
        s.r[i] *= g;
    }
}

size_t samples (double seconds, double sampleRate) { return (size_t) std::lround (seconds * sampleRate); }
} // namespace

TestSignal makeImpulse (double sampleRate, double tailSeconds)
{
    // One sample at -6 dBFS: the impulse response, from which decay times are measured.
    TestSignal t { "impuls", "Én sample (kun til målinger).", silence (sampleRate, 0.1 + tailSeconds) };
    t.audio.l[0] = t.audio.r[0] = 0.5f;
    return t;
}

TestSignal makeClick (double sampleRate, double tailSeconds)
{
    // A 2 ms tick of noise: as dry as a click, but with enough energy to hear the tail it leaves.
    TestSignal t { "klik", "Tørt klik (2 ms). Viser rummets hale helt nøgent.", silence (sampleRate, 0.1 + tailSeconds) };
    Noise noise (5);
    const size_t n = samples (0.002, sampleRate);
    for (size_t i = 0; i < n; ++i)
        t.audio.l[i] = t.audio.r[i] = (float) (noise.white() * std::exp (-(double) i / (0.0005 * sampleRate)));
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i)
        peak = std::max (peak, std::abs (t.audio.l[i]));
    for (size_t i = 0; i < n; ++i)
        t.audio.l[i] = t.audio.r[i] = t.audio.l[i] * 0.5f / peak;
    return t;
}

TestSignal makeBleeps (double sampleRate, double tailSeconds)
{
    // Very short sine bleeps (gone after ~40 ms), half a second apart: on these any copy of a
    // note is heard as an echo or a double hit.
    TestSignal t { "bleeps", "Korte bleeps (som BleepForge). Afslører ethvert ekko eller dobbelt-anslag.", silence (sampleRate, 2.0 + tailSeconds) };
    const double notes[] = { 880.0, 660.0, 990.0, 740.0 };
    for (int n = 0; n < 4; ++n)
    {
        const size_t start = samples (0.5 * n, sampleRate);
        for (size_t i = 0; i < samples (0.08, sampleRate); ++i)
        {
            const double time = (double) i / sampleRate;
            const auto s = (float) (0.5 * std::sin (2.0 * kPi * notes[n] * time) * std::exp (-time / 0.008) * std::min (1.0, time / 0.0005));
            t.audio.l[start + i] += s;
            t.audio.r[start + i] += s;
        }
    }
    return t;
}

TestSignal makeNoiseBurst (double sampleRate, double tailSeconds)
{
    TestSignal t { "stoej", "Kort burst af pink støj (250 ms). Viser halens klangfarve og bredde.", silence (sampleRate, 0.25 + tailSeconds) };
    Pink pinkL (11), pinkR (12);
    const size_t n = samples (0.25, sampleRate), fade = samples (0.005, sampleRate);
    for (size_t i = 0; i < n; ++i)
    {
        const double env = std::min (1.0, std::min ((double) i / (double) fade, (double) (n - i) / (double) fade));
        t.audio.l[i] = (float) (pinkL.next() * env);
        t.audio.r[i] = (float) (pinkR.next() * env);
    }
    normaliseRms (t.audio, 0, n, 0.125f); // -18 dBFS
    return t;
}

TestSignal makePad (double sampleRate, double tailSeconds)
{
    // D minor 9 (D3 F3 A3 C4 E4), two detuned saws per note, soft attack and release.
    TestSignal t { "pad", "Pad-akkord (Dm9, 4 s). Viser hvordan rummet folder en lang lyd ud.", silence (sampleRate, 4.0 + tailSeconds) };
    const int notes[] = { 50, 53, 57, 60, 64 };
    const size_t n = samples (4.0, sampleRate);
    std::vector<double> phases (10, 0.0);
    OnePoleLowPass lpL, lpR;
    lpL.setup (1800.0, sampleRate);
    lpR.setup (1800.0, sampleRate);
    for (size_t i = 0; i < n; ++i)
    {
        const double time = (double) i / sampleRate;
        const double env = std::min ({ 1.0, time / 0.4, std::max (0.0, (4.0 - time) / 1.0) });
        double l = 0.0, r = 0.0;
        for (size_t k = 0; k < 5; ++k)
        {
            for (size_t d = 0; d < 2; ++d)
            {
                const double hz = noteHz (notes[k]) * (d == 0 ? 0.997 : 1.003);
                auto& phase = phases[k * 2 + d];
                phase += hz / sampleRate;
                phase -= std::floor (phase);
                const double s = saw (phase, hz, 6000.0);
                (d == 0 ? l : r) += s;
            }
        }
        t.audio.l[i] = lpL.process ((float) (l * env));
        t.audio.r[i] = lpR.process ((float) (r * env));
    }
    normaliseRms (t.audio, samples (0.5, sampleRate), samples (3.0, sampleRate), 0.125f);
    return t;
}

TestSignal makeSynthFigure (double sampleRate, double tailSeconds)
{
    // Two bars of 16th-note plucks in D minor at 125 BPM, a hypnotic repeating figure.
    const double sixteenth = 60.0 / kTestBpm / 4.0;
    const double length = 32 * sixteenth;
    TestSignal t { "figur", "Kort synth-figur (2 takter 1/16-dele, 125 BPM). Viser ekko og hvordan halen hænger sammen med rytme.",
                   silence (sampleRate, length + tailSeconds) };
    // -1 = rest
    const int pattern[16] = { 62, -1, 69, 62, -1, 65, -1, 69, 62, -1, 72, -1, 65, 69, -1, 60 };
    for (int step = 0; step < 32; ++step)
    {
        const int note = pattern[step % 16];
        if (note < 0)
            continue;
        const double hz = noteHz (note);
        const size_t start = samples (step * sixteenth, sampleRate), len = samples (0.25, sampleRate);
        OnePoleLowPass lp;
        double phase = 0.0;
        for (size_t i = 0; i < len && start + i < t.audio.size(); ++i)
        {
            const double time = (double) i / sampleRate;
            const double env = std::exp (-time / 0.08) * std::min (1.0, time / 0.002);
            lp.setup (300.0 + 4000.0 * std::exp (-time / 0.05), sampleRate);
            phase += hz / sampleRate;
            phase -= std::floor (phase);
            const float s = lp.process ((float) (saw (phase, hz, 8000.0) * env));
            t.audio.l[start + i] += s;
            t.audio.r[start + i] += s;
        }
    }
    normaliseRms (t.audio, 0, samples (length, sampleRate), 0.1f);
    return t;
}

TestSignal makeHiHats (double sampleRate, double tailSeconds)
{
    // Two bars: open-ish hats on the off-beat 8ths, quiet closed hats on the 16ths in between.
    const double sixteenth = 60.0 / kTestBpm / 4.0;
    const double length = 32 * sixteenth;
    TestSignal t { "hihat", "Hi-hat-mønster (2 takter, 125 BPM). Viser diskanten: bliver den blød og mørk, eller skarp?",
                   silence (sampleRate, length + tailSeconds) };
    Noise noise (31);
    for (int step = 0; step < 32; ++step)
    {
        const bool offBeat = step % 4 == 2;
        const bool closed = step % 2 == 1;
        if (! offBeat && ! closed)
            continue;
        const double decay = offBeat ? 0.09 : 0.025, level = offBeat ? 1.0 : 0.35;
        const size_t start = samples (step * sixteenth, sampleRate), len = samples (decay * 6.0, sampleRate);
        // Crude high-pass: white noise minus its low-passed self, twice.
        OnePoleLowPass lp1, lp2;
        lp1.setup (6000.0, sampleRate);
        lp2.setup (6000.0, sampleRate);
        for (size_t i = 0; i < len && start + i < t.audio.size(); ++i)
        {
            const double time = (double) i / sampleRate;
            float x = noise.white();
            x -= lp1.process (x);
            x -= lp2.process (x);
            const float s = (float) (x * level * std::exp (-time / decay));
            t.audio.l[start + i] += s;
            t.audio.r[start + i] += s;
        }
    }
    normaliseRms (t.audio, 0, samples (length, sampleRate), 0.07f);
    return t;
}

Stereo makePinkNoise (double sampleRate, double seconds, float rms, unsigned seed)
{
    Stereo s = silence (sampleRate, seconds);
    Pink pinkL (seed), pinkR (seed + 1);
    for (size_t i = 0; i < s.size(); ++i)
    {
        s.l[i] = pinkL.next();
        s.r[i] = pinkR.next();
    }
    normaliseRms (s, 0, s.size(), rms);
    return s;
}

std::vector<TestSignal> allTestSignals (double sampleRate, double tailSeconds)
{
    return { makeClick (sampleRate, tailSeconds), makeBleeps (sampleRate, tailSeconds), makeNoiseBurst (sampleRate, tailSeconds), makePad (sampleRate, tailSeconds),
             makeSynthFigure (sampleRate, tailSeconds), makeHiHats (sampleRate, tailSeconds) };
}
} // namespace spaceforge::analysis

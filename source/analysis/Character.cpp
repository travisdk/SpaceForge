// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "analysis/Character.h"

#include <cstdlib>
#include <memory>
#include "analysis/Render.h"

namespace spaceforge::analysis
{
namespace
{
size_t at (double seconds, double sampleRate) { return (size_t) (seconds * sampleRate); }

// Cloud Seed starts each delay line's modulation at a random phase (std::rand). Two engines
// built after the same srand are identical, which the freeze leak measurement needs.
std::unique_ptr<dsp::SpaceEngine> twinEngine (const dsp::SpaceVoicing& voicing, double sampleRate)
{
    std::srand (7);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setVoicing (voicing);
    engine->prepare (sampleRate);
    return engine;
}
} // namespace

Character measureCharacter (dsp::SpaceEngine& engine, double sampleRate, float defaultMix)
{
    Character c;
    const auto& v = engine.voicing();

    // Impulse response: decay times, density, evenness.
    const auto impulse = makeImpulse (sampleRate, 30.0);
    const auto ir = render (engine, impulse.audio, sampleRate);
    c.t60 = reverbTime (ir, sampleRate);
    c.t60Band = reverbTimePerOctave (ir, sampleRate);
    const double t1k = c.t60Band[3];
    c.darkness4k = t1k > 0 ? c.t60Band[5] / t1k : 0;
    c.darkness8k = t1k > 0 ? c.t60Band[6] / t1k : 0;
    c.bassRatio = t1k > 0 ? c.t60Band[0] / t1k : 0;
    c.densityMs = densityTimeMs (ir, sampleRate);
    c.flutterDb = flutterDb (ir, sampleRate);

    // Noise burst: colour, width and ringing of the tail.
    const auto burst = makeNoiseBurst (sampleRate, 6.0);
    const auto tail = render (engine, burst.audio, sampleRate);
    const size_t from = at (0.6, sampleRate), to = at (4.0, sampleRate);
    c.centroidDry = spectralCentroid (burst.audio, sampleRate, 0, at (0.25, sampleRate));
    c.centroidWet = spectralCentroid (tail, sampleRate, from, to);
    c.corrBass = correlationBelow (tail, sampleRate, 100.0, from, to);
    c.corrHigh = correlationAbove (tail, sampleRate, 300.0, from, to);
    c.monoLossDb = monoSumLossDb (tail, from, to);
    c.ringingDb = ringingDb (tail, sampleRate, from, to);
    c.baseline = noiseBaseline (tail, from, to, c.t60 > 0 ? c.t60 : 10.0, sampleRate);

    // Level with steady material: the last 6 of 12 s of pink noise.
    const auto pink = makePinkNoise (sampleRate, 12.0, 0.125f, 3);
    const double dry = rmsDb (pink, at (6.0, sampleRate), pink.size());
    c.wetMinusDryDb = rmsDb (render (engine, pink, sampleRate), at (6.0, sampleRate), pink.size()) - dry;
    RenderOptions mixed;
    mixed.mix = defaultMix;
    c.mixMinusDryDb = rmsDb (render (engine, pink, sampleRate, mixed), at (6.0, sampleRate), pink.size()) - dry;

    // Freeze: the pad frozen at 3 s holds its level, and new sound does not get into it.
    const auto pad = makePad (sampleRate, 12.0);
    RenderOptions freeze;
    freeze.freezeOnSeconds = 3.0;
    auto quiet = twinEngine (v, sampleRate), busy = twinEngine (v, sampleRate);
    const auto held = render (*quiet, pad.audio, sampleRate, freeze);
    c.freezeDriftDb = rmsDb (held, at (14.0, sampleRate), at (15.0, sampleRate)) - rmsDb (held, at (4.0, sampleRate), at (5.0, sampleRate));

    auto disturbed = pad.audio;
    const auto figure = makeSynthFigure (sampleRate, 0.0);
    for (size_t i = 0; i < figure.audio.size(); ++i)
    {
        disturbed.l[at (6.0, sampleRate) + i] += figure.audio.l[i];
        disturbed.r[at (6.0, sampleRate) + i] += figure.audio.r[i];
    }
    const auto heldDisturbed = render (*busy, disturbed, sampleRate, freeze);
    Stereo difference = held;
    for (size_t i = 0; i < held.size(); ++i)
    {
        difference.l[i] = heldDisturbed.l[i] - held.l[i];
        difference.r[i] = heldDisturbed.r[i] - held.r[i];
    }
    c.freezeLeakDb = rmsDb (difference, at (6.0, sampleRate), at (11.0, sampleRate)) - rmsDb (held, at (6.0, sampleRate), at (11.0, sampleRate));

    c.lineModCents = modulationCents (v.lineModMs, v.lineModHz);
    c.diffuseModCents = modulationCents (v.lateDiffuseModMs, v.lateDiffuseModHz);
    return c;
}

std::vector<Target> targetsFor (const Character& c)
{
    return {
        { "Halelængde (T60, hele spektret)", c.t60, 10.0, 20.0, "s" },
        { "Halelængde ved 1 kHz", c.t60Band[3], 10.0, 20.0, "s" },
        { "Mørk: diskant (4 kHz) dør hurtigere, andel af 1 kHz", c.darkness4k, 0.3, 0.6, "x" },
        { "Mørk: top (8 kHz), andel af 1 kHz", c.darkness8k, 0.1, 0.45, "x" },
        { "Bas (125 Hz) ikke længere end mellemtonen, andel", c.bassRatio, 0.4, 1.0, "x" },
        { "Lysstyrke i halen, andel af den tørre lyd", c.centroidWet / c.centroidDry, 0.25, 0.6, "x" },
        { "Mono i bassen (korrelation under 100 Hz)", c.corrBass, 0.9, 1.0, "" },
        { "Bred over 300 Hz (korrelation)", c.corrHigh, -0.1, 0.4, "" },
        { "Tab ved mono-sum", c.monoLossDb, -3.0, 0.0, "dB" },
        { "Tid til tæt hale (ingen enkelt-ekkoer)", c.densityMs, 0.0, 150.0, "ms" },
        { "Ringen over ideel hale", c.ringingDb - c.baseline.ringingDb, -10.0, 1.5, "dB" },
        { "Ujævnt henfald over ideel hale", c.flutterDb - c.baseline.flutterDb, -10.0, 1.0, "dB" },
        { "Vådt niveau mod tørt (pink støj)", c.wetMinusDryDb, -1.5, 1.5, "dB" },
        { "Niveau ved standard-mix mod tørt", c.mixMinusDryDb, -2.0, 2.0, "dB" },
        { "Freeze: niveauændring over 10 s", c.freezeDriftDb, -3.0, 1.0, "dB" },
        { "Freeze: ny lyd der slipper ind", c.freezeLeakDb, -300.0, -40.0, "dB" },
        { "Pitch-vibrato fra delaylinjer", c.lineModCents, 0.0, 6.0, "cent" },
        { "Pitch-vibrato fra diffusorer", c.diffuseModCents, 0.0, 6.0, "cent" },
    };
}
} // namespace spaceforge::analysis

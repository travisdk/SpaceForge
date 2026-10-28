// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
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
// built after the same srand are identical; a different seed gives the same room with other
// random detail.
std::unique_ptr<dsp::SpaceEngine> twinEngine (const dsp::SpaceVoicing& voicing, const dsp::MacroPositions& macros, double sampleRate, unsigned seed = 7)
{
    std::srand (seed);
    auto engine = std::make_unique<dsp::SpaceEngine>();
    engine->setVoicing (voicing);
    engine->setMacros (macros);
    engine->prepare (sampleRate);
    return engine;
}
} // namespace

Character measureCharacter (dsp::SpaceEngine& engine, double sampleRate, float defaultMix)
{
    Character c;
    const auto v = engine.voicing();
    const auto macros = engine.macros();

    // The room's own character is measured without the grains in front of it: they are random,
    // so an impulse response through them would not be the room's.
    auto roomOnly = v;
    roomOnly.grainsEnabled = false;
    engine.setVoicing (roomOnly);

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

    // Texture: with grains the wet signal is as loud as without (pink noise), and the grains
    // clearly change how the synth figure unfolds in the room (it is scattered into fragments):
    // more than the room's own random detail does (the same room with other modulation phases).
    const auto pink = makePinkNoise (sampleRate, 12.0, 0.125f, 3);
    const auto figure = makeSynthFigure (sampleRate, 4.0);
    const size_t figureTo = at (5.0, sampleRate);
    const double inputOnly = rmsDb (render (engine, pink, sampleRate), at (6.0, sampleRate), pink.size());
    // Two identical fresh engines, one without and one with grains (one engine rendering twice
    // would differ anyway: Cloud Seed's modulation runs on between renders).
    const auto figureRoomOnly = render (*twinEngine (roomOnly, macros, sampleRate), figure.audio, sampleRate);
    const auto figureOtherDetail = render (*twinEngine (roomOnly, macros, sampleRate, 99), figure.audio, sampleRate);
    c.randomChangeDb = spectroTemporalDifferenceDb (figureOtherDetail, figureRoomOnly, sampleRate, 0, figureTo);
    engine.setVoicing (v);
    c.textureLevelDb = rmsDb (render (engine, pink, sampleRate), at (6.0, sampleRate), pink.size()) - inputOnly;
    c.textureUnderruns = engine.textureUnderruns();
    auto withGrains = twinEngine (v, macros, sampleRate);
    const auto figureWith = render (*withGrains, figure.audio, sampleRate);
    c.textureUnderruns += withGrains->textureUnderruns();
    c.textureChangeDb = spectroTemporalDifferenceDb (figureWith, figureRoomOnly, sampleRate, 0, figureTo);

    // Level with steady material: the last 6 of 12 s of pink noise.
    const double dry = rmsDb (pink, at (6.0, sampleRate), pink.size());
    c.wetMinusDryDb = rmsDb (render (engine, pink, sampleRate), at (6.0, sampleRate), pink.size()) - dry;
    RenderOptions mixed;
    mixed.mix = defaultMix;
    c.mixMinusDryDb = rmsDb (render (engine, pink, sampleRate, mixed), at (6.0, sampleRate), pink.size()) - dry;

    // Freeze: the pad frozen at 3 s holds its level, and new sound does not get into it.
    const auto pad = makePad (sampleRate, 12.0);
    RenderOptions freeze;
    freeze.freezeOnSeconds = 3.0;
    auto quiet = twinEngine (v, macros, sampleRate), busy = twinEngine (v, macros, sampleRate);
    const auto held = render (*quiet, pad.audio, sampleRate, freeze);
    c.freezeDriftDb = rmsDb (held, at (14.0, sampleRate), at (15.0, sampleRate)) - rmsDb (held, at (4.0, sampleRate), at (5.0, sampleRate));

    auto disturbed = pad.audio;
    const auto shortFigure = makeSynthFigure (sampleRate, 0.0);
    for (size_t i = 0; i < shortFigure.audio.size(); ++i)
    {
        disturbed.l[at (6.0, sampleRate) + i] += shortFigure.audio.l[i];
        disturbed.r[at (6.0, sampleRate) + i] += shortFigure.audio.r[i];
    }
    const auto heldDisturbed = render (*busy, disturbed, sampleRate, freeze);
    Stereo difference = held;
    for (size_t i = 0; i < held.size(); ++i)
    {
        difference.l[i] = heldDisturbed.l[i] - held.l[i];
        difference.r[i] = heldDisturbed.r[i] - held.r[i];
    }
    c.freezeLeakDb = rmsDb (difference, at (6.0, sampleRate), at (11.0, sampleRate)) - rmsDb (held, at (6.0, sampleRate), at (11.0, sampleRate));

    const auto knobs = dsp::mapMacros (macros);
    c.lineModCents = modulationCents (knobs.motionLineModMs, knobs.motionLineModHz);
    c.diffuseModCents = modulationCents (knobs.motionDiffuseModMs, knobs.motionDiffuseModHz);
    return c;
}

std::vector<Target> targetsFor (const Character& c)
{
    return {
        { "Tail length (T60, full range)", c.t60, 10.0, 20.0, "s" },
        { "Tail length at 1 kHz", c.t60Band[3], 10.0, 20.0, "s" },
        { "Dark: treble (4 kHz) dies faster, share of 1 kHz", c.darkness4k, 0.3, 0.6, "x" },
        { "Dark: top (8 kHz), share of 1 kHz", c.darkness8k, 0.1, 0.45, "x" },
        { "Bass (125 Hz) not longer than the mids, share", c.bassRatio, 0.4, 1.0, "x" },
        { "Brightness of the tail, share of the dry sound", c.centroidWet / c.centroidDry, 0.25, 0.6, "x" },
        { "Mono i bassen (korrelation under 100 Hz)", c.corrBass, 0.9, 1.0, "" },
        { "Bred over 300 Hz (korrelation)", c.corrHigh, -0.1, 0.4, "" },
        { "Tab ved mono-sum", c.monoLossDb, -3.0, 0.0, "dB" },
        { "Time to dense tail (no single echoes)", c.densityMs, 0.0, 150.0, "ms" },
        { "Ringing above ideal tail", c.ringingDb - c.baseline.ringingDb, -10.0, 1.5, "dB" },
        { "Uneven decay above ideal tail", c.flutterDb - c.baseline.flutterDb, -10.0, 1.0, "dB" },
        { "Wet level against dry (pink noise)", c.wetMinusDryDb, -1.5, 1.5, "dB" },
        { "Level at default mix against dry", c.mixMinusDryDb, -2.0, 2.0, "dB" },
        { "Freeze: level change over 10 s", c.freezeDriftDb, -3.0, 1.0, "dB" },
        { "Freeze: ny lyd der slipper ind", c.freezeLeakDb, -300.0, -40.0, "dB" },
        { "Pitch-vibrato fra delaylinjer", c.lineModCents, 0.0, 6.0, "cent" },
        { "Pitch-vibrato fra diffusorer", c.diffuseModCents, 0.0, 6.0, "cent" },
    };
}
} // namespace spaceforge::analysis

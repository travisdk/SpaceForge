// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/SpaceEngine.h"

#include <algorithm>
#include <cmath>
#include "DSP/ReverbChannel.h"

namespace spaceforge::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kMixSmoothingSeconds = 0.03;
constexpr double kFreezeFadeSeconds = 0.05;

// Cloud Seed mutes an output at -30 dB.
constexpr double kMutedDb = -30.0;
} // namespace

void SpaceEngine::HighPass::setup (double hz, double rate)
{
    // RBJ cookbook, Butterworth Q
    const double w = 2.0 * kPi * hz / rate, cosw = std::cos (w), alpha = std::sin (w) / (2.0 * std::sqrt (0.5));
    const double a0 = 1.0 + alpha;
    b0 = (1.0 + cosw) / 2.0 / a0;
    b1 = -(1.0 + cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
    s1 = s2 = 0.0;
}

float SpaceEngine::HighPass::process (float x)
{
    const double y = b0 * x + s1;
    s1 = b1 * x - a1 * y + s2;
    s2 = b2 * x - a2 * y;
    return (float) y;
}

SpaceEngine::SpaceEngine()
    : left (std::make_unique<Cloudseed::ReverbChannel> (48000, Cloudseed::ChannelLR::Left)),
      right (std::make_unique<Cloudseed::ReverbChannel> (48000, Cloudseed::ChannelLR::Right))
{
    prepare (48000.0);
}

SpaceEngine::~SpaceEngine() = default;

void SpaceEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    chunk = std::clamp ((int) std::lround (kChunkSeconds * sampleRate), 16, kMaxChunk);
    left->SetSamplerate ((int) sampleRate);
    right->SetSamplerate ((int) sampleRate);
    mixCoeff = (float) (1.0 - std::exp (-1.0 / (kMixSmoothingSeconds * sampleRate)));
    inputGainStep = (float) (1.0 / (kFreezeFadeSeconds * sampleRate));
    applyVoicing();
    reset();
}

void SpaceEngine::setVoicing (const SpaceVoicing& voicing)
{
    currentVoicing = voicing;
    applyVoicing();
}

void SpaceEngine::applyVoicing()
{
    namespace P = Cloudseed::Parameter;
    const auto& v = currentVoicing;
    for (auto* channel : { left.get(), right.get() })
    {
        auto set = [channel] (int id, double value) { channel->SetParameter (id, value); };
        set (P::Interpolation, 1.0);
        set (P::InputMix, v.crossFeed);
        set (P::LowCutEnabled, 1.0);
        set (P::LowCut, v.inputLowCutHz);
        set (P::HighCutEnabled, 1.0);
        set (P::HighCut, v.inputHighCutHz);
        set (P::DryOut, kMutedDb); // the dry signal is mixed here, without delay
        set (P::EarlyOut, v.earlyLevelDb);
        set (P::LateOut, v.lateLevelDb);

        set (P::TapEnabled, 0.0);
        set (P::TapCount, 1.0);
        set (P::TapDecay, 1.0);
        set (P::TapLength, 10.0);
        set (P::TapPredelay, v.preDelayMs);

        set (P::EarlyDiffuseEnabled, 1.0);
        set (P::EarlyDiffuseCount, v.earlyStages);
        set (P::EarlyDiffuseDelay, v.earlyDelayMs);
        set (P::EarlyDiffuseFeedback, v.earlyFeedback);
        set (P::EarlyDiffuseModAmount, v.earlyModMs);
        set (P::EarlyDiffuseModRate, v.earlyModHz);

        set (P::LateMode, 1.0); // listen after each line's diffuser: denser
        set (P::LateLineCount, v.lineCount);
        set (P::LateDiffuseEnabled, 1.0);
        set (P::LateDiffuseCount, v.lateStages);
        set (P::LateDiffuseDelay, v.lateDiffuseDelayMs);
        set (P::LateDiffuseFeedback, v.lateDiffuseFeedback);
        set (P::LateDiffuseModAmount, v.lateDiffuseModMs);
        set (P::LateDiffuseModRate, v.lateDiffuseModHz);
        set (P::LateLineSize, v.lineSizeMs);
        set (P::LateLineModAmount, v.lineModMs);
        set (P::LateLineModRate, v.lineModHz);

        set (P::EqLowShelfEnabled, 1.0);
        set (P::EqHighShelfEnabled, 1.0);
        set (P::EqLowpassEnabled, 1.0);
        set (P::EqLowFreq, v.lowShelfHz);
        set (P::EqHighFreq, v.highShelfHz);

        set (P::EqCrossSeed, v.crossSeed);
        set (P::SeedTap, v.seedTap);
        set (P::SeedDiffusion, v.seedDiffusion);
        set (P::SeedDelay, v.seedDelay);
        set (P::SeedPostDiffusion, v.seedPostDiffusion);
    }
    applyFreeze (frozen);
    for (auto& f : sideHighPass)
        f.setup (v.monoBassHz, sampleRate);
    wetGain = (float) std::pow (10.0, v.wetGainDb / 20.0);
}

void SpaceEngine::applyFreeze (bool on)
{
    // Frozen: the loops keep (practically) everything and the damping is lifted, so the frozen
    // sound neither fades nor slowly turns into a dark hum. Only coefficients change here, so
    // there is no jump in the signal.
    namespace P = Cloudseed::Parameter;
    const auto& v = currentVoicing;
    frozen = on;
    for (auto* channel : { left.get(), right.get() })
    {
        channel->SetParameter (P::LateLineDecay, on ? kFrozenDecaySeconds : v.decaySeconds);
        channel->SetParameter (P::EqLowGain, on ? 0.0 : v.lowShelfDb);
        channel->SetParameter (P::EqHighGain, on ? 0.0 : v.highShelfDb);
        channel->SetParameter (P::EqCutoff, on ? 0.45 * sampleRate : v.lowpassHz);
    }
}

void SpaceEngine::reset()
{
    left->ClearBuffers();
    right->ClearBuffers();
    for (auto& f : sideHighPass)
        f.s1 = f.s2 = 0.0;
    inL.fill (0.0f);
    inR.fill (0.0f);
    wetL.fill (0.0f);
    wetR.fill (0.0f);
    pos = 0;
    mixNow = mixTarget;
    if (freezeTarget != frozen)
        applyFreeze (freezeTarget);
    inputGain = frozen ? 0.0f : 1.0f;
}

void SpaceEngine::process (const float* inputL, const float* inputR, float* outL, float* outR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Anything a host might hand us that is not a number becomes silence before the loops.
        const float dryL = std::isfinite (inputL[i]) ? inputL[i] : 0.0f;
        const float dryR = std::isfinite (inputR[i]) ? inputR[i] : 0.0f;

        const float gainTarget = frozen ? 0.0f : 1.0f;
        inputGain = inputGain < gainTarget ? std::min (gainTarget, inputGain + inputGainStep)
                                           : std::max (gainTarget, inputGain - inputGainStep);
        inL[(size_t) pos] = dryL * inputGain;
        inR[(size_t) pos] = dryR * inputGain;
        const float wl = wetL[(size_t) pos], wr = wetR[(size_t) pos];
        if (++pos == chunk)
        {
            processChunk();
            pos = 0;
        }

        // Equal-power mix: dry and wet are about equally loud, so the sum stays level.
        const float target = std::clamp (mixTarget, 0.0f, 1.0f);
        mixNow += (target - mixNow) * mixCoeff;
        if (std::abs (target - mixNow) < 1.0e-5f)
            mixNow = target; // land exactly, so 0 % and 100 % are pure
        const float angle = mixNow * (float) (kPi / 2.0);
        const float dryGain = mixNow >= 1.0f ? 0.0f : std::cos (angle);
        const float wetMix = mixNow <= 0.0f ? 0.0f : std::sin (angle);
        outL[i] = dryL * dryGain + wl * wetMix;
        outR[i] = dryR * dryGain + wr * wetMix;
    }
}

void SpaceEngine::processChunk()
{
    if (freezeTarget != frozen)
        applyFreeze (freezeTarget);

    // Each side hears a little of the other (Cloud Seed's own input mix).
    const float cross = (float) currentVoicing.crossFeed * 0.5f;
    std::array<float, kMaxChunk> feedL, feedR;
    for (int i = 0; i < chunk; ++i)
    {
        feedL[(size_t) i] = inL[(size_t) i] * (1.0f - cross) + inR[(size_t) i] * cross;
        feedR[(size_t) i] = inR[(size_t) i] * (1.0f - cross) + inL[(size_t) i] * cross;
    }
    left->Process (feedL.data(), wetL.data(), chunk);
    right->Process (feedR.data(), wetR.data(), chunk);

    bool finite = true;
    for (int i = 0; i < chunk; ++i)
    {
        const float mid = 0.5f * (wetL[(size_t) i] + wetR[(size_t) i]);
        const float side = sideHighPass[1].process (sideHighPass[0].process (0.5f * (wetL[(size_t) i] - wetR[(size_t) i])));
        wetL[(size_t) i] = (mid + side) * wetGain;
        wetR[(size_t) i] = (mid - side) * wetGain;
        finite = finite && std::isfinite (wetL[(size_t) i]) && std::isfinite (wetR[(size_t) i]);
    }

    // Cannot happen with stable loops; if it ever does, silence beats a burst of noise.
    if (! finite)
    {
        left->ClearBuffers();
        right->ClearBuffers();
        for (auto& f : sideHighPass)
            f.s1 = f.s2 = 0.0;
        wetL.fill (0.0f);
        wetR.fill (0.0f);
    }
}
} // namespace spaceforge::dsp

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
// The knobs glide to a new position in about this time (smooth, but quick to follow).
constexpr double kMacroSmoothingSeconds = 0.08;
constexpr double kPanSmoothingSeconds = 0.02;
constexpr double kTiltHz = 2000.0;

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
    settings = mapMacros (macroNow);
    prepare (48000.0);
}

SpaceEngine::~SpaceEngine() = default;

void SpaceEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    chunk = std::clamp ((int) std::lround (kChunkSeconds * sampleRate), 16, kMaxChunk);
    left->SetSamplerate ((int) sampleRate);
    right->SetSamplerate ((int) sampleRate);
    texture.prepare (sampleRate);
    echo.prepare (sampleRate);
    mixCoeff = (float) (1.0 - std::exp (-1.0 / (kMixSmoothingSeconds * sampleRate)));
    macroCoeff = (float) (1.0 - std::exp (-(double) chunk / (kMacroSmoothingSeconds * sampleRate)));
    panCoeff = (float) (1.0 - std::exp (-1.0 / (kPanSmoothingSeconds * sampleRate)));
    tiltCoeff = (float) (1.0 - std::exp (-2.0 * kPi * kTiltHz / sampleRate));
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
    // The fixed part of the sound. Seeds allocate in Cloud Seed: never on the audio thread.
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
        set (P::LateLineSize, v.lineSizeMs);
        set (P::LateDiffuseEnabled, 1.0);
        set (P::LateDiffuseCount, v.lateStages);
        set (P::LateDiffuseDelay, v.lateDiffuseDelayMs);
        set (P::LateDiffuseFeedback, v.lateDiffuseFeedback);

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
    for (auto& f : sideHighPass)
        f.setup (v.monoBassHz, sampleRate);
    applySettings (settings, true);
}

void SpaceEngine::applySettings (const MacroSettings& s, bool force)
{
    // What the knobs move: only what changed is passed on (Cloud Seed recomputes its lines on
    // every call). Never allocates.
    namespace P = Cloudseed::Parameter;
    const auto changed = [force] (double now, double before) { return force || std::abs (now - before) > 1.0e-6 * std::max (1.0, std::abs (before)); };
    const auto& old = settings;
    for (auto* channel : { left.get(), right.get() })
    {
        auto set = [channel] (int id, double value) { channel->SetParameter (id, value); };
        if (changed (s.motionLineModMs, old.motionLineModMs))
            set (P::LateLineModAmount, s.motionLineModMs);
        if (changed (s.motionLineModHz, old.motionLineModHz))
            set (P::LateLineModRate, s.motionLineModHz);
        if (changed (s.motionDiffuseModMs, old.motionDiffuseModMs))
            set (P::LateDiffuseModAmount, s.motionDiffuseModMs);
        if (changed (s.motionDiffuseModHz, old.motionDiffuseModHz))
            set (P::LateDiffuseModRate, s.motionDiffuseModHz);
        if (changed (s.toneInputHighCutHz, old.toneInputHighCutHz))
            set (P::HighCut, s.toneInputHighCutHz);
        if (! frozen)
        {
            if (changed (s.spaceDecaySeconds, old.spaceDecaySeconds))
                set (P::LateLineDecay, s.spaceDecaySeconds);
            if (changed (s.toneLowpassHz, old.toneLowpassHz))
                set (P::EqCutoff, s.toneLowpassHz);
            if (changed (s.toneHighShelfDb, old.toneHighShelfDb))
                set (P::EqHighGain, s.toneHighShelfDb);
        }
    }
    if (force || changed (s.motionDensity, old.motionDensity) || changed (s.motionFeedback, old.motionFeedback)
        || changed (s.motionTextureShape, old.motionTextureShape) || changed (s.motionTextureGainDb, old.motionTextureGainDb))
    {
        auto t = currentVoicing.texture;
        t.density = s.motionDensity;
        t.feedback = s.motionFeedback;
        t.texture = s.motionTextureShape;
        t.gainDb = s.motionTextureGainDb;
        texture.setVoicing (t);
    }
    echo.setFeedback (s.echoFeedback);
    echo.setDampingHz (s.toneEchoDampingHz);
    // Tone's finish: a first-order shelf above ~2 kHz, as gain on a high-passed copy.
    tiltGain = (float) (std::pow (10.0, s.toneTiltDb / 20.0) - 1.0);
    wetGain = (float) std::pow (10.0, (currentVoicing.wetGainDb + s.spaceGainDb + s.echoGainDb + s.motionGainDb + s.toneGainDb) / 20.0);
    settings = s;
    if (force && ! frozen)
        applyFreeze (false);
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
        channel->SetParameter (P::LateLineDecay, on ? kFrozenDecaySeconds : settings.spaceDecaySeconds);
        channel->SetParameter (P::EqLowGain, on ? 0.0 : v.lowShelfDb);
        channel->SetParameter (P::EqHighGain, on ? 0.0 : settings.toneHighShelfDb);
        channel->SetParameter (P::EqCutoff, on ? 0.45 * sampleRate : settings.toneLowpassHz);
    }
}

void SpaceEngine::setTempo (double newBpm, bool playing, double ppqPosition)
{
    if (std::isfinite (newBpm) && newBpm >= 20.0 && newBpm <= 999.0)
        bpm = newBpm;
    if (playing && std::isfinite (ppqPosition))
        ppq = ppqPosition;
}

void SpaceEngine::reset()
{
    left->ClearBuffers();
    right->ClearBuffers();
    texture.reset();
    echo.reset();
    for (auto& f : sideHighPass)
        f.s1 = f.s2 = 0.0;
    tiltLowL = tiltLowR = 0.0f;
    inL.fill (0.0f);
    inR.fill (0.0f);
    wetL.fill (0.0f);
    wetR.fill (0.0f);
    pos = 0;
    mixNow = mixTarget;
    macroNow = macroTarget;
    if (freezeTarget != frozen)
        applyFreeze (freezeTarget);
    applySettings (mapMacros (macroNow), true);
    echo.setDelaySeconds (kEchoBeats * 60.0 / bpm);
    echo.reset();
    inputGain = frozen ? 0.0f : 1.0f;
    panL = panR = 1.0f;
    const auto amount = currentVoicing.grainsEnabled ? (float) std::clamp (settings.motionTextureAmount, 0.0, 1.0) : 0.0f;
    gains = { std::cos (amount * (float) (kPi / 2.0)), std::sin (amount * (float) (kPi / 2.0)), (float) settings.echoSend,
              currentVoicing.grainsEnabled ? (float) std::pow (10.0, settings.motionGrainsDirectDb / 20.0) : 0.0f, wetGain };
}

void SpaceEngine::process (const float* inputL, const float* inputR, float* outL, float* outR, int numSamples)
{
    const double beatsPerSample = bpm / (60.0 * sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        // Anything a host might hand us that is not a number becomes silence before the loops.
        const float dryL = std::isfinite (inputL[i]) ? inputL[i] : 0.0f;
        const float dryR = std::isfinite (inputR[i]) ? inputR[i] : 0.0f;

        const float gainTarget = frozen ? 0.0f : 1.0f;
        inputGain = inputGain < gainTarget ? std::min (gainTarget, inputGain + inputGainStep)
                                           : std::max (gainTarget, inputGain - inputGainStep);
        inL[(size_t) pos] = dryL;
        inR[(size_t) pos] = dryR;
        inGain[(size_t) pos] = inputGain;
        float wl = wetL[(size_t) pos], wr = wetR[(size_t) pos];
        if (++pos == chunk)
        {
            processChunk();
            pos = 0;
        }

        // Auto-pan (Motion), locked to the host's bars: equal-power, so the room keeps its level.
        const double cycleBeats = settings.motionPanBars == 0 ? 2.0 : 4.0 * settings.motionPanBars;
        const double phase = ppq / cycleBeats;
        const auto swing = (float) (settings.motionPanDepth * std::sin (2.0 * kPi * (phase - std::floor (phase))));
        const float angle = (float) (kPi / 4.0) * (1.0f + swing);
        panL += (std::cos (angle) * (float) std::sqrt (2.0) - panL) * panCoeff;
        panR += (std::sin (angle) * (float) std::sqrt (2.0) - panR) * panCoeff;
        ppq += beatsPerSample;
        wl *= panL;
        wr *= panR;

        // Equal-power mix: dry and wet are about equally loud, so the sum stays level.
        const float target = std::clamp (mixTarget, 0.0f, 1.0f);
        mixNow += (target - mixNow) * mixCoeff;
        if (std::abs (target - mixNow) < 1.0e-5f)
            mixNow = target; // land exactly, so 0 % and 100 % are pure
        const float mixAngle = mixNow * (float) (kPi / 2.0);
        const float dryGain = mixNow >= 1.0f ? 0.0f : std::cos (mixAngle);
        const float wetMix = mixNow <= 0.0f ? 0.0f : std::sin (mixAngle);
        outL[i] = dryL * dryGain + wl * wetMix;
        outR[i] = dryR * dryGain + wr * wetMix;
    }
}

void SpaceEngine::processChunk()
{
    if (freezeTarget != frozen)
    {
        applyFreeze (freezeTarget);
        if (! frozen)
            applySettings (settings, true); // back to the knobs' damping and decay
    }

    // The knobs glide to their new positions.
    auto glide = [this] (float& now, float target) {
        now += (target - now) * macroCoeff;
        if (std::abs (target - now) < 1.0e-4f)
            now = target;
    };
    const auto before = macroNow;
    glide (macroNow.space, macroTarget.space);
    glide (macroNow.echo, macroTarget.echo);
    glide (macroNow.motion, macroTarget.motion);
    glide (macroNow.tone, macroTarget.tone);
    const auto moved = [] (float a, float b) { return std::abs (a - b) > 0.0f; };
    if (moved (before.space, macroNow.space) || moved (before.echo, macroNow.echo) || moved (before.motion, macroNow.motion) || moved (before.tone, macroNow.tone))
        applySettings (mapMacros (macroNow), false);
    echo.setDelaySeconds (kEchoBeats * 60.0 / bpm);

    // Clouds runs all the time (also frozen) while it is in use, so its last second of sound is
    // always current. Out of the path (no grains anywhere) it is not run at all.
    if (gains.grains > 0.0f || settings.motionTextureAmount > 0.0 || settings.motionGrainsDirectDb > -60.0)
        texture.process (inL.data(), inR.data(), grainsL.data(), grainsR.data(), chunk);
    else
    {
        std::fill (grainsL.begin(), grainsL.begin() + chunk, 0.0f);
        std::fill (grainsR.begin(), grainsR.begin() + chunk, 0.0f);
    }

    // Into the room: input and grains (equal-power) and the echoes of both, all muted while
    // frozen. Each side hears a little of the other (Cloud Seed's own input mix).
    // The gains the knobs set glide sample by sample from the last chunk's values, so a knob
    // moving never steps them (steps every 2.7 ms would buzz).
    const auto amount = currentVoicing.grainsEnabled ? (float) std::clamp (settings.motionTextureAmount, 0.0, 1.0) : 0.0f;
    const Gains target { std::cos (amount * (float) (kPi / 2.0)), std::sin (amount * (float) (kPi / 2.0)), (float) settings.echoSend,
                         currentVoicing.grainsEnabled ? (float) std::pow (10.0, settings.motionGrainsDirectDb / 20.0) : 0.0f, wetGain };
    const Gains from = gains;
    gains = target;
    auto glideAt = [&] (float Gains::*g, int i) { return from.*g + (target.*g - from.*g) * (float) (i + 1) / (float) chunk; };

    std::array<float, kMaxChunk> feedL, feedR;
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float inputPart = glideAt (&Gains::input, i), grainPart = glideAt (&Gains::grains, i), send = glideAt (&Gains::send, i);
        feedL[k] = (inL[k] * inputPart + grainsL[k] * grainPart) * inGain[k];
        feedR[k] = (inR[k] * inputPart + grainsR[k] * grainPart) * inGain[k];
        echoL[k] = feedL[k] * send;
        echoR[k] = feedR[k] * send;
    }
    echo.process (echoL.data(), echoR.data(), echoL.data(), echoR.data(), chunk);

    const float cross = (float) currentVoicing.crossFeed * 0.5f;
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float l = feedL[k] + echoL[k] * inGain[k];
        const float r = feedR[k] + echoR[k] * inGain[k];
        feedL[k] = l * (1.0f - cross) + r * cross;
        feedR[k] = r * (1.0f - cross) + l * cross;
    }
    left->Process (feedL.data(), wetL.data(), chunk);
    right->Process (feedR.data(), wetR.data(), chunk);

    // The grains and the echoes are also heard straight away beside the room, where it has not
    // yet dissolved them.
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float direct = glideAt (&Gains::direct, i);
        wetL[k] += (grainsL[k] * direct + echoL[k]) * inGain[k];
        wetR[k] += (grainsR[k] * direct + echoR[k]) * inGain[k];
    }

    // Tone's finish, mono bass, stereo width (Space), level.
    const auto width = (float) settings.spaceWidth;
    bool finite = true;
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        tiltLowL += (wetL[k] - tiltLowL) * tiltCoeff;
        tiltLowR += (wetR[k] - tiltLowR) * tiltCoeff;
        wetL[k] += (wetL[k] - tiltLowL) * tiltGain;
        wetR[k] += (wetR[k] - tiltLowR) * tiltGain;
        const float mid = 0.5f * (wetL[k] + wetR[k]);
        const float side = sideHighPass[1].process (sideHighPass[0].process (0.5f * (wetL[k] - wetR[k]))) * width;
        const float level = glideAt (&Gains::wet, i);
        wetL[k] = (mid + side) * level;
        wetR[k] = (mid - side) * level;
        finite = finite && std::isfinite (wetL[k]) && std::isfinite (wetR[k]);
    }

    // Cannot happen with stable loops; if it ever does, silence beats a burst of noise.
    if (! finite)
    {
        left->ClearBuffers();
        right->ClearBuffers();
        echo.reset();
        for (auto& f : sideHighPass)
            f.s1 = f.s2 = 0.0;
        wetL.fill (0.0f);
        wetR.fill (0.0f);
    }
}
} // namespace spaceforge::dsp

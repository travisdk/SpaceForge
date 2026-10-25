// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/SpaceEngine.h"
#include "dsp/Modes.h"

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
constexpr double kModeFadeSeconds = 0.02; // out and in again: 40 ms in all (CLAUDE.md: 30-50 ms)
// The knobs glide to a new position in about this time (smooth, but quick to follow).
constexpr double kMacroSmoothingSeconds = 0.08;
constexpr double kPanSmoothingSeconds = 0.02;
constexpr double kTiltHz = 2000.0;
// Duck: the input's level (peak) that starts ducking and the level where the full depth is
// reached; fast attack, slow release (the tail comes back in the pauses).
constexpr double kDuckAttackSeconds = 0.008, kDuckReleaseSeconds = 0.4;
// Echo's repeats are heard in full beside the room (sharp); only this much of them goes into it.
constexpr float kDelayRoomShare = 0.5f;
// The repeats heard straight out beside the room: louder than the room's own copy, so they stand
// up out of the tail in a track (Henrik: the first Echo was too faint to hear).
constexpr float kDelayDirect = 1.6f;
constexpr float kDuckStartDb = -45.0f, kDuckFullDb = -27.0f;

// Cloud Seed mutes an output at -30 dB.
constexpr double kMutedDb = -30.0;
} // namespace

void SpaceEngine::Biquad::setup (double hz, double rate, bool highPass)
{
    // RBJ cookbook, Butterworth Q
    const double w = 2.0 * kPi * hz / rate, cosw = std::cos (w), alpha = std::sin (w) / (2.0 * std::sqrt (0.5));
    const double a0 = 1.0 + alpha;
    b0 = (highPass ? 1.0 + cosw : 1.0 - cosw) / 2.0 / a0;
    b1 = (highPass ? -(1.0 + cosw) : 1.0 - cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
    s1 = s2 = 0.0;
}

float SpaceEngine::Biquad::process (float x)
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

SpaceEngine::Gains SpaceEngine::gainsNow() const
{
    const auto gainOf = [] (double db) { return db > -60.0 ? (float) std::pow (10.0, db / 20.0) : 0.0f; };
    const bool sky = currentVoicing.grainsEnabled;
    return { (float) settings.reflectSend, sky ? gainOf (settings.spaceSkyFeedDb) : 0.0f, sky ? gainOf (settings.spaceSkyOutDb) : 0.0f, wetGain,
             (float) settings.delaySend };
}

void SpaceEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    chunk = std::clamp ((int) std::lround (kChunkSeconds * sampleRate), 16, kMaxChunk);
    left->SetSamplerate ((int) sampleRate);
    right->SetSamplerate ((int) sampleRate);
    texture.prepare (sampleRate);
    echo.prepare (sampleRate);
    delay.prepare (sampleRate);
    duckAttack = (float) (1.0 - std::exp (-1.0 / (kDuckAttackSeconds * sampleRate)));
    duckRelease = (float) (1.0 - std::exp (-1.0 / (kDuckReleaseSeconds * sampleRate)));
    mixCoeff = (float) (1.0 - std::exp (-1.0 / (kMixSmoothingSeconds * sampleRate)));
    macroCoeff = (float) (1.0 - std::exp (-(double) chunk / (kMacroSmoothingSeconds * sampleRate)));
    panCoeff = (float) (1.0 - std::exp (-1.0 / (kPanSmoothingSeconds * sampleRate)));
    tiltCoeff = (float) (1.0 - std::exp (-2.0 * kPi * kTiltHz / sampleRate));
    inputGainStep = (float) (1.0 / (kFreezeFadeSeconds * sampleRate));
    modeFadeStep = (float) (1.0 / (kModeFadeSeconds * sampleRate));
    if (macroTarget.mode != activeMode)
    {
        const bool grains = currentVoicing.grainsEnabled;
        activeMode = std::clamp (macroTarget.mode, 0, kNumModes - 1);
        currentVoicing = modeVoicing (activeMode);
        currentVoicing.grainsEnabled = grains;
    }
    applyVoicing();
    reset();
}

void SpaceEngine::setVoicing (const SpaceVoicing& voicing)
{
    currentVoicing = voicing;
    applyVoicing();
}

void SpaceEngine::applyVoicing (bool withSeeds)
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

        set (P::TapEnabled, v.tapsEnabled ? 1.0 : 0.0);
        set (P::TapCount, v.tapCount);
        set (P::TapDecay, v.tapDecay);
        set (P::TapLength, v.tapLengthMs);

        set (P::TapPredelay, v.preDelayMs);

        set (P::EarlyDiffuseEnabled, v.earlyEnabled ? 1.0 : 0.0);
        set (P::EarlyDiffuseCount, v.earlyStages);
        set (P::EarlyDiffuseDelay, v.earlyDelayMs);
        set (P::EarlyDiffuseFeedback, v.earlyFeedback);
        set (P::EarlyDiffuseModAmount, v.earlyModMs);
        set (P::EarlyDiffuseModRate, v.earlyModHz);

        set (P::LateMode, 1.0); // listen after each line's diffuser: denser
        set (P::LateLineCount, v.lineCount);
        set (P::LateLineSize, v.lineSizeMs);
        set (P::LateDiffuseEnabled, v.lateEnabled ? 1.0 : 0.0);
        set (P::LateDiffuseCount, v.lateStages);
        set (P::LateDiffuseDelay, v.lateDiffuseDelayMs);
        set (P::LateDiffuseFeedback, v.lateDiffuseFeedback);

        set (P::EqLowShelfEnabled, 1.0);
        set (P::EqHighShelfEnabled, 1.0);
        set (P::EqLowpassEnabled, 1.0);
        set (P::EqLowFreq, v.lowShelfHz);
        set (P::EqHighFreq, v.highShelfHz);

        if (withSeeds)
        {
            set (P::EqCrossSeed, v.crossSeed);
            set (P::SeedTap, v.seedTap);
            set (P::SeedDiffusion, v.seedDiffusion);
            set (P::SeedDelay, v.seedDelay);
            set (P::SeedPostDiffusion, v.seedPostDiffusion);
        }
    }
    for (auto& f : sideHighPass)
        f.setup (v.monoBassHz, sampleRate);
    for (auto& f : midHighPass)
        f.setup (v.monoBassHz, sampleRate);
    for (auto& f : midLowPass)
        f.setup (v.monoBassHz, sampleRate, false);
    skyLimit = (float) std::pow (10.0, v.skyLimitDb / 20.0);
    skyLevelCoeff = (float) (1.0 - std::exp (-1.0 / (0.3 * sampleRate)));
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
    if (force)
        texture.setVoicing (currentVoicing.texture);
    echo.setFeedback (s.reflectFeedback);
    echo.setDampingHz (s.toneEchoDampingHz);
    delay.setFeedback (s.delayFeedback);
    delay.setDampingHz (1.5 * s.toneEchoDampingHz); // a little brighter than Reflect: sharp
    // Tone's finish: a first-order shelf above ~2 kHz, as gain on a high-passed copy.
    tiltGain = (float) (std::pow (10.0, s.toneTiltDb / 20.0) - 1.0);
    wetGain = (float) std::pow (10.0, (currentVoicing.wetGainDb + s.spaceGainDb + s.reflectGainDb + s.motionGainDb + s.toneGainDb + s.delayGainDb) / 20.0);
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

void SpaceEngine::setDelayTimes()
{
    const double leftSeconds = settings.delayBeats * 60.0 / bpm;
    delay.setDelaySeconds (leftSeconds, leftSeconds * 4.0 / 3.0);
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
    if (const int wanted = std::clamp (macroTarget.mode, 0, kNumModes - 1); wanted != activeMode)
        switchMode (wanted);
    modeFading = false;
    jumpPending = false;
    modeFade = 1.0f;
    left->ClearBuffers();
    right->ClearBuffers();
    texture.reset();
    echo.reset();
    for (auto& f : sideHighPass)
        f.s1 = f.s2 = 0.0;
    for (auto& f : midHighPass)
        f.s1 = f.s2 = 0.0;
    for (auto& f : midLowPass)
        f.s1 = f.s2 = 0.0;
    tiltLowL = tiltLowR = 0.0f;
    inL.fill (0.0f);
    roomL.fill (0.0f);
    roomR.fill (0.0f);
    inR.fill (0.0f);
    wetL.fill (0.0f);
    wetR.fill (0.0f);
    pos = 0;
    mixNow = mixTarget;
    macroNow = macroTarget;
    if (freezeTarget != frozen)
        applyFreeze (freezeTarget);
    applySettings (mapMacros (macroNow), true);
    echo.setDelaySeconds (kReflectBeats * 60.0 / bpm);
    echo.reset();
    setDelayTimes();
    delay.reset();
    duckEnv = 0.0f;
    duckGain = 1.0f;
    inputGain = frozen ? 0.0f : 1.0f;
    panL = panR = 1.0f;
    gains = gainsNow();
    skyLevel = 0.0f;
}

void SpaceEngine::switchMode (int mode)
{
    // The wet signal is silent here: the old tail goes, the new setup comes in whole. No seeds
    // change (all Modes share them), so nothing allocates.
    const bool grains = currentVoicing.grainsEnabled;
    activeMode = mode;
    currentVoicing = modeVoicing (mode);
    currentVoicing.grainsEnabled = grains;
    applyVoicing (false);
    left->ClearBuffers();
    right->ClearBuffers();
    texture.reset();
    echo.reset();
    delay.reset();
    for (auto* filters : { &sideHighPass, &midHighPass, &midLowPass })
        for (auto& f : *filters)
            f.s1 = f.s2 = 0.0;
    tiltLowL = tiltLowR = 0.0f;
    roomL.fill (0.0f);
    roomR.fill (0.0f);
    macroNow = macroTarget;
    macroNow.mode = mode;
    applySettings (mapMacros (macroNow), true);
    gains = gainsNow();
    skyLevel = 0.0f;
    modeFading = false;
    jumpPending = false;
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

        // Auto-pan (Motion), locked to the host's bars: equal-power, so the room keeps its level.
        // Applied in processChunk together with the mono bass, so the bass stays in the middle.
        const double cycleBeats = settings.motionPanBars == 0 ? 2.0 : 4.0 * settings.motionPanBars;
        const double phase = ppq / cycleBeats;
        const auto swing = (float) (settings.motionPanDepth * std::sin (2.0 * kPi * (phase - std::floor (phase))));
        const float angle = (float) (kPi / 4.0) * (1.0f + swing);
        panL += (std::cos (angle) * (float) std::sqrt (2.0) - panL) * panCoeff;
        panR += (std::sin (angle) * (float) std::sqrt (2.0) - panR) * panCoeff;
        ppq += beatsPerSample;
        panGainL[(size_t) pos] = panL;
        panGainR[(size_t) pos] = panR;

        // Duck: the input's envelope pulls the whole wet signal down (room, repeats and cloud
        // alike); off, the gain is exactly 1.
        const float peak = std::max (std::abs (dryL), std::abs (dryR));
        duckEnv += (peak - duckEnv) * (peak > duckEnv ? duckAttack : duckRelease);
        if (settings.duckDepthDb > 0.0)
        {
            const float db = 20.0f * std::log10 (std::max (duckEnv, 1.0e-6f));
            const float amount = std::clamp ((db - kDuckStartDb) / (kDuckFullDb - kDuckStartDb), 0.0f, 1.0f);
            duckGain = std::pow (10.0f, -(float) settings.duckDepthDb * amount / 20.0f);
        }
        else
            duckGain = 1.0f;

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
        const float mixAngle = mixNow * (float) (kPi / 2.0);
        const float dryGain = mixNow >= 1.0f ? 0.0f : std::cos (mixAngle);
        const float wetMix = mixNow <= 0.0f ? 0.0f : std::sin (mixAngle);
        outL[i] = dryL * dryGain + wl * wetMix * duckGain;
        outR[i] = dryR * dryGain + wr * wetMix * duckGain;
    }
}

void SpaceEngine::processChunk()
{
    // A new Mode or a preset: fade out, switch once silent, fade in (in the loop at the end).
    const int wantedMode = std::clamp (macroTarget.mode, 0, kNumModes - 1);
    if (wantedMode != activeMode || jumpPending)
    {
        modeFading = true;
        if (modeFade <= 0.0f)
            switchMode (wantedMode);
    }

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
    glide (macroNow.reflect, macroTarget.reflect);
    glide (macroNow.delay, macroTarget.delay);
    glide (macroNow.duck, macroTarget.duck);
    macroNow.delayNote = macroTarget.delayNote; // a fixed choice: its time is crossfaded in the delay
    glide (macroNow.motion, macroTarget.motion);
    glide (macroNow.tone, macroTarget.tone);
    const auto moved = [] (float a, float b) { return std::abs (a - b) > 0.0f; };
    if (moved (before.space, macroNow.space) || moved (before.reflect, macroNow.reflect) || moved (before.motion, macroNow.motion)
        || moved (before.tone, macroNow.tone) || moved (before.delay, macroNow.delay) || moved (before.duck, macroNow.duck)
        || before.delayNote != macroNow.delayNote)
        applySettings (mapMacros (macroNow), false);
    echo.setDelaySeconds (kReflectBeats * 60.0 / bpm);
    setDelayTimes();

    // Clouds granulates the room's last output all the time (also frozen), so its last second
    // of sound is always current.
    if (currentVoicing.grainsEnabled)
        texture.process (roomL.data(), roomR.data(), grainsL.data(), grainsR.data(), chunk);
    else
    {
        std::fill (grainsL.begin(), grainsL.begin() + chunk, 0.0f);
        std::fill (grainsR.begin(), grainsR.begin() + chunk, 0.0f);
    }

    // Into the room: the input and its echoes, muted while frozen. Each side hears a little of
    // the other (Cloud Seed's own input mix).
    // The gains the knobs set glide sample by sample from the last chunk's values, so a knob
    // moving never steps them (steps every 2.7 ms would buzz).
    const Gains target = gainsNow();
    const Gains from = gains;
    gains = target;
    auto glideAt = [&] (float Gains::*g, int i) { return from.*g + (target.*g - from.*g) * (float) (i + 1) / (float) chunk; };

    std::array<float, kMaxChunk> feedL, feedR;
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float send = glideAt (&Gains::send, i);
        feedL[k] = inL[k] * inGain[k];
        feedR[k] = inR[k] * inGain[k];
        echoL[k] = feedL[k] * send;
        echoR[k] = feedR[k] * send;
    }
    echo.process (echoL.data(), echoR.data(), echoL.data(), echoR.data(), chunk);

    // Echo: the same input through the sharp delay (its send glides like the others).
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float send = glideAt (&Gains::delaySend, i);
        delayL[k] = feedL[k] * send;
        delayR[k] = feedR[k] * send;
    }
    delay.process (delayL.data(), delayR.data(), delayL.data(), delayR.data(), chunk);

    const float cross = (float) currentVoicing.crossFeed * 0.5f;
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float l = feedL[k] + echoL[k] * inGain[k] + delayL[k] * kDelayRoomShare * inGain[k];
        const float r = feedR[k] + echoR[k] * inGain[k] + delayR[k] * kDelayRoomShare * inGain[k];
        feedL[k] = l * (1.0f - cross) + r * cross;
        feedR[k] = r * (1.0f - cross) + l * cross;
    }
    // The sky's grains go back into the room beside the input (after the echo send, so the
    // echoes stay echoes of what was played), less of them while the room is above the limit.
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        skyLevel += (0.5f * (roomL[k] * roomL[k] + roomR[k] * roomR[k]) - skyLevel) * skyLevelCoeff;
        const float rms = std::sqrt (skyLevel);
        const float limit = rms > skyLimit ? skyLimit / rms : 1.0f;
        const float feed = glideAt (&Gains::skyFeed, i) * limit * inGain[k];
        feedL[k] += grainsL[k] * feed;
        feedR[k] += grainsR[k] * feed;
    }
    left->Process (feedL.data(), wetL.data(), chunk);
    right->Process (feedR.data(), wetR.data(), chunk);
    std::copy (wetL.begin(), wetL.begin() + chunk, roomL.begin());
    std::copy (wetR.begin(), wetR.begin() + chunk, roomR.begin());

    // The grains and the echoes are also heard straight away beside the room, where it has not
    // yet dissolved them.
    for (int i = 0; i < chunk; ++i)
    {
        const auto k = (size_t) i;
        const float out = glideAt (&Gains::skyOut, i);
        wetL[k] += (grainsL[k] * out + echoL[k] + delayL[k] * kDelayDirect) * inGain[k];
        wetR[k] += (grainsR[k] * out + echoR[k] + delayR[k] * kDelayDirect) * inGain[k];
    }

    // Tone's finish, mono bass, stereo width (Space), auto-pan, level.
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
        // The auto-pan moves only what is above the bass: the mid is split in two with the same
        // filters as the side (a Linkwitz-Riley crossover, so the halves add up without a dip),
        // and the bass half stays in the middle. (Panning before the split instead loses most
        // of the pan to the filters' phase shift.)
        const float midBass = midLowPass[1].process (midLowPass[0].process (mid));
        const float midHigh = midHighPass[1].process (midHighPass[0].process (mid));
        modeFade = modeFading ? std::max (0.0f, modeFade - modeFadeStep) : std::min (1.0f, modeFade + modeFadeStep);
        const float level = glideAt (&Gains::wet, i) * modeFade;
        wetL[k] = (midBass + (midHigh + side) * panGainL[k]) * level;
        wetR[k] = (midBass + (midHigh - side) * panGainR[k]) * level;
        finite = finite && std::isfinite (wetL[k]) && std::isfinite (wetR[k]);
    }

    // Cannot happen with stable loops; if it ever does, silence beats a burst of noise.
    if (! finite)
    {
        left->ClearBuffers();
        right->ClearBuffers();
        echo.reset();
        delay.reset();
        for (auto& f : sideHighPass)
            f.s1 = f.s2 = 0.0;
        for (auto& f : midHighPass)
            f.s1 = f.s2 = 0.0;
        for (auto& f : midLowPass)
            f.s1 = f.s2 = 0.0;
        wetL.fill (0.0f);
        wetR.fill (0.0f);
        roomL.fill (0.0f);
        roomR.fill (0.0f);
        skyLevel = 0.0f;
    }
}
} // namespace spaceforge::dsp

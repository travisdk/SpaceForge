// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace spaceforge::dsp
{
// The settings of Cloud Seed's algorithm that give SpaceForge 2 its character, in real units
// (ms, Hz, dB, seconds). Phase 2 has one fixed voicing, tuned by measurement towards long, soft,
// dark and floating tails; the macros (phase 4) will move some of these values.
struct SpaceVoicing
{
    // Input: what goes into the room. The low cut keeps kick drums out of the tail.
    double crossFeed = 0.2;        // 0..1, how much of each input side reaches the other
    double inputLowCutHz = 110.0;
    double inputHighCutHz = 7000.0;
    double preDelayMs = 12.0;

    // Early diffusion: smears attacks into a soft cloud before the room starts.
    int earlyStages = 6;
    double earlyDelayMs = 40.0;
    double earlyFeedback = 0.7;
    double earlyModMs = 0.6;       // Cloud Seed only modulates above 0.5 ms
    double earlyModHz = 0.3;
    double earlyLevelDb = -6.0;

    // The late room: 12 recirculating delay lines, each with its own diffuser and damping.
    int lineCount = 12;
    double lineSizeMs = 180.0;     // each line is 0.5..1.5 times this
    // Cloud Seed's decay setting. The measured T60 at 1 kHz is about 1.5 times longer (14-15 s),
    // because each line's diffuser makes the loop longer than the line itself.
    double decaySeconds = 9.5;
    double lineModMs = 1.0;
    double lineModHz = 0.25;
    int lateStages = 4;
    double lateDiffuseDelayMs = 50.0;
    double lateDiffuseFeedback = 0.7;
    double lateDiffuseModMs = 0.6;
    double lateDiffuseModHz = 0.4;
    double lateLevelDb = 0.0;

    // Damping inside the feedback loop, applied on every pass: this is what makes the tail
    // darker as it decays (treble dies sooner than the mids, the bass a little sooner too).
    double lowShelfHz = 300.0;
    double lowShelfDb = -0.8;
    double highShelfHz = 2500.0;
    double highShelfDb = -0.8;
    double lowpassHz = 9000.0;

    // 0 = left and right rooms as different as possible (widest), 1 = identical.
    double crossSeed = 0.0;
    int seedTap = 333, seedDiffusion = 185, seedDelay = 218, seedPostDiffusion = 365;

    // Below this the wet signal is mono (both sides the same).
    double monoBassHz = 150.0;
    // Makes the wet signal as loud as the dry for steady material (calibrated with pink noise).
    double wetGainDb = 0.4;
};
} // namespace spaceforge::dsp

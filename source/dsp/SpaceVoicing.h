// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "dsp/ModeTables.h"
#include "dsp/TextureEngine.h"

namespace spaceforge::dsp
{
// The fixed part of SpaceForge 2's sound: everything no knob moves, in real units (ms, Hz, dB).
// Tuned by measurement towards long, soft, dark and floating tails and approved by Henrik by
// ear. What the knobs move is in MacroSettings (dsp/Macros.h).
struct SpaceVoicing
{
    // The sky: Clouds granulates the room's own tail (never the input, so no grain copies a
    // note's attack: Henrik heard that as an echo on his bleeps, phase 4), and part of the
    // grains go back into the room, so the cloud keeps itself alive (how much belongs to Space).
    // Long soft grains from half a second back. Chosen by Henrik by ear (variant C).
    TextureVoicing texture { 0.5, 0.7, 0.8, 0.8, 0.7, 0.4, calibrated::kTextureGainDb[0] };
    // Above this level (RMS of the room's output) less of the grains go back, so the cloud can
    // hold itself but never grow (with Space high it otherwise grew without end; measured).
    double skyLimitDb = -18.0;
    // Measurement only (the offline tools' "without grains" comparisons): no sky. Always on in
    // the plug-in.
    bool grainsEnabled = true;

    // Input: what goes into the room. The low cut keeps kick drums out of the tail.
    double crossFeed = 0.2;        // 0..1, how much of each input side reaches the other
    double inputLowCutHz = 110.0;

    // Early reflections (Cloud Seed's multitap): off unless a Mode uses them.
    bool tapsEnabled = false;
    int tapCount = 1;
    double tapLengthMs = 10.0;
    double tapDecay = 1.0;         // 0..1, how much later taps fade

    // Early diffusion: smears attacks into a soft cloud before the room starts.
    bool earlyEnabled = true;
    int earlyStages = 6;
    double earlyFeedback = 0.7;
    double earlyModMs = 0.6;       // Cloud Seed only modulates above 0.5 ms
    double earlyModHz = 0.3;
    double earlyLevelDb = -6.0;

    // Pre-delay and the early diffusion's spacing.
    double preDelayMs = 12.0;
    double earlyDelayMs = 40.0;

    // The late room: 12 recirculating delay lines, each with its own diffuser and damping.
    int lineCount = 12;
    double lineSizeMs = 180.0;     // each line is 0.5..1.5 times this
    bool lateEnabled = true;
    int lateStages = 4;
    double lateDiffuseDelayMs = 50.0;
    double lateDiffuseFeedback = 0.7;
    double lateLevelDb = 0.0;

    // Damping inside the feedback loop, on every pass: the bass a little shorter than the mids.
    // (The treble damping belongs to Tone.)
    double lowShelfHz = 300.0;
    double lowShelfDb = -0.8;
    double highShelfHz = 2500.0;

    // 0 = left and right rooms as different as possible (widest), 1 = identical.
    double crossSeed = 0.0;
    int seedTap = 333, seedDiffusion = 185, seedDelay = 218, seedPostDiffusion = 365;

    // Below this the wet signal is mono (both sides the same).
    double monoBassHz = 150.0;
    // Makes the wet signal as loud as the dry for steady material at Init (calibrated with pink
    // noise, per Mode; these defaults are Sky's); each knob's own compensation comes on top.
    double wetGainDb = calibrated::kModeGainDb[0];
};
} // namespace spaceforge::dsp

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/Modes.h"
#include "dsp/ModeTables.h"

#include <algorithm>
#include <array>

namespace spaceforge::dsp
{
namespace
{
// The rooms start from Cloud Seed's factory programs (third_party/cloudseed-programs, MIT; in
// real units by tools/modes/cloudseed_v1_programs.py) and CloudSeedCore's Dark Plate. What the
// knobs move (decay, modulation, treble damping) is left to them. All Modes share the seeds:
// changing a seed allocates in Cloud Seed, which a Mode change on the audio thread must not.
// Levels between early and late parts are relative; each Mode's level is calibrated.
// Space's value at 50 % and the levels are calibrated (ModeTables.h, `sf2_render --calibrate-modes`).

// Sky: the sound Henrik chose (phases 2-4): a soft dark room and the granular cloud (variant C).
SpaceVoicing sky()
{
    return {};
}

// Hall, from "Medium Space": a defined room with audible early reflections, only three lines.
SpaceVoicing hall()
{
    SpaceVoicing v;
    v.crossFeed = 0.0;
    v.preDelayMs = 10.0;
    v.tapsEnabled = true;
    v.tapCount = 26;
    v.tapLengthMs = 132.0;
    v.tapDecay = 1.0;
    v.earlyStages = 7;
    v.earlyDelayMs = 61.0;
    v.earlyFeedback = 0.76;
    v.earlyLevelDb = -6.0;
    v.lineCount = 3;
    v.lineSizeMs = 156.0;
    v.lateStages = 5;
    v.lateDiffuseDelayMs = 72.0;
    v.lateDiffuseFeedback = 0.715;
    v.lowShelfHz = 70.0;
    v.lowShelfDb = -2.0;
    return v;
}

// Plate, from CloudSeedCore's "Dark Plate": no early diffusion, twelve short lines, dense and
// quick to build up.
SpaceVoicing plate()
{
    SpaceVoicing v;
    v.crossFeed = 0.23;
    v.preDelayMs = 0.0;
    v.earlyEnabled = false;
    v.earlyLevelDb = -30.0;
    v.lineCount = 12;
    v.lineSizeMs = 96.0;
    v.lateStages = 4;
    v.lateDiffuseDelayMs = 31.6;
    v.lateDiffuseFeedback = 0.85;
    v.lowShelfDb = 0.0;
    return v;
}

// Shimmer, from "Hyperplane": short dense lines, bass kept out; the sky is Clouds' looping
// delay an octave up, so the cloud rises in octaves.
SpaceVoicing shimmer()
{
    SpaceVoicing v;
    v.crossFeed = 0.155;
    v.inputLowCutHz = 280.0;
    v.preDelayMs = 0.0;
    v.tapsEnabled = true;
    v.tapCount = 21;
    v.tapLengthMs = 219.0;
    v.earlyStages = 4;
    v.earlyDelayMs = 34.0;
    v.earlyFeedback = 0.66;
    v.earlyLevelDb = 0.0;
    v.lineCount = 9;
    v.lineSizeMs = 38.0;
    v.lateStages = 8;
    v.lateDiffuseDelayMs = 30.0;
    v.lateDiffuseFeedback = 0.595;
    v.texture = { 0.6, 0.5, 0.5, 0.5, 0.7, 0.2, 0.0 };
    v.texture.playbackMode = 2;
    v.texture.pitchSemitones = 12.0;
    return v;
}

// Smear, from "Through the Looking Glass": twelve very long lines behind eight diffusers; the
// sky is Clouds' time-stretching, a slow tape-like smear.
SpaceVoicing smear()
{
    SpaceVoicing v;
    v.crossFeed = 0.0;
    v.preDelayMs = 0.0;
    v.earlyStages = 8;
    v.earlyDelayMs = 69.0;
    v.earlyFeedback = 0.76;
    v.earlyLevelDb = -30.0;
    v.lineCount = 12;
    v.lineSizeMs = 664.0;
    v.lateStages = 8;
    v.lateDiffuseDelayMs = 74.0;
    v.lateDiffuseFeedback = 0.72;
    v.texture = { 0.5, 0.7, 0.5, 0.5, 0.7, 0.3, 0.0 };
    v.texture.playbackMode = 1;
    return v;
}

// Spectral, from "Rubi-Ka Fields": four lines, lots of diffusion; the sky is Clouds' spectral
// mode, a glassy frozen surface of the room's partials.
SpaceVoicing spectral()
{
    SpaceVoicing v;
    v.crossFeed = 0.325;
    v.preDelayMs = 0.0;
    v.earlyStages = 7;
    v.earlyDelayMs = 61.0;
    v.earlyFeedback = 0.76;
    v.earlyLevelDb = -30.0;
    v.lineCount = 4;
    v.lineSizeMs = 242.0;
    v.lateStages = 6;
    v.lateDiffuseDelayMs = 72.0;
    v.lateDiffuseFeedback = 0.715;
    // Texture at 0.5: Clouds' magnitude quantization (its spectral lo-fi, below 0.48) is off;
    // at 0.3 it cut every quiet partial to nothing (measured: silent below -20 dBFS).
    v.texture = { 0.5, 0.5, 0.6, 0.5, 0.7, 0.3, 0.0 };
    v.texture.playbackMode = 3;
    return v;
}

const std::array<ModeSetup, kNumModes> kModes { {
    // name, description, voicing, decay span, sky feed dB at 0 and 100 %, sky out dB at 0/50/100 %, sky carries
    { "Sky", "Dark room with a granular cloud that keeps itself alive", sky(), 17.8, -40.0, -13.0, { -24.0, -9.0, -6.0 }, true },
    { "Hall", "Clear, large hall with distinct early reflections", hall(), 6.0, -40.0, -12.0, { -30.0, -18.0, -12.0 }, false },
    { "Plate", "Dense, dark plate that builds up quickly", plate(), 5.0, -40.0, -16.0, { -30.0, -20.0, -14.0 }, false },
    { "Shimmer", "Bright octave cloud rising above the tail", shimmer(), 10.0, -40.0, -3.0, { -30.0, -12.0, -8.0 }, false },
    { "Smear", "Slow, tape-like smear of long lines", smear(), 10.0, -30.0, -4.0, { -30.0, -12.0, -8.0 }, true },
    { "Spectral", "Glassy, frozen sound surface", spectral(), 10.0, -40.0, -5.0, { -30.0, -12.0, -8.0 }, true },
} };
} // namespace

const ModeSetup& modeSetup (int mode)
{
    return kModes[(size_t) std::clamp (mode, 0, kNumModes - 1)];
}

SpaceVoicing modeVoicing (int mode)
{
    mode = std::clamp (mode, 0, kNumModes - 1);
    auto v = kModes[(size_t) mode].voicing;
    v.wetGainDb = calibrated::kModeGainDb[(size_t) mode];
    v.texture.gainDb = calibrated::kTextureGainDb[(size_t) mode];
    return v;
}
} // namespace spaceforge::dsp

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <string>

namespace spaceforge::dsp
{
// The four macro knobs, 0..1 each. Fixed roles, fixed direction (right is always "more") and one
// absolute scale: a knob position always means the same sound, whatever the preset.
struct MacroPositions
{
    // Init: the sound Henrik approved in phases 2 and 3.
    static constexpr float kInitSpace = 0.6f, kInitEcho = 0.0f, kInitMotion = 0.5f, kInitTone = 0.5f;
    float space = kInitSpace, echo = kInitEcho, motion = kInitMotion, tone = kInitTone;
};

// Every internal value the macros move. Each is owned by exactly one knob (the prefix says which).
struct MacroSettings
{
    // Space: tight and short -> big and endless. (Not the lengths of the room's delay lines:
    // changing those while sound is in them clicks, so they stay fixed.)
    double spaceDecaySeconds = 9.5;     // Cloud Seed's decay setting (measured T60 is longer)
    double spaceWidth = 1.0;            // wet stereo width, 0..1
    double spaceGainDb = 0.0;           // level compensation

    // Echo: none -> many repeats (tempo-synced 1/8 dotted ping-pong)
    double echoSend = 0.0;              // 0..1 into the echo
    double echoFeedback = 0.0;
    double echoGainDb = 0.0;

    // Motion: still -> alive
    double motionTextureAmount = 1.0;   // grains instead of input into the room (equal-power)
    double motionGrainsDirectDb = -3.0; // grains straight out beside the room
    double motionDensity = 0.75;
    double motionFeedback = 0.25;
    double motionTextureShape = 0.7;
    double motionTextureGainDb = 5.1;   // makes the grains as loud as the input (calibrated)
    double motionLineModMs = 1.0;
    double motionLineModHz = 0.25;
    double motionDiffuseModMs = 0.6;
    double motionDiffuseModHz = 0.4;
    double motionPanDepth = 0.0;        // auto-pan, 0..1
    int motionPanBars = 8;              // auto-pan cycle in bars (0 = half a bar)
    double motionGainDb = 0.0;

    // Tone: dark -> light (level-compensated)
    double toneLowpassHz = 9000.0;      // damping inside the room's loops
    double toneHighShelfDb = -0.8;      // ditto, above 2.5 kHz
    double toneInputHighCutHz = 7000.0;
    double toneEchoDampingHz = 4500.0;  // damping inside the echo's loop
    double toneTiltDb = 0.0;            // treble after the room (the "finish"), above ~2 kHz
    double toneGainDb = 0.0;
};

MacroSettings mapMacros (const MacroPositions& p);

// The knob's value in words and real units, for the UI and the DAW (the same text in both).
std::string describeSpace (float position);
std::string describeEcho (float position);
std::string describeMotion (float position);
std::string describeTone (float position);

// Echo timing: 1/8 dotted, the number of audible repeats (until 24 dB down).
inline constexpr double kEchoBeats = 0.75;
int echoRepeats (double feedback);

// Measured T60 at 1 kHz for Space at 0, 0.1, ..., 1.0 (with the other knobs at Init), so the
// Space text shows what the room really does. Filled in by `sf2_render --calibrate`.
extern const std::array<double, 11> kSpaceMeasuredT60;
} // namespace spaceforge::dsp

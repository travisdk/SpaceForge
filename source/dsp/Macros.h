// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <string>

namespace spaceforge::dsp
{
// The four macro knobs, 0..1 each, and the Mode (dsp/Modes.h). Fixed roles, fixed direction
// (right is always "more") in every Mode, and within a Mode one absolute scale: a knob position
// always means the same sound, whatever the preset. Only the Mode sets Space's range.
// (Reflect was called Echo before the real tempo-synced Echo came; its parameter id and its
// preset key are still "echo".)
//
// Beside them two small controls, both 0 = off (then the sound is bit for bit the same as
// without them): Echo (`delay`: sharp tempo-synced repeats, at the fixed note `delayNote`) and
// Duck (the wet signal gives way while something is played).
struct MacroPositions
{
    // Init: the sound Henrik approved (Sky, variant C; Space where its tail is as long as then).
    static constexpr float kInitSpace = 0.95f, kInitReflect = 0.0f, kInitMotion = 0.5f, kInitTone = 0.5f;
    static constexpr float kInitDelay = 0.0f, kInitDuck = 0.0f;
    static constexpr int kInitDelayNote = 2; // 1/8d, see kDelayNotes
    float space = kInitSpace, reflect = kInitReflect, motion = kInitMotion, tone = kInitTone;
    int mode = 0;
    float delay = kInitDelay, duck = kInitDuck;
    int delayNote = kInitDelayNote;
};

// Every internal value the macros move. Each is owned by exactly one knob (the prefix says which).
struct MacroSettings
{
    // Space: tight and short -> big and endless. (Not the lengths of the room's delay lines:
    // changing those while sound is in them clicks, so they stay fixed.)
    double spaceDecaySeconds = 9.5;     // Cloud Seed's decay setting (measured T60 is longer)
    double spaceWidth = 1.0;            // wet stereo width, 0..1
    double spaceSkyFeedDb = -6.0;       // the room's grains back into it (the endless cloud)
    double spaceSkyOutDb = -6.0;        // the same grains heard beside the room
    double spaceGainDb = 0.0;           // level compensation

    // Reflect: none -> many repeats (tempo-synced 1/8 dotted ping-pong, melting into the room)
    double reflectSend = 0.0;              // 0..1 into the echo
    double reflectFeedback = 0.0;
    double reflectGainDb = 0.0;

    // Motion: still -> alive
    double motionLineModMs = 1.0;
    double motionLineModHz = 0.25;
    double motionDiffuseModMs = 0.6;
    double motionDiffuseModHz = 0.4;
    double motionPanDepth = 0.0;        // auto-pan, 0..1
    int motionPanBars = 8;              // auto-pan cycle in bars (0 = half a bar)
    double motionGainDb = 0.0;

    // Echo (small control): none -> many sharp repeats at a fixed note, stereo
    double delaySend = 0.0;             // 0..1: level of the repeats (into the room and beside it)
    double delayFeedback = 0.0;
    double delayBeats = 0.75;           // the left side's time; the right side's is 4/3 of it
    double delayGainDb = 0.0;           // level compensation (the repeats add energy)

    // Duck (small control): the wet signal is pulled down while something is played
    double duckDepthDb = 0.0;

    // Tone: dark -> light (level-compensated)
    double toneLowpassHz = 9000.0;      // damping inside the room's loops
    double toneHighShelfDb = -0.8;      // ditto, above 2.5 kHz
    double toneInputHighCutHz = 7000.0;
    double toneEchoDampingHz = 4500.0;  // damping inside the echo's loop
    double toneTiltDb = 0.0;            // treble after the room (the "finish"), above ~2 kHz
    double toneGainDb = 0.0;
};

MacroSettings mapMacros (const MacroPositions& p);

// Offline calibration only (`sf2_render --calibrate-modes`), never the plug-in: replaces one
// Mode's calibrated Space values at 50 % (decay setting in s, sky feed in dB). mode < 0 clears.
void setSpaceCalibrationOverride (int mode, double decayHalfSeconds, double skyFeedHalfDb);

// The knob's value in words and real units, for the UI and the DAW (the same text in both).
std::string describeSpace (float position, int mode = 0);
std::string describeReflect (float position);
std::string describeMotion (float position);
std::string describeTone (float position);
std::string describeDelay (float position, int note);
std::string describeDuck (float position);

// Echo's notes (the fixed choice that goes with the knob, saved in the preset): name and length
// in beats.
inline constexpr int kNumDelayNotes = 5;
const char* delayNoteName (int note);
double delayNoteBeats (int note);
int delayRepeats (double feedback);

// Echo timing: 1/8 dotted, the number of audible repeats (until 24 dB down).
inline constexpr double kReflectBeats = 0.75;
int reflectRepeats (double feedback);

} // namespace spaceforge::dsp

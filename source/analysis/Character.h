// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <string>
#include <vector>
#include "analysis/Measure.h"
#include "dsp/SpaceEngine.h"

namespace spaceforge::analysis
{
// The character of the room in numbers: everything the render report shows and the tests check.
struct Character
{
    double t60 = 0;                                   // s, broadband
    std::array<double, kOctaveBands.size()> t60Band {}; // s, per octave
    double darkness4k = 0, darkness8k = 0, bassRatio = 0; // T60 at 4k, 8k, 125 Hz divided by T60 at 1k
    double centroidDry = 0, centroidWet = 0;          // Hz, noise burst and its tail
    double corrBass = 0, corrHigh = 0;                // L/R correlation below and above 150 Hz
    double monoLossDb = 0;
    double densityMs = 0;
    double ringingDb = 0, flutterDb = 0;
    NoiseBaseline baseline;
    double wetMinusDryDb = 0;                         // steady pink noise, wet only against dry
    double mixMinusDryDb = 0;                         // the same at the default mix
    double freezeDriftDb = 0;                         // level change over 10 s frozen
    double freezeLeakDb = 0;                          // new input while frozen, against the frozen sound
    double lineModCents = 0, diffuseModCents = 0;
    double textureLevelDb = 0;                        // wet level with grains against without
    double textureChangeDb = 0;                       // how much the grains change the synth figure in the room
    double randomChangeDb = 0;                        // the same between two rooms with other random detail
    int textureUnderruns = 0;
};

Character measureCharacter (dsp::SpaceEngine& engine, double sampleRate, float defaultMix);

// Starting targets from CLAUDE.md ("Lydmål og måling"), adjusted after Henrik's feedback.
struct Target
{
    std::string what;  // plain Danish
    double value;
    double lo, hi;
    std::string unit;
};
std::vector<Target> targetsFor (const Character& c);
} // namespace spaceforge::analysis

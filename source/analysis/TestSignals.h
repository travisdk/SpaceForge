// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace spaceforge::analysis
{
struct Stereo
{
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
};

// Dry test sounds, generated in code and always the same (fixed random seeds), so every render
// and every measurement can be compared with the previous one. 125 BPM where tempo matters.
struct TestSignal
{
    std::string name;        // file name part, e.g. "pad"
    std::string description; // in plain Danish, for the render report
    Stereo audio;            // the sound itself, followed by silence for the tail
};

constexpr double kTestBpm = 125.0;

// tailSeconds of silence follow each sound.
TestSignal makeImpulse (double sampleRate, double tailSeconds);
TestSignal makeClick (double sampleRate, double tailSeconds);
TestSignal makeBleeps (double sampleRate, double tailSeconds);
TestSignal makeNoiseBurst (double sampleRate, double tailSeconds);
TestSignal makePad (double sampleRate, double tailSeconds);
TestSignal makeSynthFigure (double sampleRate, double tailSeconds);
TestSignal makeHiHats (double sampleRate, double tailSeconds);
// Steady pink noise for level measurements (no tail).
Stereo makePinkNoise (double sampleRate, double seconds, float rms, unsigned seed);

std::vector<TestSignal> allTestSignals (double sampleRate, double tailSeconds);
} // namespace spaceforge::analysis

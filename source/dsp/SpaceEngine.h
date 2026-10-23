// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include "dsp/SpaceVoicing.h"

namespace Cloudseed
{
class ReverbChannel;
}

namespace spaceforge::dsp
{
// The room: Cloud Seed's two reverb channels plus everything around them that a plug-in needs.
// No JUCE, so the offline render tool and the tests use exactly what the plug-in runs.
//
// - Cloud Seed's feedback loops are delayed by one processing block, so its sound would change
//   with the host's buffer size. Here it always runs in fixed chunks of ~2.7 ms; the wet signal
//   is that much late (it acts as a little extra pre-delay), the dry signal is not delayed.
// - Freeze stops new input entering the room and holds what is in it.
// - The wet signal is mono below SpaceVoicing::monoBassHz.
//
// prepare() and setVoicing() may allocate; process(), setMix() and setFreeze() never do.
class SpaceEngine
{
public:
    SpaceEngine();  // allocates the delay lines (about 70 MB)
    ~SpaceEngine();

    void prepare (double sampleRate);
    void setVoicing (const SpaceVoicing& voicing);
    const SpaceVoicing& voicing() const { return currentVoicing; }

    // Empties the room and puts every smoothed value at its target.
    void reset();

    void setMix (float mix01) { mixTarget = mix01; }
    void setFreeze (bool on) { freezeTarget = on; }

    // Any block size; outL/outR may be the same buffers as inL/inR.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

    // How late the wet signal is, in samples (the fixed chunk).
    int wetLatency() const { return chunk; }

    static constexpr double kChunkSeconds = 128.0 / 48000.0;
    // Decay while frozen: practically endless (0.0006 dB per second).
    static constexpr double kFrozenDecaySeconds = 1.0e5;
    static constexpr int kMaxChunk = 1024;

private:
    void applyVoicing();
    void applyFreeze (bool on);
    void processChunk();

    std::unique_ptr<Cloudseed::ReverbChannel> left, right;
    SpaceVoicing currentVoicing;
    double sampleRate = 48000.0;
    int chunk = 128;
    int pos = 0;

    std::array<float, kMaxChunk> inL {}, inR {}, wetL {}, wetR {};

    // High-pass on the wet side signal, two second-order stages (makes the bass mono).
    struct HighPass
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
        void setup (double hz, double rate);
        float process (float x);
    } sideHighPass[2];

    float mixTarget = 0.4f, mixNow = 0.4f, mixCoeff = 0.0f;
    bool freezeTarget = false, frozen = false;
    float inputGain = 1.0f, inputGainStep = 0.0f;
    float wetGain = 1.0f;
};
} // namespace spaceforge::dsp

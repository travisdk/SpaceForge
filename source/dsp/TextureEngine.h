// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include <vector>
#include "dsp/Resampler.h"

namespace clouds
{
class GranularProcessor;
}

namespace spaceforge::dsp
{
// The settings of Clouds' granular mode (all 0..1, Clouds' own scales).
struct TextureVoicing
{
    // Grains replay what was just played. Taken from further back (position) or longer (size),
    // every grain of a short note replays the same moment some 0.2-0.4 s later: an audible echo
    // (heard with Echo at 0, phase 4). Taken from now and about 45 ms long, the
    // copies fall within ~50 ms of the note and fuse with it.
    double position = 0.0;      // how far back in the last second the grains are taken from
    double size = 0.13;         // grain length (Clouds' scale: 0 = 32 ms, 0.13 = 45 ms, 1 = 512 ms)
    double density = 0.75;      // above 0.5: grains at random times, more the higher
    double texture = 0.7;       // grain window shape (soft); above 0.75 adds diffusion
    double stereoSpread = 0.7;
    double feedback = 0.25;
    double gainDb = 5.1;        // makes the grains as loud as what goes in (calibrated with pink noise)
    // Clouds' four modes: 0 granular, 1 stretch (time-stretching), 2 looping delay (with pitch
    // shifting), 3 spectral. The knobs' meanings change with the mode (Clouds' manual).
    int playbackMode = 0;
    double pitchSemitones = 0.0;
};

// Clouds (granular texture) at any host sample rate. Clouds is written for exactly 32 kHz in
// blocks of 32 frames, so the host signal is converted to 32 kHz, processed, and converted back.
// The output (grains only, no dry signal) is late by latency() host samples.
//
// prepare() and reset() may allocate or clear large memory; process() never allocates.
class TextureEngine
{
public:
    TextureEngine();
    ~TextureEngine();

    void prepare (double hostRate);
    void reset();
    void setVoicing (const TextureVoicing& voicing);

    // n host samples in, n out.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    int latency() const { return prefill; }
    // Times the output had to be filled with silence because Clouds had not delivered yet.
    // Always 0 when the prefill is right; the tests check it.
    int underruns() const { return underrunCount; }

    static constexpr double kCloudsRate = 32000.0;

private:
    void applyVoicing();
    void runCloudsBlock();

    struct ProcessorDeleter
    {
        void operator() (clouds::GranularProcessor*) const;
    };
    std::unique_ptr<clouds::GranularProcessor, ProcessorDeleter> processor;
    std::vector<uint8_t> largeBuffer, smallBuffer;
    TextureVoicing currentVoicing;
    float outputGain = 1.0f, appliedGain = 1.0f;

    Resampler down, up;
    static constexpr int kBlock = 32; // Clouds' block size
    std::array<float, kBlock> blockL {}, blockR {};
    int blockFill = 0;

    // Host-rate output waiting to be read.
    std::vector<float> fifoL, fifoR;
    size_t fifoMask = 0, readPos = 0, writePos = 0;
    int prefill = 0;
    int underrunCount = 0;
};
} // namespace spaceforge::dsp

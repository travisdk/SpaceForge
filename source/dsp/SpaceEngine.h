// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include "dsp/EchoEngine.h"
#include "dsp/Macros.h"
#include "dsp/SpaceVoicing.h"
#include "dsp/TextureEngine.h"

namespace Cloudseed
{
class ReverbChannel;
}

namespace spaceforge::dsp
{
// The sound engine: granular texture (Clouds) and tempo-synced echoes into the room (Cloud
// Seed's two reverb channels), plus everything around them that a plug-in needs. No JUCE, so the
// offline render tool and the tests use exactly what the plug-in runs.
//
// - Cloud Seed's feedback loops are delayed by one processing block, so its sound would change
//   with the host's buffer size. Here it always runs in fixed chunks of ~2.7 ms; the wet signal
//   is that much late (it acts as a little extra pre-delay), the dry signal is not delayed.
// - The four macros move the sound; their positions are smoothed (no clicks from a jump).
// - Freeze stops new input entering the room and holds what is in it.
// - The wet signal is mono below SpaceVoicing::monoBassHz.
//
// prepare() and setVoicing() may allocate; process() and the setters never do.
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
    void setMacros (const MacroPositions& positions) { macroTarget = positions; }
    const MacroPositions& macros() const { return macroTarget; }

    // Host tempo for the echo and the auto-pan. Without a running transport the pan runs freely
    // at the last known tempo (120 BPM if none), so it can always be heard.
    void setTempo (double bpm, bool playing, double ppqPosition);

    // Any block size; outL/outR may be the same buffers as inL/inR.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

    // How late the wet signal is, in samples (the fixed chunk).
    int wetLatency() const { return chunk; }
    // See TextureEngine::underruns(); always 0.
    int textureUnderruns() const { return texture.underruns(); }

    static constexpr double kChunkSeconds = 128.0 / 48000.0;
    // Decay while frozen: practically endless (0.0006 dB per second).
    static constexpr double kFrozenDecaySeconds = 1.0e5;
    static constexpr int kMaxChunk = 1024;

private:
    void applyVoicing();
    void applySettings (const MacroSettings& s, bool force);
    void applyFreeze (bool on);
    void processChunk();

    std::unique_ptr<Cloudseed::ReverbChannel> left, right;
    SpaceVoicing currentVoicing;
    double sampleRate = 48000.0;
    int chunk = 128;
    int pos = 0;

    TextureEngine texture;
    EchoEngine echo;
    // Per chunk: the dry input, its gain into the room (Freeze), the auto-pan, the grains, the
    // echoes, the room's output.
    std::array<float, kMaxChunk> inL {}, inR {}, inGain {}, panGainL {}, panGainR {}, grainsL {}, grainsR {}, echoL {}, echoR {}, wetL {}, wetR {};

    // High-pass on the wet side signal (makes the bass mono), and the mid split into bass and the
    // rest (the auto-pan leaves the bass in the middle); two second-order stages each.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
        void setup (double hz, double rate, bool highPass = true);
        float process (float x);
    } sideHighPass[2], midHighPass[2], midLowPass[2];

    MacroPositions macroTarget, macroNow;
    MacroSettings settings;   // what is applied now
    float macroCoeff = 0.0f;  // per chunk

    float mixTarget = 0.4f, mixNow = 0.4f, mixCoeff = 0.0f;
    bool freezeTarget = false, frozen = false;
    float inputGain = 1.0f, inputGainStep = 0.0f;
    float wetGain = 1.0f;     // voicing calibration and the knobs' level compensation together
    // The gains applied in the last chunk (each chunk glides from these to the new ones).
    struct Gains
    {
        float send = 0.0f, skyFeed = 0.0f, skyOut = 0.0f, wet = 1.0f;
    } gains;
    Gains gainsNow() const;
    // The sky (see SpaceVoicing): the room's last output, which the grains take from, and its
    // level (mean square, smoothed) against the limit.
    std::array<float, kMaxChunk> roomL {}, roomR {};
    float skyLevel = 0.0f, skyLevelCoeff = 0.0f, skyLimit = 0.0f;

    // Tone's finish (treble shelf after the room).
    float tiltCoeff = 0.0f, tiltGain = 0.0f, tiltLowL = 0.0f, tiltLowR = 0.0f;

    // Auto-pan (Motion): position in quarter notes, advanced per sample.
    double bpm = 120.0, ppq = 0.0;
    float panL = 1.0f, panR = 1.0f, panCoeff = 0.0f;
};
} // namespace spaceforge::dsp

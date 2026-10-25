// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <vector>

namespace spaceforge::dsp
{
// Echo: a true tempo-synced stereo delay with sharp repeats that are easy to count. The left and
// the right side each have a delay line and a feedback loop of their own (no ping-pong, no
// diffusion), with different times, so the repeats alternate left, right, left, ... at a
// steady beat. Every pass through the loop loses bass (a high-pass at 150 Hz) and the top (a
// one-pole low-pass), so the repeats never build up into mud; the loop gain stays below 1.
//
// A new time (a new tempo) is never glided, which would bend the pitch: the old and the new
// time are crossfaded over 50 ms.
//
// prepare() allocates; everything else never does.
class StereoDelay
{
public:
    void prepare (double sampleRate);
    void reset();

    void setDelaySeconds (double left, double right);
    void setFeedback (double amount) { targetFeedback = (float) amount; }
    void setDampingHz (double hz);

    // Stereo in, stereo repeats out (no dry signal). In-place is allowed.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    static constexpr double kMaxDelaySeconds = 4.0;

private:
    struct Side
    {
        std::vector<float> line;
        double now = 0.0, old = 0.0, target = 0.0;
        float fade = 1.0f;
        float low = 0.0f, bass = 0.0f;
    };
    float read (const Side&, double delay) const;
    float step (Side&, float input, float feedback);

    double rate = 48000.0;
    Side sides[2];
    size_t mask = 0, write = 0;
    float fadeStep = 0.0f;
    float feedback = 0.0f, targetFeedback = 0.0f, smoothing = 0.0f;
    float lowpassCoeff = 0.5f, highpassCoeff = 0.0f;
};
} // namespace spaceforge::dsp

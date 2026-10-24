// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <vector>

namespace spaceforge::dsp
{
// Tempo-synced ping-pong echo: the first repeat on the left, the next on the right, and so on,
// each one feedback times quieter and a little darker (one-pole low-pass in the loop) with the
// bass taken out (no mud building up). Cloud Seed's own delay lines only make randomly spaced
// reflections, not rhythmic repeats, so this small delay is SpaceForge 2's own.
//
// When the echo time changes (a new tempo) it is never glided (that would bend the pitch): the
// old and the new time are crossfaded over 50 ms.
//
// prepare() allocates; everything else never does.
class EchoEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    void setDelaySeconds (double seconds);
    void setFeedback (double amount) { targetFeedback = (float) amount; }
    void setDampingHz (double hz);

    // Mono-summed input in, stereo echoes out (no dry signal). In-place is allowed.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    static constexpr double kMaxDelaySeconds = 2.0;

private:
    float read (const std::vector<float>& line, double delay) const;

    double rate = 48000.0;
    std::vector<float> lineL, lineR;
    size_t mask = 0, write = 0;

    double delayNow = 0.375 * 48000.0, delayOld = 0.375 * 48000.0, delayTarget = 0.375 * 48000.0;
    float fade = 1.0f, fadeStep = 0.0f; // 0 = old time, 1 = new time
    float feedback = 0.0f, targetFeedback = 0.0f, smoothing = 0.0f;
    float lowpassCoeff = 0.5f, lowL = 0.0f, lowR = 0.0f;
    float highpassCoeff = 0.0f, bassL = 0.0f, bassR = 0.0f;
};
} // namespace spaceforge::dsp

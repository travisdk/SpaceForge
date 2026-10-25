// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/StereoDelay.h"

#include <algorithm>
#include <cmath>

namespace spaceforge::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kCrossfadeSeconds = 0.05;
constexpr double kBassCutHz = 150.0;
constexpr double kFeedbackSmoothingSeconds = 0.05;
// Never 1: the loop always loses something, so it can never build up.
constexpr float kMaxFeedback = 0.9f;
} // namespace

void StereoDelay::prepare (double sampleRate)
{
    rate = sampleRate;
    size_t size = 1;
    while ((double) size < kMaxDelaySeconds * rate + 4.0)
        size <<= 1;
    for (auto& side : sides)
        side.line.assign (size, 0.0f);
    mask = size - 1;
    fadeStep = (float) (1.0 / (kCrossfadeSeconds * rate));
    smoothing = (float) (1.0 - std::exp (-1.0 / (kFeedbackSmoothingSeconds * rate)));
    highpassCoeff = (float) std::exp (-2.0 * kPi * kBassCutHz / rate);
    reset();
}

void StereoDelay::reset()
{
    for (auto& side : sides)
    {
        std::fill (side.line.begin(), side.line.end(), 0.0f);
        side.old = side.now = side.target;
        side.fade = 1.0f;
        side.low = side.bass = 0.0f;
    }
    write = 0;
    feedback = targetFeedback;
}

void StereoDelay::setDelaySeconds (double left, double right)
{
    const double seconds[2] = { left, right };
    for (int s = 0; s < 2; ++s)
    {
        const double samples = std::clamp (seconds[s], 0.01, kMaxDelaySeconds) * rate;
        if (std::abs (samples - sides[s].target) >= 0.5)
            sides[s].target = samples;
    }
}

void StereoDelay::setDampingHz (double hz)
{
    lowpassCoeff = (float) (1.0 - std::exp (-2.0 * kPi * std::clamp (hz, 200.0, 0.45 * rate) / rate));
}

float StereoDelay::read (const Side& side, double delay) const
{
    const double pos = (double) write - delay;
    const auto base = (int64_t) std::floor (pos);
    const auto frac = (float) (pos - (double) base);
    const float a = side.line[(size_t) base & mask], b = side.line[(size_t) (base + 1) & mask];
    return a + (b - a) * frac;
}

float StereoDelay::step (Side& side, float input, float feedbackNow)
{
    // A new time starts a crossfade from the current one (only once the last has finished).
    if (side.fade >= 1.0f && std::abs (side.target - side.now) >= 0.5)
    {
        side.old = side.now;
        side.now = side.target;
        side.fade = 0.0f;
    }
    float echo = read (side, side.now);
    if (side.fade < 1.0f)
    {
        const float g = 0.5f - 0.5f * std::cos (side.fade * (float) kPi);
        echo = read (side, side.old) * (1.0f - g) + echo * g;
        side.fade = std::min (1.0f, side.fade + fadeStep);
    }
    // Less bass and top on every pass.
    side.low += (echo - side.low) * lowpassCoeff;
    side.bass = side.low + (side.bass - side.low) * highpassCoeff;
    side.line[write & mask] = input + (side.low - side.bass) * feedbackNow;
    return echo;
}

void StereoDelay::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        feedback += (std::clamp (targetFeedback, 0.0f, kMaxFeedback) - feedback) * smoothing;
        const float l = step (sides[0], inL[i], feedback);
        const float r = step (sides[1], inR[i], feedback);
        ++write;
        outL[i] = l;
        outR[i] = r;
    }
}
} // namespace spaceforge::dsp

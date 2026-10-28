// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/EchoEngine.h"

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
constexpr float kMaxFeedback = 0.95f;
} // namespace

void EchoEngine::prepare (double sampleRate)
{
    rate = sampleRate;
    size_t size = 1;
    while ((double) size < kMaxDelaySeconds * rate + 4.0)
        size <<= 1;
    lineL.assign (size, 0.0f);
    lineR.assign (size, 0.0f);
    mask = size - 1;
    fadeStep = (float) (1.0 / (kCrossfadeSeconds * rate));
    smoothing = (float) (1.0 - std::exp (-1.0 / (kFeedbackSmoothingSeconds * rate)));
    highpassCoeff = (float) std::exp (-2.0 * kPi * kBassCutHz / rate);
    reset();
}

void EchoEngine::reset()
{
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    write = 0;
    delayOld = delayNow = delayTarget;
    fade = 1.0f;
    feedback = targetFeedback;
    lowL = lowR = bassL = bassR = 0.0f;
}

void EchoEngine::setDelaySeconds (double seconds)
{
    const double samples = std::clamp (seconds, 0.01, kMaxDelaySeconds) * rate;
    if (std::abs (samples - delayTarget) < 0.5)
        return;
    delayTarget = samples;
}

void EchoEngine::setDampingHz (double hz)
{
    lowpassCoeff = (float) (1.0 - std::exp (-2.0 * kPi * std::clamp (hz, 200.0, 0.45 * rate) / rate));
}

float EchoEngine::read (const std::vector<float>& line, double delay) const
{
    const double pos = (double) write - delay;
    const auto base = (int64_t) std::floor (pos);
    const auto frac = (float) (pos - (double) base);
    const float a = line[(size_t) base & mask], b = line[(size_t) (base + 1) & mask];
    return a + (b - a) * frac;
}

void EchoEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        // A new time starts a crossfade from the current one (only once the last has finished).
        if (fade >= 1.0f && std::abs (delayTarget - delayNow) >= 0.5)
        {
            delayOld = delayNow;
            delayNow = delayTarget;
            fade = 0.0f;
        }
        feedback += (std::clamp (targetFeedback, 0.0f, kMaxFeedback) - feedback) * smoothing;

        float echoL = read (lineL, delayNow), echoR = read (lineR, delayNow);
        if (fade < 1.0f)
        {
            const float g = 0.5f - 0.5f * std::cos (fade * (float) kPi);
            echoL = read (lineL, delayOld) * (1.0f - g) + echoL * g;
            echoR = read (lineR, delayOld) * (1.0f - g) + echoR * g;
            fade = std::min (1.0f, fade + fadeStep);
        }

        // Ping-pong: the input goes into the left line, the left line's output into the right,
        // the right one's back into the left. Darker and without bass on every pass.
        const float input = 0.5f * (inL[i] + inR[i]);
        lowL += (echoL - lowL) * lowpassCoeff;
        lowR += (echoR - lowR) * lowpassCoeff;
        bassL = lowL + (bassL - lowL) * highpassCoeff;
        bassR = lowR + (bassR - lowR) * highpassCoeff;
        const float darkL = lowL - bassL, darkR = lowR - bassR;

        lineL[write & mask] = input + darkR * feedback;
        lineR[write & mask] = darkL * feedback;
        ++write;

        outL[i] = echoL;
        outR[i] = echoR;
    }
}
} // namespace spaceforge::dsp

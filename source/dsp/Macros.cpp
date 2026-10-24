// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/Macros.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace spaceforge::dsp
{
namespace
{
// Level compensation per knob at 0, 0.1, ..., 1.0 (dB, the other knobs at Init), measured with
// steady pink noise so no knob moves the overall level. Filled in by `sf2_render --calibrate`.
constexpr std::array<double, 11> kSpaceGainDb { 6.25, 5.90, 5.31, 4.35, 3.01, 1.39, -0.03, -1.44, -2.69, -3.66, -4.34 };
constexpr std::array<double, 11> kEchoGainDb { 0.03, -0.49, -1.74, -1.78, -1.84, -1.92, -2.02, -2.16, -2.34, -2.60, -3.00 };
constexpr std::array<double, 11> kMotionGainDb { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
constexpr std::array<double, 11> kToneGainDb { 2.09, 1.72, 1.36, 0.98, 0.53, -0.03, -0.38, -0.76, -1.20, -1.70, -2.25 };
// Clouds' output level depends on how many grains overlap: makes the grains as loud as the input.
constexpr std::array<double, 11> kMotionTextureGainDb { 23.2, 16.6, 9.3, 5.5, 3.4, 1.5, 0.8, 0.1, 0.1, 0.2, 0.1 };

double clamp01 (double x) { return std::isfinite (x) ? std::clamp (x, 0.0, 1.0) : 0.0; }

double table (const std::array<double, 11>& t, double x)
{
    const double pos = clamp01 (x) * 10.0;
    const auto i = std::min ((size_t) pos, (size_t) 9);
    return t[i] + (t[i + 1] - t[i]) * (pos - (double) i);
}

// Straight lines through (0, a), (mid, b), (1, c).
double through (double x, double a, double mid, double b, double c)
{
    x = clamp01 (x);
    return x <= mid ? a + (b - a) * x / mid : b + (c - b) * (x - mid) / (1.0 - mid);
}

// The same, but even steps in ratio (for frequencies and times).
double geometric (double x, double a, double mid, double b, double c)
{
    return std::exp (through (x, std::log (a), mid, std::log (b), std::log (c)));
}

std::string dots (double x, const char* left, const char* right)
{
    const int filled = (int) std::lround (clamp01 (x) * 4.0) + 1;
    std::string s = std::string (left) + " ";
    for (int i = 0; i < 5; ++i)
        s += i < filled ? "●" : "○";
    return s + " " + right;
}
} // namespace

const std::array<double, 11> kSpaceMeasuredT60 { 1.03, 1.23, 1.94, 3.21, 5.41, 9.04, 14.68, 23.20, 34.72, 49.34, 66.40 };

int echoRepeats (double feedback)
{
    if (feedback <= 0.0)
        return 1;
    return 1 + (int) std::floor (24.0 / (-20.0 * std::log10 (std::min (feedback, 0.999))));
}

MacroSettings mapMacros (const MacroPositions& p)
{
    MacroSettings s;

    const double space = clamp01 (p.space);
    s.spaceDecaySeconds = 0.3 * std::pow (317.0, space); // setting 0.3..95 s (measured T60 1.6..70 s), 9.5 s at Init
    s.spaceWidth = through (space, 0.6, 0.5, 1.0, 1.0);
    s.spaceGainDb = table (kSpaceGainDb, space);

    const double echo = clamp01 (p.echo);
    s.echoSend = std::min (1.0, echo / 0.2);
    s.echoFeedback = echo > 0.0 ? 0.15 + 0.7 * echo : 0.0;
    s.echoGainDb = table (kEchoGainDb, echo);

    const double motion = clamp01 (p.motion);
    // No grains (phase 4): heard directly, every grain of a short note is an audible copy of it
    // (Henrik heard it as an echo on his own bleeps); inside the room only, they made no audible
    // difference (measured). Clouds stays in the code but out of the signal path.
    s.motionTextureAmount = 0.0;
    s.motionGrainsDirectDb = -100.0;
    s.motionDensity = through (motion, 0.6, 0.5, 0.85, 0.92);
    s.motionFeedback = through (motion, 0.05, 0.5, 0.15, 0.08); // more makes the grains repeat audibly
    s.motionTextureShape = through (motion, 0.6, 0.5, 0.7, 0.82);
    s.motionTextureGainDb = table (kMotionTextureGainDb, motion);
    s.motionLineModMs = through (motion, 0.3, 0.5, 1.0, 1.8);
    s.motionLineModHz = through (motion, 0.12, 0.5, 0.25, 0.4);
    s.motionDiffuseModMs = through (motion, 0.2, 0.5, 0.6, 1.0);
    s.motionDiffuseModHz = through (motion, 0.2, 0.5, 0.4, 0.6);
    s.motionPanDepth = through (motion, 0.0, 0.5, 0.0, 0.7);
    // Faster as Motion rises: 8, 4, 2, 1 and half a bar over the five steps above 50 %.
    constexpr int bars[] = { 8, 8, 4, 2, 1, 0 };
    s.motionPanBars = bars[std::clamp ((int) std::ceil ((motion - 0.5) * 10.0 - 1.0e-3), 0, 5)];
    s.motionGainDb = table (kMotionGainDb, motion);

    const double tone = clamp01 (p.tone);
    s.toneLowpassHz = geometric (tone, 3500.0, 0.5, 9000.0, 18000.0);
    s.toneHighShelfDb = through (tone, -6.0, 0.5, -0.8, 0.0);
    s.toneInputHighCutHz = geometric (tone, 2500.0, 0.5, 7000.0, 18000.0);
    s.toneEchoDampingHz = geometric (tone, 1200.0, 0.5, 4500.0, 14000.0);
    s.toneTiltDb = through (tone, -8.0, 0.5, 0.0, 5.0);
    s.toneGainDb = table (kToneGainDb, tone);
    return s;
}

std::string describeSpace (float position)
{
    const double t = table (kSpaceMeasuredT60, position);
    char text[32];
    if (t < 10.0)
        std::snprintf (text, sizeof (text), "%.1f s", t);
    else
        std::snprintf (text, sizeof (text), "%.0f s", t);
    return text;
}

std::string describeEcho (float position)
{
    const auto s = mapMacros ({ MacroPositions::kInitSpace, position, MacroPositions::kInitMotion, MacroPositions::kInitTone });
    if (s.echoSend <= 0.0)
        return "intet ekko";
    return "1/8d · " + std::to_string (echoRepeats (s.echoFeedback)) + "x";
}

std::string describeMotion (float position)
{
    if (position < 0.02f)
        return "stille";
    const auto s = mapMacros ({ MacroPositions::kInitSpace, MacroPositions::kInitEcho, position, MacroPositions::kInitTone });
    auto text = dots (position, "stille", "levende");
    if (s.motionPanDepth > 0.0)
        text += s.motionPanBars == 0 ? " · pan ½ takt" : " · pan " + std::to_string (s.motionPanBars) + (s.motionPanBars == 1 ? " takt" : " takter");
    return text;
}

std::string describeTone (float position) { return dots (position, "mørk", "lys"); }
} // namespace spaceforge::dsp

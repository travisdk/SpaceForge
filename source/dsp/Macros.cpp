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
constexpr std::array<double, 11> kSpaceGainDb { 8.83, 8.45, 7.51, 5.60, 2.82, 1.22, -0.03, -1.74, -3.26, -4.44, -5.21 };
constexpr std::array<double, 11> kEchoGainDb { 0.03, -0.30, -1.16, -1.19, -1.22, -1.27, -1.33, -1.42, -1.55, -1.73, -2.02 };
constexpr std::array<double, 11> kMotionGainDb { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
// How much of the room's grains go back into it (the sky), per Space position: found by
// measurement so the tail grows evenly, about twice as long per step up to Henrik's choice (C)
// at Init; above it the sky holds itself (the limit keeps it from growing).
constexpr std::array<double, 11> kSpaceSkyFeedDb { -40.0, -19.7, -8.7, -4.9, -3.9, -4.9, -6.2, -5.7, -5.2, -4.7, -4.2 };
constexpr std::array<double, 11> kToneGainDb { 1.71, 1.44, 1.17, 0.83, 0.44, -0.03, -0.32, -0.65, -1.04, -1.48, -1.99 };

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

const std::array<double, 11> kSpaceMeasuredT60 { 1.77, 3.50, 6.87, 13.40, 26.78, 50.99, 110.97, 117.11, 129.50, 133.82, 132.74 };

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
    // The sky grows with the room: hardly any in a short room, Henrik's choice (C) at Init.
    s.spaceSkyFeedDb = table (kSpaceSkyFeedDb, space);
    s.spaceSkyOutDb = through (space, -24.0, 0.6, -6.0, -6.0);
    s.spaceGainDb = table (kSpaceGainDb, space);

    const double echo = clamp01 (p.echo);
    s.echoSend = std::min (1.0, echo / 0.2);
    s.echoFeedback = echo > 0.0 ? 0.15 + 0.7 * echo : 0.0;
    s.echoGainDb = table (kEchoGainDb, echo);

    const double motion = clamp01 (p.motion);
    // Most of the modulation comes early (Henrik heard nothing below 75 %); the top stays under
    // the 6 cent vibrato limit.
    s.motionLineModMs = through (motion, 0.5, 0.5, 1.2, 1.5);
    s.motionLineModHz = through (motion, 0.2, 0.5, 0.3, 0.33);
    s.motionDiffuseModMs = through (motion, 0.4, 0.5, 0.8, 0.9);
    s.motionDiffuseModHz = through (motion, 0.3, 0.5, 0.45, 0.55);
    // The auto-pan runs over the whole knob, from none at 0 % to wide and fast at 100 %; half
    // its width is already there at 25 %, so the lower half can be heard too.
    s.motionPanDepth = through (motion, 0.0, 0.25, 0.35, 0.7);
    // Faster as Motion rises: 8, 4, 2, 1 and half a bar.
    s.motionPanBars = motion <= 0.15 ? 8 : motion <= 0.35 ? 4 : motion <= 0.6 ? 2 : motion <= 0.85 ? 1 : 0;
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
    if (t >= 120.0)
        return "∞"; // the sky holds itself
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

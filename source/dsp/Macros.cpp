// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/Macros.h"
#include "dsp/ModeTables.h"
#include "dsp/Modes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace spaceforge::dsp
{
namespace
{
struct SpaceOverride
{
    int mode = -1;
    double decayHalfSeconds = 0.0, skyFeedHalfDb = 0.0;
} spaceOverride;

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


int echoRepeats (double feedback)
{
    if (feedback <= 0.0)
        return 1;
    return 1 + (int) std::floor (24.0 / (-20.0 * std::log10 (std::min (feedback, 0.999))));
}

MacroSettings mapMacros (const MacroPositions& p)
{
    MacroSettings s;
    using namespace calibrated;
    const auto m = (size_t) std::clamp (p.mode, 0, kNumModes - 1);
    const auto& mode = modeSetup ((int) m);

    // Space: the Mode's range, a medium room (about 12 s) at 50 % in every Mode.
    const double space = clamp01 (p.space);
    const bool overridden = spaceOverride.mode == (int) m;
    const double half = overridden ? spaceOverride.decayHalfSeconds : kDecayHalfSeconds[m];
    const double feedHalf = overridden ? spaceOverride.skyFeedHalfDb : kSkyFeedHalfDb[m];
    s.spaceDecaySeconds = geometric (space, half / mode.decaySpan, 0.5, half, half * mode.decaySpan);
    s.spaceWidth = through (space, 0.6, 0.5, 1.0, 1.0);
    // The sky grows with the room: hardly any in a short room.
    s.spaceSkyFeedDb = through (space, mode.skyFeedLowDb, 0.5, feedHalf, mode.skyFeedHighDb);
    s.spaceSkyOutDb = through (space, mode.skyOutDb[0], 0.5, mode.skyOutDb[1], mode.skyOutDb[2]);
    s.spaceGainDb = table (kSpaceGainDb[m], space);

    const double echo = clamp01 (p.echo);
    s.echoSend = std::min (1.0, echo / 0.2);
    s.echoFeedback = echo > 0.0 ? 0.15 + 0.7 * echo : 0.0;
    s.echoGainDb = table (kEchoGainDb[m], echo);

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
    s.motionGainDb = table (kMotionGainDb[m], motion);

    const double tone = clamp01 (p.tone);
    s.toneLowpassHz = geometric (tone, 3500.0, 0.5, 9000.0, 18000.0);
    s.toneHighShelfDb = through (tone, -6.0, 0.5, -0.8, 0.0);
    s.toneInputHighCutHz = geometric (tone, 2500.0, 0.5, 7000.0, 18000.0);
    s.toneEchoDampingHz = geometric (tone, 1200.0, 0.5, 4500.0, 14000.0);
    s.toneTiltDb = through (tone, -8.0, 0.5, 0.0, 5.0);
    s.toneGainDb = table (kToneGainDb[m], tone);
    return s;
}

void setSpaceCalibrationOverride (int mode, double decayHalfSeconds, double skyFeedHalfDb)
{
    spaceOverride = { mode, decayHalfSeconds, skyFeedHalfDb };
}

std::string describeSpace (float position, int mode)
{
    const double t = table (calibrated::kSpaceMeasuredT60[(size_t) std::clamp (mode, 0, kNumModes - 1)], position);
    // From 80 s the tail is endless for all musical purposes (and too long to measure steadily:
    // the T60 of a tail that holds itself jumps between 90 and 200 s from one position to the next).
    if (t >= 80.0)
        return "∞";
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

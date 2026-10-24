// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// Written by `sf2_render --calibrate-modes`: per Mode (rows in the order of Modes.cpp), per knob
// position 0, 0.1, ..., 1.0 with the other knobs at Init. Do not edit by hand.

#pragma once

#include <array>
#include "dsp/ModeCount.h"

namespace spaceforge::dsp::calibrated
{
// Each Mode's level against the dry signal at Init (pink noise), dB.
inline constexpr std::array<double, kNumModes> kModeGainDb { -3.72, -0.14, -2.34, -4.54, 3.66, 2.87 };
// Space at 50 % in each Mode: Cloud Seed's decay setting (s) and the sky's feed (dB).
inline constexpr std::array<double, kNumModes> kDecayHalfSeconds { 1.700, 4.281, 6.684, 2.564, 3.000, 2.000 };
inline constexpr std::array<double, kNumModes> kSkyFeedHalfDb { -8.35, -24.00, -26.00, -3.00, -11.34, -7.12 };
// Clouds' level in each Mode (grains as loud as what goes in), dB.
inline constexpr std::array<double, kNumModes> kTextureGainDb { 4.38, 4.38, 4.38, 13.38, 11.52, 2.85 };
// Measured T60 at 1 kHz of a noise burst (s); the Space text shows it.
inline constexpr std::array<std::array<double, 11>, kNumModes> kSpaceMeasuredT60 { {
    { 2.29, 2.69, 3.26, 3.79, 5.26, 12.06, 14.34, 19.86, 34.68, 73.37, 150.60 },
    { 2.60, 3.18, 4.23, 6.74, 9.30, 12.08, 16.71, 23.86, 38.26, 191.26, 125.67 },
    { 3.15, 3.93, 5.19, 7.07, 9.24, 11.90, 15.80, 21.03, 29.42, 49.62, 205.91 },
    { 1.54, 2.31, 3.35, 5.51, 9.01, 12.08, 17.36, 23.69, 30.33, 37.09, 44.47 },
    { 3.50, 4.42, 5.33, 6.27, 8.29, 11.92, 16.47, 29.05, 108.31, 93.15, 132.13 },
    { 1.85, 1.91, 2.17, 2.88, 4.55, 11.95, 20.01, 44.68, 101.58, 134.97, 139.94 },
} };

// Level compensation per knob (dB, pink noise), so no knob moves the overall level.
inline constexpr std::array<std::array<double, 11>, kNumModes> kSpaceGainDb { {
    { 10.39, 10.10, 9.76, 9.28, 8.40, 6.30, 5.04, 3.39, 1.99, 0.63, -0.58 },
    { 5.97, 5.33, 4.51, 3.51, 2.35, 1.04, -0.05, -1.22, -2.47, -3.79, -5.24 },
    { 7.43, 6.33, 5.13, 3.84, 2.49, 1.10, -0.02, -1.16, -2.30, -3.42, -4.50 },
    { 10.62, 9.94, 9.05, 7.87, 5.67, 0.97, -0.02, -0.99, -1.90, -2.69, -3.36 },
    { 3.33, 3.05, 2.74, 2.36, 1.80, 0.82, -0.01, -1.42, -3.65, -5.97, -8.09 },
    { 5.51, 5.24, 4.91, 4.43, 3.57, 1.51, -0.01, -2.31, -4.32, -6.00, -7.59 },
} };

inline constexpr std::array<std::array<double, 11>, kNumModes> kEchoGainDb { {
    { 0.03, -0.30, -1.18, -1.21, -1.25, -1.31, -1.38, -1.49, -1.63, -1.84, -2.17 },
    { 0.03, -0.33, -1.25, -1.28, -1.32, -1.37, -1.44, -1.53, -1.66, -1.85, -2.14 },
    { 0.03, -0.29, -1.15, -1.18, -1.22, -1.28, -1.35, -1.45, -1.58, -1.78, -2.08 },
    { 0.03, -0.12, -0.66, -0.67, -0.69, -0.71, -0.75, -0.80, -0.87, -0.98, -1.16 },
    { 0.03, -0.20, -0.85, -0.88, -0.91, -0.94, -0.99, -1.06, -1.17, -1.30, -1.53 },
    { 0.03, -0.17, -0.74, -0.76, -0.79, -0.83, -0.89, -0.95, -1.05, -1.19, -1.41 },
} };

inline constexpr std::array<std::array<double, 11>, kNumModes> kMotionGainDb { {
    { -0.07, 0.04, 0.11, 0.03, 0.07, 0.00, 0.00, -0.03, -0.03, 0.10, 0.08 },
    { 0.29, -0.02, 0.16, 0.10, -0.12, 0.00, -0.01, 0.02, -0.22, 0.01, 0.17 },
    { -0.11, -0.17, 0.00, -0.04, -0.11, 0.00, 0.09, 0.09, 0.19, 0.13, 0.22 },
    { 0.17, 0.13, -0.05, -0.07, 0.11, 0.00, 0.04, 0.08, 0.04, -0.10, 0.12 },
    { -0.23, -0.09, -0.18, -0.03, -0.03, 0.00, -0.03, -0.15, 0.17, 0.06, -0.01 },
    { -1.45, -0.19, -0.25, -0.16, 0.08, 0.00, -0.06, 0.01, -0.01, -0.04, -0.07 },
} };

inline constexpr std::array<std::array<double, 11>, kNumModes> kToneGainDb { {
    { 2.23, 1.87, 1.50, 1.04, 0.53, -0.03, -0.35, -0.71, -1.13, -1.60, -2.14 },
    { 1.54, 1.25, 0.94, 0.65, 0.34, -0.03, -0.26, -0.52, -0.84, -1.20, -1.63 },
    { 1.91, 1.53, 1.16, 0.78, 0.39, -0.03, -0.26, -0.52, -0.81, -1.16, -1.59 },
    { 3.81, 3.07, 2.48, 1.83, 0.98, -0.03, -0.60, -1.21, -1.86, -2.56, -3.31 },
    { 1.22, 1.01, 0.73, 0.47, 0.31, -0.03, -0.25, -0.51, -0.80, -1.15, -1.54 },
    { 1.51, 1.24, 0.95, 0.67, 0.39, -0.03, -0.29, -0.58, -0.91, -1.30, -1.74 },
} };
} // namespace spaceforge::dsp::calibrated

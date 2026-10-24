// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include "analysis/TestSignals.h"

namespace spaceforge::analysis
{
// Measurements of rendered sound, used by the render tool's report and by the tests.
// Offline only: slow and allocating, never in the plug-in.

double rmsDb (const Stereo& s, size_t from, size_t to);

// Reverb time T30 (Schroeder backward integration, -5..-35 dB, extrapolated to 60 dB) of an
// impulse response, both channels' energy together. 0 if it never falls 35 dB.
double reverbTime (const Stereo& ir, double sampleRate);

// The same per octave band.
inline constexpr std::array<double, 7> kOctaveBands { 125, 250, 500, 1000, 2000, 4000, 8000 };
std::array<double, kOctaveBands.size()> reverbTimePerOctave (const Stereo& ir, double sampleRate);

// Brightness: the spectral centroid in Hz (both channels), 20 Hz..20 kHz.
double spectralCentroid (const Stereo& s, double sampleRate, size_t from, size_t to);

// Correlation of left and right (1 = mono, 0 = unrelated, -1 = opposite) below or above a
// frequency, over a stretch of the signal.
double correlationBelow (const Stereo& s, double sampleRate, double hz, size_t from, size_t to);
double correlationAbove (const Stereo& s, double sampleRate, double hz, size_t from, size_t to);

// Level change in dB when left and right are summed to mono ((L+R)/2 against the stereo level).
double monoSumLossDb (const Stereo& s, size_t from, size_t to);

// Ringing: how far narrow peaks in the tail's spectrum stick out above the smoothed spectrum
// (95th percentile, dB, 200 Hz..8 kHz). Metallic tails score high; compare with noiseBaseline.
double ringingDb (const Stereo& s, double sampleRate, size_t from, size_t to);

// Flutter: how unevenly the impulse response's level falls (dB, the spread of 30 ms levels
// around their 0.5 s average). Grainy, pumping or echoey tails score high; compare with noiseBaseline.
double flutterDb (const Stereo& ir, double sampleRate);

// Time in ms until the impulse response becomes dense noise (normalised echo density >= 0.9,
// Abel & Huang). Low = no audible single echoes at the start.
double densityTimeMs (const Stereo& ir, double sampleRate);

// The same two measures on an ideal tail (random noise with the same spectrum as tail[from..to)
// and the same decay time), so the numbers above read as "this much worse than perfectly smooth".
struct NoiseBaseline
{
    double ringingDb = 0.0, flutterDb = 0.0;
};
NoiseBaseline noiseBaseline (const Stereo& tail, size_t from, size_t to, double t60, double sampleRate);

// How differently two renders of the same sound unfold: octave-band levels (125 Hz..8 kHz) in
// 170 ms frames, the average difference in dB. Coarse on purpose, so random fine detail averages
// out and only real changes (scattered fragments, other colour) remain. 0 = identical.
double spectroTemporalDifferenceDb (const Stereo& a, const Stereo& b, double sampleRate, size_t from, size_t to);

// How much a tail moves: in octave bands 250 Hz..4 kHz, frames of ~43 ms, each band's level
// with its straight decay line (dB against time) taken out; the RMS of what is left, averaged
// over the bands (dB). 0 = a perfectly even decay; modulation, beating and grains raise it.
double tailMovementDb (const Stereo& s, double sampleRate, size_t from, size_t to);

// How strongly a sound comes back as a copy (an echo, a grain): the largest normalised
// correlation between `original` (the sound alone) and the output, at delays between minLag and
// maxLag seconds after `start`. 1 = an exact copy, a diffuse tail stays far lower.
double copyStrength (const Stereo& out, const std::vector<float>& original, size_t start, double sampleRate, double minLag, double maxLag);

// Pitch wobble from modulated delay lines, in cents (largest deviation of a sine-modulated delay).
double modulationCents (double depthMs, double rateHz);
} // namespace spaceforge::analysis

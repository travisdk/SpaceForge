// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Minimal test helpers (from SpaceForge 1): CHECK records a failure and keeps going, finish()
// prints a summary and returns the process exit code for CTest.
inline int& testFailures()
{
    static int failures = 0;
    return failures;
}

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            ++testFailures();                                                              \
            std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
        }                                                                                  \
    } while (false)

// Like CHECK, but prints the measured value too, which makes tuning failures readable.
#define CHECK_RANGE(value, lo, hi)                                                                         \
    do                                                                                                     \
    {                                                                                                      \
        const double v_ = (double) (value);                                                                \
        if (! (v_ >= (double) (lo) && v_ <= (double) (hi)))                                                \
        {                                                                                                  \
            ++testFailures();                                                                              \
            std::printf ("FAIL %s:%d: %s = %g, expected %g..%g\n", __FILE__, __LINE__, #value, v_,          \
                         (double) (lo), (double) (hi));                                                    \
        }                                                                                                  \
    } while (false)

inline bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}

inline float peakOf (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min (to, v.size()); ++i)
        p = std::max (p, std::abs (v[i]));
    return p;
}

inline double rmsOf (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    double sum = 0.0;
    to = std::min (to, v.size());
    for (size_t i = from; i < to; ++i)
        sum += (double) v[i] * v[i];
    return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
}

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

inline int finish (const char* name)
{
    std::printf ("%s: %s (%d failed checks)\n", name, testFailures() == 0 ? "passed" : "FAILED", testFailures());
    return testFailures() == 0 ? 0 : 1;
}

inline constexpr double kSampleRates[] = { 44100.0, 48000.0, 96000.0 };

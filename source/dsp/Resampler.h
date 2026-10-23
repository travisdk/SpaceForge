// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace spaceforge::dsp
{
// Stereo sample-rate converter between any two fixed rates: a windowed-sinc filter (Blackman-
// Harris window, 16 zero crossings each side) read from a finely sampled table. Cuts at 90 % of
// the lower Nyquist frequency, so it neither aliases going down nor leaves images going up.
//
// Push-driven: every input sample is pushed in, and each output sample that becomes available is
// handed to a callback. prepare() allocates; reset() and push() never do.
class Resampler
{
public:
    void prepare (double inRate, double outRate)
    {
        step = inRate / outRate;
        const double cutoff = 0.5 * 0.9 * std::min (1.0, outRate / inRate); // cycles per input sample
        halfTaps = (int) std::ceil (16.0 / (2.0 * cutoff));
        table.assign ((size_t) (halfTaps * kOversample + 2), 0.0f);
        for (size_t i = 0; i < table.size(); ++i)
        {
            const double x = (double) i / kOversample;
            if (x >= halfTaps)
                continue;
            const double t = 2.0 * cutoff * x;
            const double sinc = t < 1.0e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double w = 0.5 + 0.5 * x / halfTaps; // 0.5..1 over the right half of the window
            const double window = 0.35875 - 0.48829 * std::cos (2.0 * kPi * w) + 0.14128 * std::cos (4.0 * kPi * w)
                                  - 0.01168 * std::cos (6.0 * kPi * w);
            table[i] = (float) (2.0 * cutoff * sinc * window);
        }
        size_t size = 1;
        while (size < (size_t) (2 * halfTaps + 4))
            size <<= 1;
        historyL.assign (size, 0.0f);
        historyR.assign (size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (historyL.begin(), historyL.end(), 0.0f);
        std::fill (historyR.begin(), historyR.end(), 0.0f);
        count = 0;
        nextOut = 0.0;
    }

    // Input samples by which the output lags (the filter's look-ahead).
    int lookahead() const { return halfTaps; }

    template <typename Emit>
    void push (float l, float r, Emit&& emit)
    {
        historyL[(size_t) count & mask] = l;
        historyR[(size_t) count & mask] = r;
        ++count;
        // An output at input time t needs inputs up to floor(t) + halfTaps.
        while (std::floor (nextOut) + halfTaps <= (double) (count - 1))
        {
            const auto centre = (int64_t) std::floor (nextOut);
            float outL = 0.0f, outR = 0.0f;
            for (int64_t k = centre - halfTaps + 1; k <= centre + halfTaps; ++k)
            {
                const float h = kernel (std::abs (nextOut - (double) k));
                const auto idx = (size_t) k & mask;
                outL += historyL[idx] * h;
                outR += historyR[idx] * h;
            }
            emit (outL, outR);
            nextOut += step;
        }
    }

private:
    static constexpr int kOversample = 64;
    static constexpr double kPi = 3.14159265358979323846;

    float kernel (double x) const
    {
        const double pos = x * kOversample;
        const auto i = (size_t) pos;
        if (i + 1 >= table.size())
            return 0.0f;
        const auto frac = (float) (pos - (double) i);
        return table[i] + (table[i + 1] - table[i]) * frac;
    }

    std::vector<float> table, historyL, historyR;
    size_t mask = 0;
    int halfTaps = 16;
    double step = 1.0;
    int64_t count = 0; // input samples pushed so far
    double nextOut = 0.0; // input time of the next output sample
};
} // namespace spaceforge::dsp

// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "analysis/Measure.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numeric>

namespace spaceforge::analysis
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> w (std::cos (-2.0 * kPi / (double) len), std::sin (-2.0 * kPi / (double) len));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                wn *= w;
            }
        }
    }
}

// RBJ cookbook biquad, direct form I in double.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    enum class Type { LowPass, HighPass, BandPass };
    Biquad (Type type, double hz, double q, double rate)
    {
        const double w = 2.0 * kPi * hz / rate, cosw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        switch (type)
        {
            case Type::LowPass:  b0 = (1.0 - cosw) / 2.0; b1 = 1.0 - cosw; b2 = b0; break;
            case Type::HighPass: b0 = (1.0 + cosw) / 2.0; b1 = -(1.0 + cosw); b2 = b0; break;
            case Type::BandPass: b0 = alpha; b1 = 0.0; b2 = -alpha; break;
        }
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// Runs the signal through the filter twice (doubles its slope), both channels.
Stereo filtered (const Stereo& s, Biquad::Type type, double hz, double q, double rate)
{
    Stereo out = s;
    for (auto* channel : { &out.l, &out.r })
        for (int pass = 0; pass < 2; ++pass)
        {
            Biquad f (type, hz, q, rate);
            for (auto& x : *channel)
                x = (float) f.process (x);
        }
    return out;
}

std::vector<double> energyOf (const Stereo& s)
{
    std::vector<double> e (s.size());
    for (size_t i = 0; i < s.size(); ++i)
        e[i] = (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
    return e;
}

// Slope (per index) of the least-squares line through y[from..to).
double slopeOf (const std::vector<double>& y, size_t from, size_t to)
{
    const double n = (double) (to - from);
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = from; i < to; ++i)
    {
        const double x = (double) (i - from);
        sx += x; sy += y[i]; sxx += x * x; sxy += x * y[i];
    }
    const double d = n * sxx - sx * sx;
    return std::abs (d) > 0.0 ? (n * sxy - sx * sy) / d : 0.0;
}

double t30FromEnergy (const std::vector<double>& e, double sampleRate)
{
    std::vector<double> db (e.size());
    double sum = 0.0;
    for (size_t i = e.size(); i-- > 0;)
        db[i] = (sum += e[i]);
    const double total = sum;
    if (total <= 0.0)
        return 0.0;
    size_t at5 = 0, at35 = 0;
    for (size_t i = 0; i < db.size(); ++i)
    {
        db[i] = 10.0 * std::log10 (std::max (db[i] / total, 1.0e-30));
        if (at5 == 0 && db[i] <= -5.0)
            at5 = i;
        if (db[i] <= -35.0)
        {
            at35 = i;
            break;
        }
    }
    if (at35 <= at5 + 1)
        return 0.0;
    const double slope = slopeOf (db, at5, at35); // dB per sample
    return slope < 0.0 ? -60.0 / slope / sampleRate : 0.0;
}

// Welch power spectrum of both channels, Hann window. With normalise, every segment counts
// equally however loud it is (for decaying tails, so the start does not dominate).
std::vector<double> powerSpectrum (const Stereo& s, size_t from, size_t to, size_t window, bool normalise)
{
    std::vector<double> psd (window / 2 + 1, 0.0), hann (window);
    for (size_t i = 0; i < window; ++i)
        hann[i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) window);
    to = std::min (to, s.size());
    std::vector<std::complex<double>> buf (window);
    std::vector<double> segment (psd.size());
    for (size_t start = from; start + window <= to; start += window / 2)
        for (const auto* channel : { &s.l, &s.r })
        {
            for (size_t i = 0; i < window; ++i)
                buf[i] = { (*channel)[start + i] * hann[i], 0.0 };
            fft (buf);
            double total = 0.0;
            for (size_t k = 0; k < segment.size(); ++k)
                total += (segment[k] = std::norm (buf[k]));
            const double scale = normalise ? (total > 0.0 ? 1.0 / total : 0.0) : 1.0;
            for (size_t k = 0; k < segment.size(); ++k)
                psd[k] += segment[k] * scale;
        }
    return psd;
}

double correlation (const Stereo& s, size_t from, size_t to)
{
    to = std::min (to, s.size());
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = from; i < to; ++i)
    {
        lr += (double) s.l[i] * s.r[i];
        ll += (double) s.l[i] * s.l[i];
        rr += (double) s.r[i] * s.r[i];
    }
    return ll > 0.0 && rr > 0.0 ? lr / std::sqrt (ll * rr) : 1.0;
}

double percentile (std::vector<double> v, double p)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    return v[(size_t) std::clamp (p * (double) (v.size() - 1), 0.0, (double) (v.size() - 1))];
}

uint32_t nextRandom (uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
} // namespace

double rmsDb (const Stereo& s, size_t from, size_t to)
{
    to = std::min (to, s.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += 0.5 * ((double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i]);
    return to > from ? 10.0 * std::log10 (std::max (sum / (double) (to - from), 1.0e-24)) : -240.0;
}

double reverbTime (const Stereo& ir, double sampleRate) { return t30FromEnergy (energyOf (ir), sampleRate); }

std::array<double, kOctaveBands.size()> reverbTimePerOctave (const Stereo& ir, double sampleRate)
{
    std::array<double, kOctaveBands.size()> t {};
    for (size_t b = 0; b < kOctaveBands.size(); ++b)
        t[b] = t30FromEnergy (energyOf (filtered (ir, Biquad::Type::BandPass, kOctaveBands[b], std::sqrt (2.0), sampleRate)), sampleRate);
    return t;
}

double spectralCentroid (const Stereo& s, double sampleRate, size_t from, size_t to)
{
    constexpr size_t window = 4096;
    const auto psd = powerSpectrum (s, from, to, window, false);
    double num = 0.0, den = 0.0;
    for (size_t k = 0; k < psd.size(); ++k)
    {
        const double hz = (double) k * sampleRate / (double) window;
        if (hz < 20.0 || hz > 20000.0)
            continue;
        num += hz * psd[k];
        den += psd[k];
    }
    return den > 0.0 ? num / den : 0.0;
}

double correlationBelow (const Stereo& s, double sampleRate, double hz, size_t from, size_t to)
{
    return correlation (filtered (s, Biquad::Type::LowPass, hz, std::sqrt (0.5), sampleRate), from, to);
}

double correlationAbove (const Stereo& s, double sampleRate, double hz, size_t from, size_t to)
{
    return correlation (filtered (s, Biquad::Type::HighPass, hz, std::sqrt (0.5), sampleRate), from, to);
}

double monoSumLossDb (const Stereo& s, size_t from, size_t to)
{
    Stereo mono = s;
    for (size_t i = 0; i < s.size(); ++i)
        mono.l[i] = mono.r[i] = 0.5f * (s.l[i] + s.r[i]);
    return rmsDb (mono, from, to) - rmsDb (s, from, to);
}

double ringingDb (const Stereo& s, double sampleRate, size_t from, size_t to)
{
    constexpr size_t window = 8192;
    const auto psd = powerSpectrum (s, from, to, window, true);
    const double binHz = sampleRate / (double) window;
    const double sixthOctave = std::pow (2.0, 1.0 / 12.0); // +-1/12 octave = 1/6 octave wide
    std::vector<double> ratios;
    for (size_t k = 0; k < psd.size(); ++k)
    {
        const double hz = (double) k * binHz;
        if (hz < 200.0 || hz > 8000.0 || psd[k] <= 0.0)
            continue;
        const auto lo = (size_t) std::floor (hz / sixthOctave / binHz), hi = (size_t) std::ceil (hz * sixthOctave / binHz);
        double mean = 0.0;
        for (size_t j = lo; j <= hi; ++j)
            mean += psd[j];
        mean /= (double) (hi - lo + 1);
        ratios.push_back (10.0 * std::log10 (psd[k] / mean));
    }
    return percentile (ratios, 0.95);
}

double flutterDb (const Stereo& ir, double sampleRate)
{
    // 30 ms levels in dB, compared with their own 0.5 s running average: what is left are fast
    // ups and downs (pumping, beating, grainy echoes), not the slow bend of the decay itself.
    const auto block = (size_t) std::lround (0.03 * sampleRate);
    constexpr size_t half = 8; // +-8 blocks = about 0.5 s
    std::vector<double> db;
    for (size_t start = 0; start + block <= ir.size(); start += block)
        db.push_back (rmsDb (ir, start, start + block));
    if (db.empty())
        return 0.0;
    const size_t peak = (size_t) (std::max_element (db.begin(), db.end()) - db.begin());
    const size_t from = peak + (size_t) std::lround (0.1 / 0.03) + half;
    size_t to = from;
    while (to + half < db.size() && db[to] > db[peak] - 45.0)
        ++to;
    if (to < from + 10)
        return 0.0;
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double average = std::accumulate (db.begin() + (long) (i - half), db.begin() + (long) (i + half + 1), 0.0) / (double) (2 * half + 1);
        sum += (db[i] - average) * (db[i] - average);
    }
    return std::sqrt (sum / (double) (to - from));
}

double densityTimeMs (const Stereo& ir, double sampleRate)
{
    const auto& x = ir.l;
    size_t first = 0;
    while (first < x.size() && std::abs (x[first]) < 1.0e-7f)
        ++first;
    const auto window = (size_t) std::lround (0.02 * sampleRate), hop = (size_t) std::lround (0.001 * sampleRate);
    const double expected = std::erfc (1.0 / std::sqrt (2.0)); // share outside 1 sigma for Gaussian noise
    for (size_t start = first; start + window <= x.size(); start += hop)
    {
        double sum = 0.0, sq = 0.0;
        for (size_t i = start; i < start + window; ++i)
        {
            sum += x[i];
            sq += (double) x[i] * x[i];
        }
        const double mean = sum / (double) window;
        const double sigma = std::sqrt (std::max (0.0, sq / (double) window - mean * mean));
        if (sigma <= 0.0)
            continue;
        size_t outside = 0;
        for (size_t i = start; i < start + window; ++i)
            outside += std::abs (x[i] - mean) > sigma ? 1u : 0u;
        if ((double) outside / (double) window / expected >= 0.9)
            return (double) (start + window / 2 - first) / sampleRate * 1000.0;
    }
    return -1.0;
}

NoiseBaseline noiseBaseline (const Stereo& tail, size_t from, size_t to, double t60, double sampleRate)
{
    // Random noise with the tail's own spectrum (random phases, smooth Welch magnitude) and one
    // clean exponential decay: the same colour and length, but perfectly smooth.
    constexpr size_t window = 4096;
    const auto psd = powerSpectrum (tail, from, to, window, true);
    const double seconds = std::max (to > from ? (double) (to - from) / sampleRate : 0.0, 0.75 * t60 + 1.0);
    size_t n = 1;
    while ((double) n < seconds * sampleRate && n < ((size_t) 1 << 22))
        n <<= 1;

    uint32_t state = 12345;
    Stereo s { std::vector<float> (n), std::vector<float> (n) };
    const double perSample = std::pow (10.0, -3.0 / (std::max (t60, 0.1) * sampleRate));
    for (auto* channel : { &s.l, &s.r })
    {
        std::vector<std::complex<double>> spectrum (n);
        for (size_t k = 1; k < n / 2; ++k)
        {
            const double position = (double) k * (double) window / (double) n;
            const auto i = std::min ((size_t) position, psd.size() - 2);
            const double frac = position - (double) i;
            const double magnitude = std::sqrt (std::max (0.0, psd[i] * (1.0 - frac) + psd[i + 1] * frac));
            const double phase = (double) nextRandom (state) / 4294967295.0 * 2.0 * kPi;
            spectrum[k] = std::polar (magnitude, phase);
            spectrum[n - k] = std::conj (spectrum[k]);
        }
        fft (spectrum); // a forward transform of a Hermitian spectrum gives a real (time-reversed) signal: fine for noise
        double peak = 0.0;
        for (const auto& x : spectrum)
            peak = std::max (peak, std::abs (x.real()));
        double gain = 1.0;
        for (size_t i = 0; i < n; ++i)
        {
            (*channel)[i] = (float) (spectrum[i].real() / std::max (peak, 1.0e-30) * gain);
            gain *= perSample;
        }
    }
    return { ringingDb (s, sampleRate, 0, std::min (n, to - from)), flutterDb (s, sampleRate) };
}

double modulationCents (double depthMs, double rateHz)
{
    return 1200.0 * std::log2 (1.0 + 2.0 * kPi * rateHz * depthMs / 1000.0);
}
} // namespace spaceforge::analysis

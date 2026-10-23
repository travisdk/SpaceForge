// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// Smoke test of the two vendored engines on their own, outside the plug-in: they build on this
// platform, run with host-like block sizes, stay finite and make a clearly audible sound.
// Cloud Seed runs at the host sample rates; Clouds still runs at its native 32 kHz (adapting
// it to the host rate is phase 3).

#include <cstdint>
#include <memory>
#include "TestUtils.h"
#include "DSP/ReverbController.h"
#include "Programs.h"
#include "clouds/dsp/granular_processor.h"

namespace
{
// True when the two channels are not the same signal (exact float compare is what we want here,
// but -Wfloat-equal rightly flags ==, so compare the largest difference).
bool channelsDiffer (const std::vector<float>& a, const std::vector<float>& b)
{
    float largest = 0.0f;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        largest = std::max (largest, std::abs (a[i] - b[i]));
    return largest > 1.0e-6f;
}

// Reverb time from the impulse response: Schroeder's backward-integrated energy, the slope
// between -5 and -35 dB extrapolated to 60 dB (T30). Returns 0 when it never falls 35 dB.
double reverbTimeSeconds (const std::vector<float>& ir, double sampleRate)
{
    std::vector<double> energy (ir.size());
    double sum = 0.0;
    for (size_t i = ir.size(); i-- > 0;)
        energy[i] = (sum += (double) ir[i] * ir[i]);

    const double total = energy[0];
    size_t at5 = 0, at35 = 0;
    for (size_t i = 0; i < ir.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (energy[i] / total, 1.0e-30));
        if (at5 == 0 && db <= -5.0)
            at5 = i;
        if (db <= -35.0)
        {
            at35 = i;
            break;
        }
    }
    return at35 > at5 ? 2.0 * (double) (at35 - at5) / sampleRate : 0.0;
}

void testCloudSeed()
{
    std::printf ("Cloud Seed, 'Dark Plate' program from CloudSeedCore, impulse in:\n");
    std::printf ("  rate      block  peak      T30      energy out/in\n");

    Cloudseed::initPrograms();
    double firstT30 = 0.0, firstGain = 0.0;
    for (const double rate : kSampleRates)
    {
        // A block larger than the engine's internal chunk (1024) and an odd one.
        for (const int block : { 333, 2048 })
        {
            auto reverb = std::make_unique<Cloudseed::ReverbController> ((int) rate);
            for (int i = 0; i < Cloudseed::Parameter::COUNT; ++i)
                reverb->SetParameter (i, Cloudseed::ProgramDarkPlate[i]);
            reverb->SetParameter (Cloudseed::Parameter::DryOut, 0.0); // wet only
            reverb->SetSamplerate ((int) rate);
            reverb->ClearBuffers();

            const size_t length = (size_t) (rate * 8.0);
            std::vector<float> inL (length, 0.0f), inR (length, 0.0f), outL (length), outR (length);
            inL[0] = inR[0] = 1.0f;
            for (size_t pos = 0; pos < length; pos += (size_t) block)
            {
                const int n = (int) std::min ((size_t) block, length - pos);
                reverb->Process (&inL[pos], &inR[pos], &outL[pos], &outR[pos], n);
            }

            // Energy out over energy in (the impulse has energy 1), independent of sample rate.
            double energy = 0.0;
            for (const float x : outL)
                energy += (double) x * x;
            const double gain = 10.0 * std::log10 (std::max (energy, 1.0e-30));
            const double t30 = reverbTimeSeconds (outL, rate);
            std::printf ("  %-8.0f  %5d  %.4f    %5.2f s  %6.1f dB\n", rate, block, peakOf (outL), t30, gain);

            CHECK (allFinite (outL) && allFinite (outR));
            CHECK_RANGE (peakOf (outL), 0.001, 2.0);
            CHECK_RANGE (t30, 0.3, 30.0);        // a real tail, not a click
            CHECK_RANGE (gain, -40.0, 20.0);     // a clear tail; the level is calibrated in phase 2
            CHECK (channelsDiffer (outL, outR));  // the two channels differ: it is stereo

            // The same room at every sample rate: within 15 % of the 44.1 kHz decay, 3 dB of its level.
            if (firstT30 <= 0.0)
            {
                firstT30 = t30;
                firstGain = gain;
            }
            CHECK_RANGE (t30 / firstT30, 0.85, 1.15);
            CHECK_RANGE (gain - firstGain, -3.0, 3.0);
        }
    }
}

void testClouds()
{
    constexpr double rate = 32000.0;
    constexpr size_t block = 32;
    std::printf ("Clouds, granular mode at its native 32 kHz, 400 Hz sine at -6 dBFS in:\n");

    // Buffer sizes from the author's own desktop test (clouds/test/clouds_test.cc).
    static uint8_t largeBuffer[118784];
    static uint8_t smallBuffer[65536 - 128];

    // Static storage, as on the module: GranularProcessor's constructor leaves members (silence_
    // among them) uninitialised and relies on the zero-filled memory of a global. On the heap it
    // starts with garbage and may stay silent. The plug-in must zero it the same way (phase 3).
    static clouds::GranularProcessor processorStorage;
    auto* processor = &processorStorage;
    processor->Init (largeBuffer, sizeof (largeBuffer), smallBuffer, sizeof (smallBuffer));
    processor->set_num_channels (2);
    processor->set_low_fidelity (false);
    processor->set_playback_mode (clouds::PLAYBACK_MODE_GRANULAR);

    auto* p = processor->mutable_parameters();
    p->position = 0.3f;
    p->size = 0.5f;
    p->pitch = 0.0f;
    p->density = 0.75f;
    p->texture = 0.5f;
    // Wet only, but not exactly 1: at 1.0 Clouds' cross-fade lookup reads one entry past its
    // table (THIRD_PARTY.md). TextureEngine keeps it below 1 the same way.
    p->dry_wet = 0.999f;
    p->stereo_spread = 0.5f;
    p->feedback = 0.0f;
    p->reverb = 0.0f;
    p->freeze = false;
    p->trigger = false;
    p->gate = false;

    const size_t length = (size_t) (rate * 3.0);
    std::vector<float> outL, outR;
    outL.reserve (length);
    outR.reserve (length);
    float phase = 0.0f;
    processor->Prepare();
    for (size_t pos = 0; pos < length; pos += block)
    {
        clouds::ShortFrame input[block], output[block];
        for (auto& frame : input)
        {
            phase += 400.0f / (float) rate;
            phase -= std::floor (phase);
            frame.l = frame.r = (short) (16384.0f * std::sin (phase * 6.2831853f));
        }
        processor->Process (input, output, block);
        processor->Prepare();
        for (const auto& frame : output)
        {
            outL.push_back ((float) frame.l / 32768.0f);
            outR.push_back ((float) frame.r / 32768.0f);
        }
    }

    const double level = toDb (rmsOf (outL, (size_t) rate));
    std::printf ("  level after 1 s: %.1f dBFS, peak %.3f, L and R %s\n", level, peakOf (outL),
                 channelsDiffer (outL, outR) ? "differ" : "identical");

    CHECK (allFinite (outL) && allFinite (outR));
    CHECK_RANGE (level, -30.0, 0.0); // clearly audible grains
    CHECK (channelsDiffer (outL, outR)); // stereo spread gives two different channels
}
} // namespace

int main()
{
    testCloudSeed();
    testClouds();
    return finish ("engines");
}

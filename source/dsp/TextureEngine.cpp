// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "dsp/TextureEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"

namespace spaceforge::dsp
{
namespace
{
// Buffer sizes: as in the author's own desktop test (clouds/test/clouds_test.cc), about one
// second of stereo sound at 32 kHz, but twice the bytes, since Clouds here keeps its samples as
// floats (THIRD_PARTY.md). The small buffer holds one channel's samples; the large one the
// other channel's plus Clouds' effect workspace (diffuser, reverb, correlator, pitch shifter,
// all floats: about 90 kB).
constexpr size_t kSmallBufferBytes = 2 * (65536 - 128);
constexpr size_t kLargeBufferBytes = kSmallBufferBytes + 98304;

// Clouds' levels are those of the module's codec (full scale 1.0) and its soft limiters sit
// there. The signal goes in 6 dB lower (and comes out 6 dB higher), so a host signal up to
// +6 dBFS meets them where the module's input did. Nothing is rounded: Clouds runs on floats.
constexpr float kHeadroom = 0.5f;

// Clouds reads one entry past a lookup table when dry/wet or size is exactly 1.0
// (THIRD_PARTY.md), so no setting reaches 1. Just below it the difference is inaudible; for
// dry/wet the dry signal is 56 dB down: grains only, for all practical purposes.
constexpr float kAlmostOne = 0.999f;
constexpr float kWetOnly = kAlmostOne;

float setting (double v) { return std::isfinite (v) ? std::clamp ((float) v, 0.0f, kAlmostOne) : 0.0f; }


// Clouds' constructor leaves members uninitialised and relies on the zero-filled memory of a
// global (as on the module). So it lives in memory that is zeroed before it is constructed.
clouds::GranularProcessor* constructZeroed (void* memory)
{
    std::memset (memory, 0, sizeof (clouds::GranularProcessor));
    return new (memory) clouds::GranularProcessor();
}
} // namespace

void TextureEngine::ProcessorDeleter::operator() (clouds::GranularProcessor* p) const
{
    p->~GranularProcessor();
    std::free (p);
}

TextureEngine::TextureEngine()
    : largeBuffer (kLargeBufferBytes), smallBuffer (kSmallBufferBytes)
{
    void* memory = std::malloc (sizeof (clouds::GranularProcessor));
    if (memory == nullptr)
        throw std::bad_alloc();
    processor.reset (constructZeroed (memory));
    prepare (48000.0);
}

TextureEngine::~TextureEngine() = default;

void TextureEngine::prepare (double hostRate)
{
    down.prepare (hostRate, kCloudsRate);
    up.prepare (kCloudsRate, hostRate);

    // The output must never run dry. Worst case, Clouds holds back one block not yet full, and
    // the two converters their look-ahead; the output starts that much late.
    const double ratio = hostRate / kCloudsRate;
    prefill = (int) std::ceil (down.lookahead() + (kBlock + up.lookahead() + 1) * ratio) + 2;

    size_t size = 1;
    while (size < (size_t) (prefill * 2 + 4096))
        size <<= 1;
    fifoL.assign (size, 0.0f);
    fifoR.assign (size, 0.0f);
    fifoMask = size - 1;
    reset();
}

void TextureEngine::reset()
{
    // A fresh Clouds with empty buffers, and the same random grains every time (renders and tests
    // can be compared sample for sample).
    std::fill (largeBuffer.begin(), largeBuffer.end(), uint8_t (0));
    std::fill (smallBuffer.begin(), smallBuffer.end(), uint8_t (0));
    auto* p = processor.get();
    p->~GranularProcessor();
    constructZeroed (p);
    p->Init (largeBuffer.data(), largeBuffer.size(), smallBuffer.data(), smallBuffer.size());
    p->set_num_channels (2);
    p->set_low_fidelity (false);
    applyVoicing();
    appliedGain = outputGain;
    p->Prepare();
    stmlib::Random::Seed (0x5f2u);

    down.reset();
    up.reset();
    blockFill = 0;
    std::fill (fifoL.begin(), fifoL.end(), 0.0f);
    std::fill (fifoR.begin(), fifoR.end(), 0.0f);
    readPos = 0;
    writePos = (size_t) prefill;
    underrunCount = 0;
}

void TextureEngine::setVoicing (const TextureVoicing& voicing)
{
    currentVoicing = voicing;
    applyVoicing();
}

void TextureEngine::applyVoicing()
{
    auto* p = processor->mutable_parameters();
    const auto& v = currentVoicing;
    p->position = setting (v.position);
    p->size = setting (v.size);
    // A new mode takes effect at the next Prepare(): Clouds stays silent for that one block.
    processor->set_playback_mode ((clouds::PlaybackMode) std::clamp (v.playbackMode, 0, (int) clouds::PLAYBACK_MODE_SPECTRAL));
    p->pitch = std::isfinite (v.pitchSemitones) ? (float) std::clamp (v.pitchSemitones, -24.0, 24.0) : 0.0f;
    p->density = setting (v.density);
    p->texture = setting (v.texture);
    p->dry_wet = kWetOnly;
    p->stereo_spread = setting (v.stereoSpread);
    p->feedback = setting (v.feedback);
    p->reverb = 0.0f; // the room after it does that
    p->freeze = false;
    p->trigger = false;
    p->gate = false;
    outputGain = (float) std::pow (10.0, v.gainDb / 20.0) / kHeadroom;
}

void TextureEngine::runCloudsBlock()
{
    clouds::FloatFrame input[kBlock], output[kBlock];
    for (int i = 0; i < kBlock; ++i)
    {
        input[i].l = blockL[(size_t) i] * kHeadroom;
        input[i].r = blockR[(size_t) i] * kHeadroom;
    }
    processor->Process (input, output, (size_t) kBlock);
    processor->Prepare();
    // The level glides across the block (Motion moves it).
    const float from = appliedGain;
    appliedGain = outputGain;
    for (int i = 0; i < kBlock; ++i)
    {
        const float gain = from + (outputGain - from) * (float) (i + 1) / (float) kBlock;
        const float l = output[i].l * gain;
        const float r = output[i].r * gain;
        up.push (l, r, [this] (float outL, float outR) {
            fifoL[writePos & fifoMask] = outL;
            fifoR[writePos & fifoMask] = outR;
            ++writePos;
        });
    }
}

void TextureEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        down.push (inL[i], inR[i], [this] (float l, float r) {
            blockL[(size_t) blockFill] = l;
            blockR[(size_t) blockFill] = r;
            if (++blockFill == kBlock)
            {
                runCloudsBlock();
                blockFill = 0;
            }
        });
    }
    for (int i = 0; i < n; ++i)
    {
        if (readPos == writePos)
        {
            ++underrunCount;
            outL[i] = outR[i] = 0.0f;
            continue;
        }
        outL[i] = fifoL[readPos & fifoMask];
        outR[i] = fifoR[readPos & fifoMask];
        ++readPos;
    }
}
} // namespace spaceforge::dsp

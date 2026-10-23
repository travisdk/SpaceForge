// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include "analysis/TestSignals.h"
#include "dsp/SpaceEngine.h"

namespace spaceforge::analysis
{
struct RenderOptions
{
    float mix = 1.0f;
    int blockSize = 512;
    double freezeOnSeconds = -1.0;  // < 0: never
    double freezeOffSeconds = -1.0; // < 0: never
};

// Runs a sound through the engine from an empty room, as a host would (block by block).
inline Stereo render (dsp::SpaceEngine& engine, const Stereo& input, double sampleRate, const RenderOptions& options = {})
{
    engine.setMix (options.mix);
    engine.setFreeze (false);
    engine.reset();
    Stereo out { std::vector<float> (input.size()), std::vector<float> (input.size()) };
    const auto freezeOn = (size_t) (options.freezeOnSeconds * sampleRate);
    const auto freezeOff = (size_t) (options.freezeOffSeconds * sampleRate);
    for (size_t pos = 0; pos < input.size(); pos += (size_t) options.blockSize)
    {
        if (options.freezeOnSeconds >= 0.0)
            engine.setFreeze (pos >= freezeOn && (options.freezeOffSeconds < 0.0 || pos < freezeOff));
        const int n = (int) std::min ((size_t) options.blockSize, input.size() - pos);
        engine.process (&input.l[pos], &input.r[pos], &out.l[pos], &out.r[pos], n);
    }
    return out;
}
} // namespace spaceforge::analysis

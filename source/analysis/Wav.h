// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include "analysis/TestSignals.h"

namespace spaceforge::analysis
{
// Writes 24-bit stereo PCM WAV. Returns false if the file cannot be written.
inline bool writeWav (const std::string& path, const Stereo& s, double sampleRate)
{
    std::ofstream out (path, std::ios::binary);
    if (! out)
        return false;
    auto u32 = [&out] (uint32_t v) { for (int i = 0; i < 4; ++i) out.put ((char) ((v >> (8 * i)) & 0xff)); };
    auto u16 = [&out] (uint16_t v) { out.put ((char) (v & 0xff)); out.put ((char) (v >> 8)); };

    const auto rate = (uint32_t) std::lround (sampleRate);
    const auto dataBytes = (uint32_t) (s.size() * 2 * 3);
    out.write ("RIFF", 4);
    u32 (36 + dataBytes);
    out.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (1); // PCM
    u16 (2);
    u32 (rate);
    u32 (rate * 2 * 3);
    u16 (2 * 3);
    u16 (24);
    out.write ("data", 4);
    u32 (dataBytes);
    for (size_t i = 0; i < s.size(); ++i)
        for (const float x : { s.l[i], s.r[i] })
        {
            const auto v = (int32_t) std::lround (std::clamp ((double) x, -1.0, 1.0) * 8388607.0);
            out.put ((char) (v & 0xff));
            out.put ((char) ((v >> 8) & 0xff));
            out.put ((char) ((v >> 16) & 0xff));
        }
    return (bool) out;
}
} // namespace spaceforge::analysis

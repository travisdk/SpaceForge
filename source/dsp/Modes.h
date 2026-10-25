// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "dsp/ModeCount.h"
#include "dsp/SpaceVoicing.h"

namespace spaceforge::dsp
{
// The Modes: one visible choice of character. Each is a fixed setup of the room (Cloud Seed) and
// the sky (Clouds), taken from Cloud Seed's factory programs and Clouds' four modes. The four
// macros keep their roles and directions in every Mode; a Mode only sets Space's range, and
// Space at 50 % is a medium room (about 12 s) in all of them (Henrik's rules, CLAUDE.md).
// Presets never change a Mode's scale, and the dice never changes the Mode.

struct ModeSetup
{
    const char* name;
    const char* description;
    SpaceVoicing voicing;
    // Space in this Mode: Cloud Seed's decay setting runs geometrically from half / decaySpan
    // (0 %) through the calibrated value at 50 % to half * decaySpan (100 %); the sky's feed
    // runs in straight lines (dB) from skyFeedLowDb through the calibrated value at 50 % to
    // skyFeedHighDb, its direct level through skyOutDb at 0, 50 and 100 %.
    double decaySpan;
    double skyFeedLowDb, skyFeedHighDb;
    double skyOutDb[3];
    // Which value the calibration moves to make Space at 50 % a medium room: the sky's feed
    // (the sky carries the tail) or the room's decay (the room carries it).
    bool skyCarries;
};

const ModeSetup& modeSetup (int mode);
// The Mode's voicing with its calibrated levels (ModeTables.h).
SpaceVoicing modeVoicing (int mode);
} // namespace spaceforge::dsp

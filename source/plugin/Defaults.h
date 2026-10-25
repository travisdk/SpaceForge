// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// Parameter ids and defaults shared by the plug-in and the offline tools (no JUCE here).
namespace spaceforge::plugin
{
inline constexpr const char* kSpaceId = "space";
inline constexpr const char* kReflectId = "echo"; // Reflect (called Echo before; the id stays so old projects and presets work)
inline constexpr const char* kDelayId = "delay";         // the Echo knob
inline constexpr const char* kDelayNoteId = "delayNote"; // Echo's fixed note
inline constexpr const char* kDuckId = "duck";
inline constexpr const char* kMotionId = "motion";
inline constexpr const char* kToneId = "tone";
inline constexpr const char* kMixId = "mix";
inline constexpr const char* kModeId = "mode";
inline constexpr float kDefaultMix = 0.4f;
} // namespace spaceforge::plugin

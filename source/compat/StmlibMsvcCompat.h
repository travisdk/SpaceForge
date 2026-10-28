// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// stmlib (Clouds) is written for GCC. Force-included (on MSVC only) in front of Clouds, so its
// sources need no changes. __attribute__((always_inline)) is only an inlining hint on declarations
// that are already inline, so dropping it changes nothing but the hint.
#define __attribute__(x)

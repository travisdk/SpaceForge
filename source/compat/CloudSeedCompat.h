// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// Cloud Seed is written for MSVC. Force-included (on other compilers only) in front of Cloud
// Seed, so its sources need no changes for this.
#include <cmath>
#include <cstdio>

// strcpy_s is MSVC-only. Same contract: copies at most size - 1 characters, always terminates.
#define strcpy_s(dst, size, src) std::snprintf ((dst), (size), "%s", (src))

// std::sinf, std::sqrtf and std::expf are standard C++, but libstdc++ only has them from GCC 14.
#if defined(__GLIBCXX__) && _GLIBCXX_RELEASE < 14
namespace std
{
using ::expf;
using ::sinf;
using ::sqrtf;
} // namespace std
#endif

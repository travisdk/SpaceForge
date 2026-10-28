// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SpaceForge: replaces std::rand() for the start phase of the modulated delay lines and
// allpasses. std::rand() gives a different sequence in every C library (glibc, MSVC), so the same
// plug-in sounded slightly different per platform. A small LCG (the same technique as in
// SubForge) with a fixed start value gives the same phases everywhere, in the same order of
// construction. One sequence per thread: no locks, and the audio thread never touches it.

#pragma once

#include <stdint.h>

namespace Cloudseed
{
	class ModPhaseRandom
	{
	public:
		static constexpr uint32_t DefaultSeed = 0x12345678u;

		// Restarts the sequence: engines built after the same Seed() are identical
		static void Seed(uint32_t seed)
		{
			State() = seed;
		}

		// Next value in [0, 1)
		static float Next()
		{
			uint32_t& s = State();
			s = s * 1664525u + 1013904223u;
			return (float)(s >> 8) / 16777216.0f;
		}

	private:
		static uint32_t& State()
		{
			static thread_local uint32_t state = DefaultSeed;
			return state;
		}
	};
}

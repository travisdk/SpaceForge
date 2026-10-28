// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SpaceForge: replaces std::rand() for the start phase of the modulated delay lines and
// allpasses. std::rand() gives a different sequence in every C library (glibc, MSVC), so the
// same plug-in sounded slightly different per platform.
//
// This is the algorithm behind glibc's random() (the default generator on Linux): an additive
// feedback generator, r[i] = r[i-3] + r[i-31], started from a seed with a small multiplicative
// generator and run in for 310 values. The code is written for SpaceForge from the algorithm
// alone. With seed 1 it gives the same sequence as srand(1) on Linux, so the sound is the same
// as before, on every platform.
//
// No global state: each engine owns a generator, seeds it when it is prepared and hands it to
// the delay lines in a fixed order (see ReverbChannel::SeedModPhases).

#pragma once

#include <stdint.h>

namespace Cloudseed
{
	class ModPhaseRandom
	{
	public:
		// glibc's RAND_MAX, on every platform (MSVC's is 32767)
		static constexpr int Max = 2147483647;

		explicit ModPhaseRandom(uint32_t seed = 1)
		{
			Seed(seed);
		}

		void Seed(uint32_t seed)
		{
			if (seed == 0)
				seed = 1;

			// Start values: the first is the seed, the rest a "minimal standard" generator
			// (16807 * x mod 2^31 - 1, computed without overflow) run from it
			int32_t word = (int32_t)seed;
			r[0] = (uint32_t)word;
			for (int i = 1; i < Degree; i++)
			{
				const int32_t hi = word / 127773;
				const int32_t lo = word % 127773;
				word = 16807 * lo - 2836 * hi;
				if (word < 0)
					word += 2147483647;
				r[i] = (uint32_t)word;
			}

			front = Separation;
			rear = 0;
			for (int i = 0; i < Degree * 10; i++)
				NextInt();
		}

		// Next value in [0, Max]
		int NextInt()
		{
			r[front] += r[rear];
			const int result = (int)(r[front] >> 1);
			if (++front >= Degree)
				front = 0;
			if (++rear >= Degree)
				rear = 0;
			return result;
		}

	private:
		static constexpr int Degree = 31;
		static constexpr int Separation = 3;

		uint32_t r[Degree];
		int front;
		int rear;
	};
}

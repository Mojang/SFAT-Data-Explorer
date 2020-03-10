/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>

namespace SFAT {

	inline bool isPowerOf2(uint32_t value) {
		return ((value & (value - 1)) == 0);
	}

	inline uint32_t smallestPowerOf2GreaterOrEqual(uint32_t value) {
		if (isPowerOf2(value)) {
			return value;
		}

		uint32_t p = 4;
		while (p < value) {
			p <<= 1;
		}

		return p;
	}

	template <int N>
	struct TLog2 {
		enum {
			value = TLog2<N-1>::value + 1,
		};
	};

	template <>
	struct TLog2<1> {
		enum {
			value = 0,
		};
	};

	template <int N>
	struct TSmallestPowerOf2GreaterOrEqual {
		enum {
			v0 = 1 << TLog2<N>::value,
			value = v0 >= N ? v0 : v0 * 2, 
		};
	};

} // namespace SFAT

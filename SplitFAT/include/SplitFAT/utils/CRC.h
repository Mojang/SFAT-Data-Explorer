/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

namespace SFAT {

	class CRC32 {
	public:
		static uint32_t calculate(const void *data, size_t bytesCount, uint32_t crcAccum = 0);

	private:
		static const uint32_t table[256];
	}; //CRC32

	class CRC16 {
	public:
		static uint16_t calculate(const void *data, size_t bytesCount, uint16_t crcAccum = 0);

	private:
		static const uint32_t table[256];
	}; //CRC16

	class CRC24 {
	public:
		static uint32_t calculate(const void *data, size_t bytesCount, uint32_t crcAccum = 0xb704ce);

	private:
		static const uint32_t table[256];
	}; //CRC24

} // namespace SFAT
/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include <SplitFAT/utils/CRC.h>

using namespace SFAT;

// Tests the CRC32.
TEST(CRC32, TestCRC32) {
	// You can access data in the test fixture here.
	const char *szText = "The quick brown fox jumps over the lazy dog";
	uint32_t crc = CRC32::calculate(szText, strlen(szText));
	EXPECT_EQ(crc, 0x414fa339);
}

// Tests the CRC16.
TEST(CRC16, TestCRC16) {
	// You can access data in the test fixture here.
	const char *szText = "The quick brown fox jumps over the lazy dog";
	uint16_t crc = CRC16::calculate(szText, strlen(szText));
	EXPECT_EQ(crc, 0xFCDF);
}

// Tests the CRC24.
TEST(CRC24, TestCRC24) {
	// You can access data in the test fixture here.
	const char *szText = "The quick brown fox jumps over the lazy dog";
	uint32_t crc = CRC24::calculate(szText, strlen(szText));
	EXPECT_EQ(crc, 0xA2618C);
}

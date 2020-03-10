/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include <SplitFAT/utils/BitSet.h>

using namespace SFAT;

// Tests the BitSet.
TEST(BitSet, Constructor) {
	// You can access data in the test fixture here.
	BitSet bitSet;
	EXPECT_EQ(bitSet.getSize(), 0);
	EXPECT_EQ(bitSet.mSize, 0);
	EXPECT_EQ(bitSet.mElements.size(), 0);

	bitSet.setSize(0);
	EXPECT_EQ(bitSet.getSize(), 0);
	EXPECT_EQ(bitSet.mSize, 0);
	EXPECT_EQ(bitSet.mElements.size(), 0);

	BitSet bitSet1(1);
	EXPECT_EQ(bitSet1.getSize(), 1);
	EXPECT_EQ(bitSet1.mSize, 1);
	EXPECT_EQ(bitSet1.mElements.size(), 1);

	BitSet bitSet2(2);
	EXPECT_EQ(bitSet2.getSize(), 2);
	EXPECT_EQ(bitSet2.mSize, 2);
	EXPECT_EQ(bitSet2.mElements.size(), 1);

	BitSet bitSet63(63);
	EXPECT_EQ(bitSet63.getSize(), 63);
	EXPECT_EQ(bitSet63.mSize, 63);
	EXPECT_EQ(bitSet63.mElements.size(), 1);

	BitSet bitSet64(64);
	EXPECT_EQ(bitSet64.getSize(), 64);
	EXPECT_EQ(bitSet64.mSize, 64);
	EXPECT_EQ(bitSet64.mElements.size(), 1);

	BitSet bitSet65(65);
	EXPECT_EQ(bitSet65.getSize(), 65);
	EXPECT_EQ(bitSet65.mSize, 65);
	EXPECT_EQ(bitSet65.mElements.size(), 2);
}

TEST(BitSet, setAll) {
	// You can access data in the test fixture here.
	BitSet bitSet(10);
	bitSet.setAll(false);
	for (size_t i = 0; i < 10; ++i) {
		EXPECT_EQ(bitSet.getValue(i), false);
	}
#ifndef _DEBUG
	EXPECT_EQ(bitSet.getValue(11), false);
#endif

	bitSet.setAll(true);
	for (size_t i = 0; i < 10; ++i) {
		EXPECT_EQ(bitSet.getValue(i), true);
	}
#ifndef _DEBUG
	EXPECT_EQ(bitSet.getValue(11), false);
#endif
}

TEST(BitSet, setValue) {
	// You can access data in the test fixture here.
	BitSet bitSet(20);
	bitSet.setAll(false);
	bitSet.setValue(10, true);
	for (size_t i = 0; i < 10; ++i) {
		EXPECT_EQ(bitSet.getValue(i), false);
	}

	EXPECT_EQ(bitSet.getValue(10), true);

	for (size_t i = 11; i < 20; ++i) {
		EXPECT_EQ(bitSet.getValue(i), false);
	}

	bitSet.setAll(true);
	bitSet.setValue(10, false);
	for (size_t i = 0; i < 10; ++i) {
		EXPECT_EQ(bitSet.getValue(i), true);
	}

	EXPECT_EQ(bitSet.getValue(10), false);

	for (size_t i = 11; i < 20; ++i) {
		EXPECT_EQ(bitSet.getValue(i), true);
	}

#ifndef _DEBUG
	bitSet.setValue(20, true);
	EXPECT_EQ(bitSet.getValue(20), false);
#endif
}


TEST(BitSet, findFirst) {
	BitSet bitSet(127);
	bitSet.setAll(false);
	size_t bitIndexFound = 12345678;
	bool res = bitSet.findFirst(bitIndexFound, true, 0);
	EXPECT_EQ(res, false);
	EXPECT_EQ(bitIndexFound, BitSet::npos);

	bitSet.setValue(10, true);
	res = bitSet.findFirst(bitIndexFound, true, 0);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 10);

	// Test the bits at the start of the range
	{
		bitSet.setAll(false);
		bitSet.setValue(0, true);
		res = bitSet.findFirst(bitIndexFound, true, 0);
		EXPECT_EQ(res, true);
		EXPECT_EQ(bitIndexFound, 0);

		bitSet.setAll(false);
		bitSet.setValue(1, true);
		res = bitSet.findFirst(bitIndexFound, true, 0);
		EXPECT_EQ(res, true);
		EXPECT_EQ(bitIndexFound, 1);
	}

	// Test changing the range
	{
		bitSet.setAll(false);
		bitSet.setValue(0, true);
		bitSet.setValue(5, true);
		res = bitSet.findFirst(bitIndexFound, true, 0);
		EXPECT_EQ(res, true);
		EXPECT_EQ(bitIndexFound, 0);


		bitSet.setAll(false);
		bitSet.setValue(0, true);
		bitSet.setValue(5, true);
		res = bitSet.findFirst(bitIndexFound, true, 1);
		EXPECT_EQ(res, true);
		EXPECT_EQ(bitIndexFound, 5);
	}

	// Test out of range reading
	{
		bitIndexFound = 12345678;
		bitSet.setAll(true); // Note that 128 bits are set to 1 here.
		// Set the first 127 bits that we can access to 0
		for (size_t i = 0; i < 127; ++i) {
			bitSet.setValue(i, false); 
		}
		// We are expected not to find any bit set, even that the 128th bit is 1
		res = bitSet.findFirst(bitIndexFound, true, 0);
		EXPECT_EQ(res, false);
		EXPECT_EQ(bitIndexFound, BitSet::npos);

		// Check if we can find the bit index 126
		bitSet.setValue(126, true);
		res = bitSet.findFirst(bitIndexFound, true, 0);
		EXPECT_EQ(res, true);
		EXPECT_EQ(bitIndexFound, 126);
	}
}

TEST(BitSet, findLast) {
	BitSet bitSet(127);
	bitSet.setAll(false);
	size_t bitIndexFound = 12345678;
	bool res = bitSet.findLast(bitIndexFound, true, 128);
	EXPECT_EQ(res, false);
	EXPECT_EQ(bitIndexFound, BitSet::npos);

	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 127);
	EXPECT_EQ(res, false);
	EXPECT_EQ(bitIndexFound, BitSet::npos);

	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 126);
	EXPECT_EQ(res, false);
	EXPECT_EQ(bitIndexFound, BitSet::npos);

	bitSet.setValue(126, true);
	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 126);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 126);

	bitSet.setValue(0, true);
	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 126);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 126);

	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 125);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 0);

	bitSet.setValue(126, false);
	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 126);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 0);

	bitSet.setValue(1, true);
	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 126);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 1);

	bitIndexFound = 12345678;
	res = bitSet.findLast(bitIndexFound, true, 9999);
	EXPECT_EQ(res, true);
	EXPECT_EQ(bitIndexFound, 1);
}

TEST(BitSet, BooleanOperations) {
	const size_t size = 32768;
	assert(size - 1 <= RAND_MAX);

	BitSet src0(size);
	BitSet src1(size);

	src0.setAll(false);
	src1.setAll(false);
	for (size_t i = 0; i < 500; ++i) {
		src0.setValue(rand() % size, true);
	}
	for (size_t i = 0; i < 500; ++i) {
		src1.setValue(rand() % size, true);
	}
	for (size_t i = 0; i < 33; ++i) {
		int index = rand() % size;
		src0.setValue(index, true);
		src1.setValue(index, true);
	}

	// Test andOp()
	{
		BitSet dest;
		BitSet::andOp(dest, src0, src1);
		EXPECT_EQ(dest.getSize(), size);

		for (size_t j = 0; j < size; ++j) {
			EXPECT_EQ(src0.getValue(j) && src1.getValue(j), dest.getValue(j));
		}
	}

	// Test orOp()
	{
		BitSet dest;
		BitSet::orOp(dest, src0, src1);
		EXPECT_EQ(dest.getSize(), size);

		for (size_t j = 0; j < size; ++j) {
			EXPECT_EQ(src0.getValue(j) || src1.getValue(j), dest.getValue(j));
		}
	}

	// Test xorOp()
	{
		BitSet dest;
		BitSet::xorOp(dest, src0, src1);
		EXPECT_EQ(dest.getSize(), size);

		for (size_t j = 0; j < size; ++j) {
			EXPECT_EQ(src0.getValue(j) ^ src1.getValue(j), dest.getValue(j));
		}
	}
}

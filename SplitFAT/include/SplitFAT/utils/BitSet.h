/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <vector>
#include <stdint.h>

#if !defined(MCPE_PUBLISH)
class BitSet_Constructor_Test;
#endif //!defined(MCPE_PUBLISH)

namespace SFAT {

	class BitSet {
#if !defined(MCPE_PUBLISH)
		friend class BitSet_Constructor_Test;
#endif //!defined(MCPE_PUBLISH)
	public:
		typedef uint64_t ElementType;

	public:
		BitSet();
		BitSet(size_t size);
		BitSet(const BitSet& bitSet);
		BitSet(BitSet&& bitSet);
		BitSet& operator=(const BitSet& bitSet);
		BitSet& operator=(BitSet&& bitSet);
		BitSet& operator|=(const BitSet& bitSet);

		// Changes the size without keeping the previous state of the set.
		void setSize(size_t size);
		void setAll(bool value);
		bool getValue(size_t index) const;
		void setValue(size_t index, bool value);
		// Returns the index of the first bit with value equal to the specified. The search range is [startIndex, mSize)
		bool findFirst(size_t& bitIndexFound, bool valueToLookFor, size_t startIndex) const;
		bool findLast(size_t& bitIndexFound, bool valueToLookFor) const;
		bool findStartOfLastKElements(size_t& startIndexFound, bool valueToLookFor, size_t endIndex, size_t countElements) const;

		// Returns the index of the last bit with value equal to the specified. The search range is [0, endIndex]
		bool findLast(size_t& bitIndexFound, bool valueToLookFor, size_t endIndex) const;
		bool findFirstZero(size_t& bitIndexFound, size_t startIndex = 0) const;
		bool findFirstOne(size_t& bitIndexFound, size_t startIndex = 0) const;
		size_t getSize() const;
		size_t getCountZeros() const;
		size_t getCountOnes() const;
		size_t getCountOnes(size_t firstIndex, size_t countIndices) const;

		static BitSet& xorOp(BitSet& dest, const BitSet& src0, const BitSet& src1);
		static BitSet& orOp(BitSet& dest, const BitSet& src0, const BitSet& src1);
		static BitSet& andOp(BitSet& dest, const BitSet& src0, const BitSet& src1);

		bool anyInRange(size_t startIndex, size_t countElements) const;
		// To be used for verification.
		bool slowAnyInRange(size_t startIndex, size_t countElements) const;

	public:
		static const size_t npos = (size_t)(-1); // bad/missing position
	private:
		size_t mSize;
		std::vector<ElementType> mElements;
	}; // BitSet


} // namespace SFAT
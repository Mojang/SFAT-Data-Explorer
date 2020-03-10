/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/utils/SFATAssert.h"
#include <algorithm>
#include <string.h>

namespace SFAT {

	BitSet::BitSet()
		: mSize(0) {
	}

	BitSet::BitSet(size_t size) {
		setSize(size);
	}

	BitSet::BitSet(const BitSet& bitSet) {
		mElements.resize(bitSet.mElements.size());
		memcpy(mElements.data(), bitSet.mElements.data(), bitSet.mElements.size() * sizeof(ElementType));
		mSize = bitSet.mSize;
	}

	BitSet::BitSet(BitSet&& bitSet) {
		mElements = std::move(bitSet.mElements);
		mSize = bitSet.mSize;
		bitSet.mSize = 0;
	}

	BitSet& BitSet::operator=(const BitSet& bitSet) {
		mElements.resize(bitSet.mElements.size());
		memcpy(mElements.data(), bitSet.mElements.data(), bitSet.mElements.size() * sizeof(ElementType));
		mSize = bitSet.mSize;
		return *this;
	}

	BitSet& BitSet::operator=(BitSet&& bitSet) {
		mElements = std::move(bitSet.mElements);
		mSize = bitSet.mSize;
		bitSet.mSize = 0;
		return *this;
	}

	void BitSet::setSize(size_t size) {
		mSize = size;
		if (mSize == 0) {
			mElements.clear();
		}
		else {
			size_t elementsCount = (mSize-1) / (sizeof(ElementType) * 8) + 1;
			mElements.resize(elementsCount);
		}
	}

	void BitSet::setAll(bool value) {
		ElementType element;
		if (value) {
			element = ~static_cast<ElementType>(0);
		}
		else {
			element = static_cast<ElementType>(0);
		}
		size_t elementsCount = mElements.size();
		for (size_t i = 0; i < elementsCount; ++i) {
			mElements[i] = element;
		}
	}

	bool BitSet::getValue(size_t index) const {
		if (index >= mSize) {
			return false;
		}
		size_t elementIndex = index / (sizeof(ElementType) * 8);
		ElementType bitMask = static_cast<ElementType>(1) << (index % (sizeof(ElementType) * 8));
		bool value = (mElements[elementIndex] & bitMask) != static_cast<ElementType>(0);
		return value;
	}

	void BitSet::setValue(size_t index, bool value) {
		SFAT_ASSERT(index < mSize, "Bit index out of range!");
		if (index >= mSize) {
			return;
		}
		size_t elementIndex = index / (sizeof(ElementType) * 8);
		ElementType bitMask = static_cast<ElementType>(1) << (index % (sizeof(ElementType) * 8));
		if (value) {
			mElements[elementIndex] |= bitMask;
		}
		else {
			mElements[elementIndex] &= ~bitMask;
		}
	}

	bool BitSet::findFirst(size_t& bitIndexFound, bool valueToLookFor, size_t startIndex) const {
		bitIndexFound = npos;
		constexpr size_t bitsCount = sizeof(ElementType) * 8;
		size_t elementsCount = mElements.size();
		size_t startElement = startIndex / bitsCount;
		size_t startBit = startIndex % bitsCount;
		ElementType bitTestValue = 0;
		ElementType allBitsTestValue = ~static_cast<ElementType>(0);
		if (valueToLookFor) {
			bitTestValue = 1;
			allBitsTestValue = 0;
		}
		for (size_t elementIndex = startElement; elementIndex < elementsCount; ++elementIndex) {
			ElementType elementValue = mElements[elementIndex];
			if (elementValue != allBitsTestValue) {
				for (size_t bitIndex = startBit; bitIndex < bitsCount; ++bitIndex) {
					if (bitTestValue == ((elementValue >> bitIndex) & 1)) {
						size_t index = elementIndex * bitsCount + bitIndex;
						if (index >= mSize) {
							return false;
						}
						bitIndexFound = index;
						return true;
					}
				}
			}
			startBit = 0;
		}
		return false;
	}

	bool BitSet::findLast(size_t& bitIndexFound, bool valueToLookFor) const {
		bitIndexFound = npos;
		if (mSize == 0) {
			return false;
		}

		size_t index = mSize;
		do {
			--index;
			if (valueToLookFor == getValue(index)) {
				bitIndexFound = index;
				return true;
			}
		} while (index != 0);

		return false;
	}

	bool BitSet::findStartOfLastKElements(size_t& startIndexFound, bool valueToLookFor, size_t endIndex, size_t countElements) const {
		startIndexFound = npos;
		if ((mSize == 0) || (countElements == 0)) {
			return false;
		}
		if (endIndex >= mSize) {
			endIndex = mSize - 1;
		}

		size_t index = endIndex + 1;
		do {
			--index;
			if (valueToLookFor == getValue(index)) {
				--countElements;
				if (0 == countElements) {
					startIndexFound = index;
					return true;
				}
			}
		} while (index != 0);

		return false;
	}

	bool BitSet::findLast(size_t& bitIndexFound, bool valueToLookFor, size_t endIndex) const {
		bitIndexFound = npos;
		if (mSize == 0) {
			return false;
		}
		if (endIndex >= mSize) {
			endIndex = mSize - 1;
		}

		size_t index = endIndex + 1;
		do {
			--index;
			if (valueToLookFor == getValue(index)) {
				bitIndexFound = index;
				return true;
			}
		} while (index != 0);

		return false;
	}

	bool BitSet::findFirstZero(size_t& bitIndexFound, size_t startIndex) const {
		return findFirst(bitIndexFound, false, startIndex);
	}

	bool BitSet::findFirstOne(size_t& bitIndexFound, size_t startIndex) const {
		return findFirst(bitIndexFound, true, startIndex);
	}

	size_t BitSet::getSize() const {
		return mSize;
	}

	size_t BitSet::getCountZeros() const {
		return mSize - getCountOnes();
	}

	size_t BitSet::getCountOnes() const {
		size_t count = 0;
		for (size_t i = 0; i < mSize; ++i) {
			if (getValue(i)) {
				++count;
			}
		}
		return count;
	}

	size_t BitSet::getCountOnes(size_t firstIndex, size_t countIndices) const {
		size_t count = 0;
		if (mSize == 0) {
			return 0;
		}
		size_t endIndex = firstIndex + countIndices;
		if (endIndex > mSize) {
			endIndex = mSize;
		}
		for (size_t i = firstIndex; i < endIndex; ++i) {
			if (getValue(i)) {
				++count;
			}
		}
		return count;
	}

	BitSet& BitSet::xorOp(BitSet& dest, const BitSet& src0, const BitSet& src1) {
		size_t destSize = std::min(src0.mSize, src1.mSize);
		dest.setSize(destSize);
		size_t elementsCount = dest.mElements.size();
		for (size_t elementIndex = 0; elementIndex < elementsCount; ++elementIndex) {
			dest.mElements[elementIndex] = src0.mElements[elementIndex] ^ src1.mElements[elementIndex];
		}
		return dest;
	}

	BitSet& BitSet::orOp(BitSet& dest, const BitSet& src0, const BitSet& src1) {
		size_t destSize = std::min(src0.mSize, src1.mSize);
		dest.setSize(destSize);
		size_t elementsCount = dest.mElements.size();
		for (size_t elementIndex = 0; elementIndex < elementsCount; ++elementIndex) {
			dest.mElements[elementIndex] = src0.mElements[elementIndex] | src1.mElements[elementIndex];
		}
		return dest;
	}

	BitSet& BitSet::operator|=(const BitSet& bitSet) {
		SFAT_ASSERT(getSize() == bitSet.getSize(), "The size of the bit sets must be same!");
		size_t elementsCount = mElements.size();
		for (size_t elementIndex = 0; elementIndex < elementsCount; ++elementIndex) {
			mElements[elementIndex] |= bitSet.mElements[elementIndex];
		}
		return *this;
	}

	BitSet& BitSet::andOp(BitSet& dest, const BitSet& src0, const BitSet& src1) {
		size_t destSize = std::min(src0.mSize, src1.mSize);
		dest.setSize(destSize);
		size_t elementsCount = dest.mElements.size();
		for (size_t elementIndex = 0; elementIndex < elementsCount; ++elementIndex) {
			dest.mElements[elementIndex] = src0.mElements[elementIndex] & src1.mElements[elementIndex];
		}
		return dest;
	}

	bool BitSet::anyInRange(size_t startIndex, size_t countElements) const {
		if (startIndex >= mSize) {
			return false;
		}
		if (startIndex + countElements >= mSize) {
			countElements = mSize - startIndex;
		}
		if (countElements == 0) {
			return false;
		}
		
		constexpr size_t bitsPerElement = sizeof(ElementType) * 8;
		size_t startElementIndex = startIndex / bitsPerElement;
		size_t endElementIndex = (startIndex + countElements - 1) / bitsPerElement;
		size_t bitPos = startElementIndex % bitsPerElement;
		ElementType startBitMask = static_cast<ElementType>(-1) << bitPos;
		bitPos = endElementIndex % bitsPerElement;
		ElementType endBitMask = static_cast<ElementType>(-1) >> bitPos;

		if (startElementIndex == endElementIndex) {
			bool value = ((mElements[startElementIndex] & (startBitMask & endBitMask)) != 0);
			return value;
		}

		bool value = ((mElements[startElementIndex] & startBitMask) != 0);
		if (value) {
			return true;
		}

		for (size_t i = startIndex + 1; i < endElementIndex; ++i) {
			if (mElements[i] != 0) {
				return true;
			}
		}

		value = ((mElements[endElementIndex] & endBitMask) != 0);
		return value;
	}

	bool BitSet::slowAnyInRange(size_t startIndex, size_t countElements) const {
		for (size_t i = 0; i < countElements; ++i) {
			if (getValue(startIndex + i)) {
				return true;
			}
		}
		return false;
	}


} // namespace SFAT


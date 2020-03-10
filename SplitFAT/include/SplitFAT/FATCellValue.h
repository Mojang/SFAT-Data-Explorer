/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#ifndef _SPLIT_FAT_CELL_VALUE_H_
#define _SPLIT_FAT_CELL_VALUE_H_

#define SPLIT_FAT__ENABLE_CRC_PER_CLUSTER	1

namespace SFAT {

	typedef uint32_t	ClusterIndexType;
	typedef int64_t		FilePositionType;
	typedef uint64_t	FileSizeType;

	/*
		Old version up to 2019/11/07
		==============================

		Encoding of the 64 bits per FAT cell
		We have 4 cases for a cell in the cluster chain - first/last, first only, last only, middle

		 1. First/Last
		=========================

			// FAT Cell value encoding (
			//
			//     |File Desc. Record Idx|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                          |----- Cluster Index where the FileDescriptorRecord is stored -----|
			//  |
			//  Start of chain - SHOULD BE SET

			// FAT Cell value encoding
			//
			//     |File Desc. Record Idx|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                          |-------------------- Cluster Index -------------------------------|
			//  |                          |---------------------- Not used  ---------------------------------|
			//  End of chain - SHOULD BE SET
			//
			//  23 bits not used


		 2. First only
		=========================

			// FAT Cell value encoding (
			//
			//     |File Desc. Record Idx|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                          |----- Cluster Index where the FileDescriptorRecord is stored -----|
			//  |
			//  Start of chain - SHOULD BE SET

			// FAT Cell value encoding
			//
			//     |----- Not used ------|
			//     |File Desc. Record Idx|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                          |------------- Cluster Index of the next in chain -----------------|
			//  |
			//  End of chain - Should be 0
			//
			//  8 bits not used

		 3. Last only
		=========================

			// FAT Cell value encoding (
			//
			//     |----- Not used ------|
			//     |File Desc. Record Idx|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                          |----------- Cluster Index of the previous in chain ---------------|
			//  |
			//  Start of chain - Should be 0
			//
			//  8 bits not used

			// FAT Cell value encoding
			//
			//     |File Desc. Record Idx|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                          |----- Cluster Index where the FileDescriptorRecord is stored -----|
			//  |
			//  End of chain - SHOULD BE SET

		 3. Middle
		=========================

			// FAT Cell value encoding (
			//
			//     |----- Not used ------|
			//     |File Desc. Record Idx|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                          |----------- Cluster Index of the previous in chain ---------------|
			//  |
			//  Start of chain - Should be 0
			//
			//  8 bits not used

			// FAT Cell value encoding
			//
			//     |----- Not used ------|
			//     |File Desc. Record Idx|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                          |------------- Cluster Index of the next in chain -----------------|
			//  |
			//  End of chain - Should be 0
			//
			//  8 bits not used
	*/

	/*
		New version from 2019/11/07
		==============================

		Encoding of the 64 bits per FAT cell
		We have 4 cases for a cell in the cluster chain - first/last, first only, last only, middle

		 1. First/Last
		=========================

			// FAT Cell low 32 bits
			//
			//  1  |File Desc. Record Idx|    | -- Low 8 CRC bits --|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                           ^                         | -- Cluster Index where                |
			//  |                           |                         |  the FileDescriptorRecord is stored --|
			//  |                           |
			//  |                           CRC initialized
			//  |
			//  Start of chain - SHOULD BE SET
			//
			//  0 bits not used

			// FAT Cell high 32 bits
			//
			//  1  |-- 8 bits not used --|    | -- High 8 CRC bits -|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                                                     |-------- 14 bits not used -------------|
			//  |
			//  End of chain - SHOULD BE SET
			//
			//  23 bits not used


		 2. First only
		=========================

			// FAT Cell low 32 bits
			//
			//  1  |File Desc. Record Idx|    | -- Low 8 CRC bits --|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                           ^                         | -- Cluster Index where                |
			//  |                           |                         |  the FileDescriptorRecord is stored --|
			//  |                           |
			//  |                           CRC initialized
			//  |
			//  Start of chain - SHOULD BE SET
			//
			//  0 bits not used

			// FAT Cell high 32 bits
			//
			//  0  | -- High 8 CRC bits -|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                             |---------- Cluster Index of the next in chain -----------------|
			//  |
			//  End of chain - Should be 0
			//
			//  1 bits not used

		 3. Last only
		=========================

			// FAT Cell low 32 bits
			//
			//  0  | -- Low 8 CRC bits --|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                           ^ |-------- Cluster Index of the previous in chain ---------------|
			//  |                           |
			//  |                           CRC initialized
			//  |
			//  Start of chain - Should be 0
			//
			//  0 bits not used

			// FAT Cell high 32 bits
			//
			//  1  |File Desc. Record Idx|    | -- High 8 CRC bits -|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                                                     | -- Cluster Index where                |
			//  |                                                     |  the FileDescriptorRecord is stored --|
			//  |
			//  End of chain - SHOULD BE SET
			//
			//  1 bits not used

		 4. Middle
		=========================

			// FAT Cell low 32 bits
			//
			//  0  | -- Low 8 CRC bits --|
			// [31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00]
			//  ^                           ^ |-------- Cluster Index of the previous in chain ---------------|
			//  |                           |
			//  |                           CRC initialized
			//  |
			//  Start of chain - Should be 0
			//
			//  0 bits not used

			// FAT Cell high 32 bits
			//
			//  0  | -- High 8 CRC bits -|
			// [63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32]
			//  ^                             |---------- Cluster Index of the next in chain -----------------|
			//  |
			//  End of chain - Should be 0
			//
			//  1 bits not used
	*/

	enum ClusterValues : uint32_t {
		ROOT_START_CLUSTER_INDEX = 0, // This cluster index is reserved for the start of the Root directory
		FREE_CLUSTER = ROOT_START_CLUSTER_INDEX, // Note that we can use the index 0 for that, becase the cluster with index 0 is the Root, and nothing is supposed to point to the begining of the Root.
		CLUSTER_INDEX_BITS_COUNT = 22,
		CLUSTER_SHORT_INDEX_BITS_COUNT = 14, // The cluster for the FileDescriptorRecord is represented with only the first 14 bits
		CLUSTER_INDEX_MASK = (1UL << CLUSTER_INDEX_BITS_COUNT) - 1, //22 bits is enough for addressing 32GB with cluster size of 8192 bytes. The final value is 0x3FFFFF
		CLUSTER_SHORT_INDEX_MASK = (1UL << CLUSTER_SHORT_INDEX_BITS_COUNT) - 1, //14 bits is enough for addressing the first 16 blocks of 256MB where the directories are allowed to be stored for Berwick. (Currently only the first block!)
		LAST_CLUSTER_INDEX_VALUE = CLUSTER_INDEX_MASK - 1, //Keep one value to mark it as INVALID.
		START_END_VALUE_FLAG = 1UL << 31,
		INVALID_VALUE = CLUSTER_INDEX_MASK,
		CHAIN_START_END_MASK = START_END_VALUE_FLAG,
		FLAGS_AND_INDEX_MASK = CHAIN_START_END_MASK | CLUSTER_INDEX_MASK,
		FLAGS_AND_SHORT_INDEX_MASK = CHAIN_START_END_MASK | CLUSTER_SHORT_INDEX_MASK,
		START_OF_CHAIN = START_END_VALUE_FLAG,
		END_OF_CHAIN = START_END_VALUE_FLAG,
		FDRI_START_BIT = CLUSTER_INDEX_BITS_COUNT + 1, //File Descriptor Record Index start bit
		HIGH_POSITION_OF_8_CRC_BITS = CLUSTER_INDEX_BITS_COUNT + 1,
		LOW_POSITION_OF_8_CRC_BITS = CLUSTER_SHORT_INDEX_BITS_COUNT,
		FDRI_BITS_COUNT = 8, // FileDescriptorRecord bits. Also need 8 bits for half of the CRC-16, which can be alternatively encoded at the same place.
		FDRI_MASK = ((1 << FDRI_BITS_COUNT) - 1),
		FDRI_SHIFTED_MASK = FDRI_MASK << FDRI_START_BIT,
		CRC_LOW_POSITION_8_BITS_MASK = FDRI_MASK << CLUSTER_SHORT_INDEX_BITS_COUNT,

		//CRC per cluster
		CRC_INITIALIZED_MASK = 1UL << CLUSTER_INDEX_BITS_COUNT, // A single bit mask in mPrev
		CRC_BIT_COUNT = 16,
		CLUSTER_NOT_INITIALIZED = 1UL << CLUSTER_INDEX_BITS_COUNT, // A single bit mask in mNext
	};

	inline bool isValidClusterIndex(ClusterIndexType clusterIndex) {
		return (clusterIndex <= ClusterValues::LAST_CLUSTER_INDEX_VALUE);
	}

	class  FATCellValueType {
	public:
		FATCellValueType()
			: mPrev(0)
			, mNext(0)
		{
		}

		FATCellValueType(ClusterIndexType prevClusterIndex, ClusterIndexType nextClusterIndex)
			: mPrev(prevClusterIndex)
			, mNext(nextClusterIndex)
		{
		}

		inline ClusterIndexType getNext() const {
			if (isEndOfChain()) {
				return mNext & CLUSTER_SHORT_INDEX_MASK;
			}
			return mNext & CLUSTER_INDEX_MASK;
		}

		inline ClusterIndexType getPrev() const {
			if (isStartOfChain()) {
				return mPrev & CLUSTER_SHORT_INDEX_MASK;
			}
			return mPrev & CLUSTER_INDEX_MASK;
		}

		// For unit-testing
		inline ClusterIndexType getRawNext() const {
			return mNext;
		}

		// For unit-testing
		inline ClusterIndexType getRawPrev() const {
			return mPrev;
		}

		inline void setNext(ClusterIndexType value) {
			// Clear the flag for end-of-chain if it is set.
			// Set the next cluster index
			uint16_t crc = decodeCRC();
			mNext = (mNext & (~FLAGS_AND_INDEX_MASK)) | (value & CLUSTER_INDEX_MASK);
			_encodeCRC(crc);
		}

		inline void setPrev(ClusterIndexType value) {
			// Clear the flag for start-of-chain if it is set.
			// Set the next cluster index
			uint16_t crc = decodeCRC();
			mPrev = (mPrev & (~FLAGS_AND_INDEX_MASK)) | (value & CLUSTER_INDEX_MASK);
			_encodeCRC(crc);
		}

		inline void makeEndOfChain() {
			mNext = END_OF_CHAIN;
		}

		inline void makeStartOfChain() {
			mPrev = START_OF_CHAIN;
		}

		// Used just for the first cluster in the chain.
		inline void encodeFileDescriptorLocation(ClusterIndexType descriptorClusterIndex, uint32_t recordIndex) {
			SFAT_ASSERT(isStartOfChain() || isEndOfChain(), "The FileDescriptorLocation can be encoded only in cell value representing either start or end of cluster-chain!");
			if (isStartOfChain()) {
				setPrev(descriptorClusterIndex);
				mPrev = START_OF_CHAIN | (mPrev & (~FDRI_SHIFTED_MASK)) | ((recordIndex & FDRI_MASK) << FDRI_START_BIT);
			}
			else if (isEndOfChain()) {
				setNext(descriptorClusterIndex);
				mNext = END_OF_CHAIN | (mNext & (~FDRI_SHIFTED_MASK)) | ((recordIndex & FDRI_MASK) << FDRI_START_BIT);
			}
		}

		// Used just for the first cluster in the chain.
		inline void decodeFileDescriptorLocation(ClusterIndexType& descriptorClusterIndex, uint32_t& recordIndex) const {
			SFAT_ASSERT(isStartOfChain() || isEndOfChain(), "The FileDescriptorLocation can be encoded only in cell value representing either start or end of cluster-chain!");
			if (isStartOfChain()) {
				descriptorClusterIndex = getPrev();
				recordIndex = (mPrev >> FDRI_START_BIT) & FDRI_MASK;
			}
			else if (isEndOfChain()) {
				descriptorClusterIndex = getNext();
				recordIndex = (mNext >> FDRI_START_BIT) & FDRI_MASK;
			}
		}

		// Used just for the clusters after the first and before the last one.
		inline void _encodeCRC(uint16_t crc) {
			if (isStartOfChain()) {
				mPrev = (mPrev & (~CRC_LOW_POSITION_8_BITS_MASK)) | (static_cast<ClusterIndexType>(crc & 0xFF) << LOW_POSITION_OF_8_CRC_BITS);
			}
			else {
				mPrev = (mPrev & (~FDRI_SHIFTED_MASK)) | (static_cast<ClusterIndexType>(crc & 0xFF) << HIGH_POSITION_OF_8_CRC_BITS);
			}

			if (isEndOfChain()) {
				mNext = (mNext & (~CRC_LOW_POSITION_8_BITS_MASK)) | (static_cast<ClusterIndexType>(crc & 0xFF00) << (LOW_POSITION_OF_8_CRC_BITS - 8));
			}
			else {
				mNext = (mNext & (~FDRI_SHIFTED_MASK)) | (static_cast<ClusterIndexType>(crc & 0xFF00) << (HIGH_POSITION_OF_8_CRC_BITS - 8));
			}
		}

		inline void encodeCRC(uint16_t crc) {
			_encodeCRC(crc);
			mPrev |= CRC_INITIALIZED_MASK;
		}

		// Used just for the clusters after the first and before the last one.
		inline uint16_t decodeCRC() {
			uint16_t crc;
			if (isStartOfChain()) {
				crc = static_cast<uint16_t>(mPrev >> LOW_POSITION_OF_8_CRC_BITS) & 0xFF;
			}
			else {
				crc = static_cast<uint16_t>(mPrev >> HIGH_POSITION_OF_8_CRC_BITS) & 0xFF;
			}

			if (isEndOfChain()) {
				crc |= static_cast<uint16_t>(mNext >> (LOW_POSITION_OF_8_CRC_BITS - 8)) & 0xFF00;
			}
			else {
				crc |= static_cast<uint16_t>(mNext >> (HIGH_POSITION_OF_8_CRC_BITS - 8)) & 0xFF00;
			}

			return crc;
		}

		inline bool isCRCInitialized() const {
			return (mPrev & CRC_INITIALIZED_MASK) != 0;
		}

		inline bool isClusterInitialized() const {
			return (mNext & CLUSTER_NOT_INITIALIZED) != 0;
		}

		inline void setClusterInitialized(bool initialized) {
			// Setting the bit to 0 if the cluster is initialized and 1 if it is not.
			if (initialized) {
				mNext &= (~CLUSTER_NOT_INITIALIZED);
			}
			else {
				mNext |= CLUSTER_NOT_INITIALIZED;
			}
		}

		inline bool isFreeCluster() const {
			// Note that only (value.getNext() == 0), means a free cluster
			// The value.getPrev() could be 0, when this is the second cluster in the chain for the root directory.
			// The first root directory cluster has cluster_index = 0
			return ((mNext & FLAGS_AND_INDEX_MASK) == ClusterValues::FREE_CLUSTER);
		}

		inline bool isEndOfChain() const {
			return ((mNext & CHAIN_START_END_MASK) == ClusterValues::END_OF_CHAIN);
		}

		inline bool isStartOfChain() const {
			return ((mPrev & CHAIN_START_END_MASK) == ClusterValues::START_OF_CHAIN);
		}

		inline bool isValid() const {
			//return ((mNext <= ClusterValues::LAST_CLUSTER_INDEX_VALUE) || isEndOfChain()) &&
			//	((mPrev <= ClusterValues::LAST_CLUSTER_INDEX_VALUE) || isStartOfChain());
			return (getNext() != ClusterValues::INVALID_VALUE) && (getPrev() != ClusterValues::INVALID_VALUE);
		}

		inline static FATCellValueType freeCellValue()
		{
			return FATCellValueType(ClusterValues::FREE_CLUSTER, ClusterValues::FREE_CLUSTER);
		}

		inline static FATCellValueType invalidCellValue()
		{
			return FATCellValueType(ClusterValues::INVALID_VALUE, ClusterValues::INVALID_VALUE);
		}

		inline static FATCellValueType singleElementClusterChainValue()
		{
			return FATCellValueType(ClusterValues::START_OF_CHAIN, ClusterValues::END_OF_CHAIN);
		}

		inline static FATCellValueType badCellValue()
		{
			return FATCellValueType(0xBADC0DE, 0xBADC0DE);
		}

		inline bool operator==(const FATCellValueType& cellValue) const {
			return (cellValue.mPrev == mPrev) && (cellValue.mNext == mNext);
		}

		inline bool operator!=(const FATCellValueType& cellValue) const {
			return (cellValue.mPrev != mPrev) || (cellValue.mNext != mNext);
		}

	private:
		ClusterIndexType mPrev; // Points to the previous cluster in the file (cluster chain)
		ClusterIndexType mNext; // Points to the mNext cluster in the file (cluster chain)
	};

} //namespace SFAT

#endif //_SPLIT_FAT_CELL_VALUE_H_
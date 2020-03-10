/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <set>
#include <memory>

#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/Mutex.h"
#include "SplitFAT/utils/BitSet.h"

#define SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED	0
#define SPLIT_FAT__USE_BITSET	1

namespace SFAT {

	class FileHandle;
	class VolumeDescriptor;
	class VolumeManager;

	class FATBlock
	{
	public:
		FATBlock(VolumeManager& volumeManager, uint32_t blockIndex); // ClusterIndexType startCluster, ClusterIndexType clustersCount);
		FATCellValueType getValue(ClusterIndexType index) const;
		void setValue(ClusterIndexType index, FATCellValueType value);

		// Reads from specific place
		ErrorCode read(FileHandle& file, FilePositionType filePosition);
		// Writes to specific place
		ErrorCode write(FileHandle& file, FilePositionType filePosition) const;

		uint32_t calculateCRC32() const;
		bool tryToFindFreeCluster(ClusterIndexType& newClusterIndex) const;
#if (SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED == 1)
		BlockControlData& getBlockControlData();
#endif
		uint32_t getCountFreeClusters() const;
		bool getFirstFreeClusterIndex(ClusterIndexType& clusterIndex) const;
		//bool getLastFreeClusterIndex(ClusterIndexType& clusterIndex) const;
		ErrorCode flush(FileHandle& file, FilePositionType filePosition);

		//To be used from the transaction
		FATBlockTableType& getTable();
		bool isCacheInSync() const;
		void markOutOfSync();
		const BitSet& getFreeClustersSet() const;

	private:
		const VolumeDescriptor& getVolumeDescriptor() const;

	private:
		const VolumeDescriptor&	mVolumeDescriptor;
		uint32_t			mBlockIndex;
		ClusterIndexType	mStartClusterIndex;
		ClusterIndexType	mEndClusterIndex;
		FATBlockTableType	mTable;
#if (SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED == 1)
		BlockControlData	mBlockControlData;
#endif
#if (SPLIT_FAT__USE_BITSET == 1)
		BitSet	mFreeClustersBitSet;
#else
		std::set<ClusterIndexType> mFreeClustersSet;
#endif
		bool				mIsCacheInSync;
	};

	class FATDataManager
	{
	public:
		FATDataManager(VolumeManager& volumeManager);

		ErrorCode getValue(ClusterIndexType index, FATCellValueType& value);
		ErrorCode setValue(ClusterIndexType index, FATCellValueType value);
		
		ErrorCode allocateFATBlock(uint32_t blockIndex);
		ErrorCode preallocateAllFATDataBlocks();
		ErrorCode preloadAllFATDataBlocks();
		bool canExpand() const;

		ErrorCode flush();

		//TODO: Can be made const function if all FATBlocks are pre-cached! Will also simplify the return value, because the ErrorCode wont be necessary.
		ErrorCode tryFindFreeClusterInAllocatedBlocks(ClusterIndexType& newClusterIndex, bool useFileDataStorage);
		ErrorCode tryFindFreeClusterInBlock(ClusterIndexType& newClusterIndex, uint32_t blockIndex);

		ErrorCode getCountFreeClusters(uint32_t& countFreeClusters);
		ErrorCode getCountFreeClusters(uint32_t& countFreeClusters, uint32_t blockIndex);
		ErrorCode executeOnBlock(uint32_t blockIndex, FATBlockCallbackType callback);
		ErrorCode getMaxCountFreeClustersInABlock(uint32_t& maxFreeClustersInABlock, uint32_t& blockIndexFound, uint32_t blockIndexToSkip);
		const BitSet* getFreeClustersSet(uint32_t blockIndex);
		ClusterIndexType getStartClusterIndex(uint32_t blockIndex) const;

		//For testing purposes only
#if !defined(MCPE_PUBLISH)
		ErrorCode discardCachedChanges();
#endif //!defined(MCPE_PUBLISH)

	private:
		ErrorCode _prepareBlock(uint32_t blockIndex);
		ErrorCode _updateCache(uint32_t blockIndex);

	private:
		const VolumeDescriptor& mVolumeDescriptor;
		std::vector<std::unique_ptr<FATBlock>>	mFATBlocksCache;
		VolumeManager& mVolumeManager;
		SFATMutex	mFATBlockReadWriteMutex;
	};

} // namespace SFAT

/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "WindowsDataPlacementStrategy.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/CRC.h"
#include <algorithm>

namespace SFAT {

	/**************************************************************************
	*	ClusterDataBuffer implementation
	**************************************************************************/

	//ClusterDataBuffer::ClusterDataBuffer(VolumeManager& volumeManager) {
	//	mCountClusters = volumeManager.getVolumeDescriptor().getClustersPerFATBlock();
	//	mClusterSize = volumeManager.getVolumeDescriptor().getClusterSize();
	//	mBufferSize = static_cast<size_t>(mCountClusters) * static_cast<size_t>(mClusterSize);
	//	mNewAllocatedClusters.setSize(mCountClusters);
	//}

	/**************************************************************************
	*	DegradedBlockOptimizationJob implementation
	**************************************************************************/

	DegradedBlockOptimizationJob::DegradedBlockOptimizationJob(uint32_t blockIndex)
	: mBlockIndex(blockIndex)
	, mFinished(false) {
		if (mBlockIndex == static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE)) {
			mFinished = true;
		}
		SFAT_ASSERT(mBlockIndex != static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE), "The selected for optimizing block should be valid!");
	}

	uint32_t DegradedBlockOptimizationJob::getBlockIndex() const {
		return mBlockIndex;
	}


	/**************************************************************************
	*	WindowsDataPlacementStrategy implementation
	**************************************************************************/

	WindowsDataPlacementStrategy::WindowsDataPlacementStrategy(VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem)
		: DataPlacementStrategyBase(volumeManager, virtualFileSystem) {
	}

	ErrorCode WindowsDataPlacementStrategy::prepareForWriteTransaction() {
		mIsActive = false;
		FATDataManager& fatMgr = mVolumeManager.getFATDataManager();

		uint32_t blockToBeOptimizedIndex = static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE);
		ErrorCode err = findBlockForOptimization(blockToBeOptimizedIndex);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Failed during data blocks analysis for defragmentation!");
		}

		mMaxFreeClustersInABlock = 0;
		mBlockIndexFound = static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE);
		err = fatMgr.getMaxCountFreeClustersInABlock(mMaxFreeClustersInABlock, mBlockIndexFound, blockToBeOptimizedIndex);
		mIsActive = ((err == ErrorCode::RESULT_OK) && (mBlockIndexFound != static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE)) && (mMaxFreeClustersInABlock > 0));
		
		if (isValidBlockIndex(blockToBeOptimizedIndex) && (blockToBeOptimizedIndex == mBlockIndexFound)) {
			// Have to drop the optimization job
			if (mOptimizationJob) {
				mOptimizationJob = nullptr;
			}
		}

		return err;
	}

	ErrorCode WindowsDataPlacementStrategy::fixDegradedBlock(uint32_t blockIndex) {
		SFAT_ASSERT(isValidBlockIndex(blockIndex), "The block index should be valid!");
		SFAT_ASSERT(isValidBlockIndex(mBlockIndexFound), "The current block should be valid!");
		SFAT_ASSERT(blockIndex != mBlockIndexFound, "The block that we fix should be different from the current block.");

		FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
		uint32_t countFreeClusters = 0;
		ErrorCode err = fatMgr.getCountFreeClusters(countFreeClusters, mBlockIndexFound);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (countFreeClusters >= mMaxFreeClustersInABlock) {
			// There more free clusters now, so nothing was written, and no defragmentation will be done.
			return ErrorCode::RESULT_OK;
		}
		uint32_t budget = mMaxFreeClustersInABlock - countFreeClusters;
		uint32_t degradedBlockFreeClustersCount = 0;
		err = fatMgr.getCountFreeClusters(degradedBlockFreeClustersCount, blockIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		uint32_t halfBlockClusters = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock() / 2;
		if (degradedBlockFreeClustersCount >= halfBlockClusters) {
			// The block is actually not degraded
			return ErrorCode::RESULT_OK;
		}

		const BitSet* srcFreeClustersSet = fatMgr.getFreeClustersSet(blockIndex);
		const BitSet* destFreeClustersSet = fatMgr.getFreeClustersSet(mBlockIndexFound);
		if ((nullptr == srcFreeClustersSet) || (nullptr == destFreeClustersSet)) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Defragmentation failed!");
			return ErrorCode::ERROR_FAT_NOT_CACHED;
		}

		uint32_t countClustersToMove = std::min(budget, halfBlockClusters - degradedBlockFreeClustersCount);
		ClusterIndexType srcCluster = ClusterValues::INVALID_VALUE;
		ClusterIndexType destCluster = ClusterValues::INVALID_VALUE;
		size_t srcIndex = 0;
		size_t destIndex = 0;
		for (uint32_t i = 0; i < countClustersToMove; ++i) {
			if (!srcFreeClustersSet->findFirst(srcIndex, false, srcIndex)) {
				// Nothing more to be moved
				SFAT_LOGW(LogArea::LA_PHYSICAL_DISK, "Miscalculated the count of clusters to be moved!");
				break;
			}
			if (!destFreeClustersSet->findFirst(destIndex, true, destIndex)) {
				// No place to be moved
				SFAT_LOGW(LogArea::LA_PHYSICAL_DISK, "Miscalculated the count of clusters to be moved!");
				break;
			}
			srcCluster = fatMgr.getStartClusterIndex(blockIndex) + static_cast<ClusterIndexType>(srcIndex);
			destCluster = fatMgr.getStartClusterIndex(mBlockIndexFound) + static_cast<ClusterIndexType>(destIndex);
			// Move the cluster
			err = moveCluster(srcCluster, destCluster);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Defragmentation failed!");
				return err;
			}
			++srcIndex;
			++destIndex;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsDataPlacementStrategy::performDefragmentaionOnTransactionEnd() {
		if (mOptimizationJob) {
			fixDegradedBlock(mOptimizationJob->getBlockIndex());
		}
		return ErrorCode::RESULT_OK;
	}

	uint32_t WindowsDataPlacementStrategy::getSelectedBlockIndex() const {
		return mBlockIndexFound;
	}

	ErrorCode WindowsDataPlacementStrategy::defragmentFullBlock(uint32_t blockIndex) {
		(void)blockIndex; // Not used parameter
		return ErrorCode::RESULT_OK;
	}

	uint32_t WindowsDataPlacementStrategy::getCountClustersWritten() const {
		//TODO: Implement it
		return 0;
	}

	//static
	float WindowsDataPlacementStrategy::calculateDegradationScore(const FATBlockTableType& table) {
		uint32_t degradationScore = 0UL;
		uint32_t countIntervals = 0UL;
		size_t size = table.size();
		bool lastWasOccupied = false;
		for (size_t i = 0; i < size; ++i) {
			bool currentIsFree = table[i].isFreeCluster();
			if (lastWasOccupied && currentIsFree) {
				degradationScore += static_cast<uint32_t>(i);
				++countIntervals;
			}
			lastWasOccupied = !currentIsFree;
		}
		if (countIntervals > 0) {
			// Calculates an average from the starts of the free cluster intervals.
			return static_cast<float>(degradationScore) / static_cast<float>(countIntervals);
		}
		return 0.0f;
	}

	ErrorCode WindowsDataPlacementStrategy::findBlockForOptimization(uint32_t& outBlockIndex) {
		FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
		uint32_t countBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		uint32_t halfBlockClusters = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock() / 2;

		if (mOptimizationJob) {
			// We have an old optimization job started.
			// Update the degradation estimate
			uint32_t countFreeClusters;
			outBlockIndex = mOptimizationJob->getBlockIndex();
			ErrorCode err = fatMgr.getCountFreeClusters(countFreeClusters, outBlockIndex);
			if ((err != ErrorCode::RESULT_OK) || (countFreeClusters >= halfBlockClusters)) {
				// In case of error, we would like to remove this job.
				// Alternatively, the selected block should be fine for other type of optimization,
				// becase more than half of it is already empty.
				mOptimizationJob = nullptr;
			}

			// No need to select different block.
			return err;
		}

		float highesDegradationScore = 0.0f;
		uint32_t degradationBlockIndex = static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE);
		uint32_t startBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();

		for (uint32_t localBlockIndex = startBlockIndex; localBlockIndex < countBlocks; ++localBlockIndex) {
			uint32_t countFreeClusters;
			ErrorCode err = fatMgr.getCountFreeClusters(countFreeClusters, localBlockIndex);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			if (countFreeClusters >= halfBlockClusters) {
				// The current block should be fine for other type of optimization, becase more than half of it is empty.
				// So skipping it
				continue;
			}

			float degradationScore = 0.0f;
			err = fatMgr.executeOnBlock(localBlockIndex, [&degradationScore](uint32_t blockIndex, FATBlockTableType& table, bool& wasChanged)->ErrorCode {
				(void)blockIndex; // Not used parameter
				degradationScore = calculateDegradationScore(table);
				wasChanged = false;
				return ErrorCode::RESULT_OK;
			});
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			// Normalize the degradationScore
			degradationScore /= static_cast<float>(halfBlockClusters);
			// Update the highest...
			if (degradationScore > highesDegradationScore) {
				degradationBlockIndex = localBlockIndex;
				highesDegradationScore = degradationScore;
			}
		}

		if (isValidBlockIndex(degradationBlockIndex)) {
			mOptimizationJob = std::make_unique<DegradedBlockOptimizationJob>(degradationBlockIndex);
			outBlockIndex = degradationBlockIndex;
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsDataPlacementStrategy::findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage) {
		if (isActive() && useFileDataStorage) {
			// Try first to find a free cluster in the currently selected block
			uint32_t selectedBlockIndex = getSelectedBlockIndex();
			SFAT_ASSERT(selectedBlockIndex >= mVolumeManager.getFirstFileDataBlockIndex(), "The selectedBlockIndex doesn't correspond to a file-data block!");
			ClusterIndexType freeClusterIndex = ClusterValues::INVALID_VALUE;
			ErrorCode err = mVolumeManager.getFATDataManager().tryFindFreeClusterInBlock(freeClusterIndex, selectedBlockIndex);
			if (err == ErrorCode::RESULT_OK) {
				if (freeClusterIndex <= ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
					//Found a free cluster
					newClusterIndex = freeClusterIndex;
					return ErrorCode::RESULT_OK;
				}
				//Everything is correct with the block, but it is full.
				//Time to switch the block.
				//Before switching a defragmentation could be done.
				err = defragmentFullBlock(selectedBlockIndex);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
			}
			// Otherwise we search in the rest of the blocks
		}
		return mVolumeManager.findFreeCluster(newClusterIndex, useFileDataStorage);
	}

	//ErrorCode onNewClusterAllocated(ClusterIndexType newClusterIndex, bool useFileDataStorage) {
	//	return ErrorCode::RESULT_OK;
	//}

} // namespace SFAT

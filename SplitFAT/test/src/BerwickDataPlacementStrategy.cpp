/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#if defined(MCPE_PLATFORM_ORBIS)

#include "Core/Platform/orbis/file/sfat/BerwickDataPlacementStrategy.h"
#include "Core/Debug/DebugUtils.h"
#include "Core/Debug/Log.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/CRC.h"
#include <algorithm>

#else

#include "BerwickDataPlacementStrategy.h"

#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/CRC.h"
#include <algorithm>

#endif

namespace Core {
	namespace SFAT {

	/**************************************************************************
	*	BerwickDataPlacementStrategy implementation
	**************************************************************************/

	BerwickDataPlacementStrategy::BerwickDataPlacementStrategy(::SFAT::VolumeManager& volumeManager, ::SFAT::VirtualFileSystem& virtualFileSystem)
		: DataPlacementStrategyBase(volumeManager, virtualFileSystem)
		, mIndexOfDegradedBlock(static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE)) {
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::prepareForWriteTransaction() {
		mIsActive = false;
		::SFAT::FATDataManager& fatMgr = mVolumeManager.getFATDataManager();

		::SFAT::ErrorCode err;
		uint32_t blockToBeOptimizedIndex = static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE);
#if (SPLITFAT_ENABLE_DEFRAGMENTATION == 1)	
		err = findBlockForOptimization(blockToBeOptimizedIndex);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Failed during data blocks analysis for defragmentation!");
		}
#endif // SPLITFAT_ENABLE_DEFRAGMENTATION
		mMaxFreeClustersInABlock = 0;
		mBlockIndexFound = static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE);
		err = fatMgr.getMaxCountFreeClustersInABlock(mMaxFreeClustersInABlock, mBlockIndexFound, blockToBeOptimizedIndex);
		mIsActive = ((err == ::SFAT::ErrorCode::RESULT_OK) && (mBlockIndexFound != static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE)) && (mMaxFreeClustersInABlock > 0));

#if (SPLITFAT_ENABLE_DEFRAGMENTATION == 1)	
		if (::SFAT::isValidBlockIndex(blockToBeOptimizedIndex) && (blockToBeOptimizedIndex == mBlockIndexFound)) {
			// Have to drop the optimization job
			mIndexOfDegradedBlock = static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE);
		}
#endif // SPLITFAT_ENABLE_DEFRAGMENTATION

		return err;
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::fixDegradedBlock(uint32_t blockIndex) {
		DEBUG_ASSERT(::SFAT::isValidBlockIndex(blockIndex), "The block index should be valid!");
		DEBUG_ASSERT(::SFAT::isValidBlockIndex(mBlockIndexFound), "The current block should be valid!");
		DEBUG_ASSERT(blockIndex != mBlockIndexFound, "The block that we fix should be different from the current block.");

		::SFAT::FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
		uint32_t countFreeClusters = 0;
		::SFAT::ErrorCode err = fatMgr.getCountFreeClusters(countFreeClusters, mBlockIndexFound);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}
		if (countFreeClusters >= mMaxFreeClustersInABlock) {
			// There are more free clusters now, so no defragmentation will be done.
			return ::SFAT::ErrorCode::RESULT_OK;
		}
		uint32_t budget = mMaxFreeClustersInABlock - countFreeClusters; // This just gives how many clusters were allocated in this block during the transaction.
		uint32_t degradedBlockFreeClustersCount = 0;
		err = fatMgr.getCountFreeClusters(degradedBlockFreeClustersCount, blockIndex);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		uint32_t halfBlockClusters = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock() / 2;
		if (degradedBlockFreeClustersCount >= halfBlockClusters) {
			// The block is actually not degraded.
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		const ::SFAT::BitSet* srcFreeClustersSet = fatMgr.getFreeClustersSet(blockIndex);
		const ::SFAT::BitSet* destFreeClustersSet = fatMgr.getFreeClustersSet(mBlockIndexFound);
		if ((nullptr == srcFreeClustersSet) || (nullptr == destFreeClustersSet)) {
			ALOGE(LOG_AREA_FILE, "Defragmentation failed (Fixing block's performance)! FAT not cached!");
			return ::SFAT::ErrorCode::ERROR_FAT_NOT_CACHED;
		}

		uint32_t countClustersToMove = std::min(budget, halfBlockClusters - degradedBlockFreeClustersCount);
		::SFAT::ClusterIndexType srcCluster = ::SFAT::ClusterValues::INVALID_VALUE;
		::SFAT::ClusterIndexType destCluster = ::SFAT::ClusterValues::INVALID_VALUE;
		size_t srcIndex = 0;
		size_t destIndex = 0;
		if (!srcFreeClustersSet->findStartOfLastKElements(srcIndex, false, srcFreeClustersSet->getSize(), countClustersToMove)) {
			// If can't find the specified amount of clusters to move, start looking from index 0
			srcIndex = 0;
		}
		for (uint32_t i = 0; i < countClustersToMove; ++i) {
			if (!srcFreeClustersSet->findFirst(srcIndex, false, srcIndex)) {
				// Nothing more to be moved
				break;
			}
			if (!destFreeClustersSet->findFirst(destIndex, true, destIndex)) {
				// No place to be moved
				break;
			}
			srcCluster = fatMgr.getStartClusterIndex(blockIndex) + static_cast<::SFAT::ClusterIndexType>(srcIndex);
			destCluster = fatMgr.getStartClusterIndex(mBlockIndexFound) + static_cast<::SFAT::ClusterIndexType>(destIndex);
			// Move the cluster
			err = moveCluster(srcCluster, destCluster);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_FILE, "Defragmentation failed! Cluster movement failed (from %8u to %8u)!", srcCluster, destCluster);
				return err;
			}
			++srcIndex;
			++destIndex;
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::performDefragmentaionOnTransactionEnd() {
		// Optimize the cached data block.
		::SFAT::ErrorCode err = mVolumeManager.getLowLevelFileAccess().defragmentationOnTransactionEnd();
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Defragmentation failed on transaction end! ErrorCode: %04u", err);
		}

		if (static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE) != mIndexOfDegradedBlock) {
#if (SPLITFAT_ENABLE_FIXING_DEGRADED_BLOCK == 1)
			err = fixDegradedBlock(mIndexOfDegradedBlock);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_FILE, "Fixing degraded data block failed! ErrorCode: %04u", err);
			}
#endif //(SPLITFAT_ENABLE_FIXING_DEGRADED_BLOCK == 1)
		}
		
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	uint32_t BerwickDataPlacementStrategy::getSelectedBlockIndex() const {
		return mBlockIndexFound;
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::defragmentFullBlock(uint32_t blockIndex) {
		UNUSED_PARAMETER(blockIndex);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	uint32_t BerwickDataPlacementStrategy::getCountClustersWritten() const {
		//TODO: Implement it
		return 0;
	}

	//static
	float BerwickDataPlacementStrategy::calculateDegradationScore(const ::SFAT::FATBlockTableType& table) {
		uint32_t degradationScore = 0UL;
		uint32_t countIntervals = 0UL;
		size_t size = table.size();
		if (size == 0) {
			return 0.0f;
		}
		bool previousIsOccupied = false;
		size_t i = size;
		do {
			--i;
			bool currentIsFree = table[i].isFreeCluster();
			if (previousIsOccupied && currentIsFree) {
				degradationScore += static_cast<uint32_t>(size - i);
				++countIntervals;
			}
			previousIsOccupied = !currentIsFree;
		} while (i > 0);
		if (countIntervals > 0) {
			// Calculates an average from the starts of the free cluster intervals.
			return static_cast<float>(degradationScore) / static_cast<float>(countIntervals);
		}
		return 0.0f;
	}

	// TODO: Optimize! Organize the use of this function (or the analysis part of it) 
	//  the way that it is called once per block when the block is finally saved.
	//  The result of the analysis should be kept per block.
	::SFAT::ErrorCode BerwickDataPlacementStrategy::findBlockForOptimization(uint32_t& outBlockIndex) {
		::SFAT::FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
		uint32_t countBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		uint32_t halfBlockClusters = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock() / 2;

		if (static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE) != mIndexOfDegradedBlock) {
			// We have an old optimization job started.
			// Update the degradation estimate
			uint32_t countFreeClusters;
			outBlockIndex = mIndexOfDegradedBlock;
			::SFAT::ErrorCode err = fatMgr.getCountFreeClusters(countFreeClusters, outBlockIndex);
			if ((err != ::SFAT::ErrorCode::RESULT_OK) || (countFreeClusters >= halfBlockClusters)) {
				// In case of error, we would like to remove this job.
				// Alternatively, the selected block should be fine for other type of optimization,
				// becase more than half of it is already empty.
				mIndexOfDegradedBlock = static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE);
			}

			// No need to select different block.
			return err;
		}

		float highesDegradationScore = 0.0f;
		uint32_t degradedBlockIndex = static_cast<uint32_t>(::SFAT::BlockIndexValues::INVALID_VALUE);
		uint32_t startBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();

		for (uint32_t localBlockIndex = startBlockIndex; localBlockIndex < countBlocks; ++localBlockIndex) {
			uint32_t countFreeClusters;
			::SFAT::ErrorCode err = fatMgr.getCountFreeClusters(countFreeClusters, localBlockIndex);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}

			if (countFreeClusters >= halfBlockClusters) {
				// The current block should be fine for other type of optimization, becase more than half of it is empty.
				// So skipping it
				continue;
			}

			float degradationScore = 0.0f;
			err = fatMgr.executeOnBlock(localBlockIndex, [&degradationScore](uint32_t blockIndex, ::SFAT::FATBlockTableType& table, bool& wasChanged)->::SFAT::ErrorCode {
				UNUSED_PARAMETER(blockIndex);
				degradationScore = calculateDegradationScore(table);
				wasChanged = false;
				return ::SFAT::ErrorCode::RESULT_OK;
			});
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}

			// Normalize the degradationScore
			degradationScore /= static_cast<float>(halfBlockClusters);
			// Update the highest...
			if (degradationScore > highesDegradationScore) {
				degradedBlockIndex = localBlockIndex;
				highesDegradationScore = degradationScore;
			}
		}

		if (::SFAT::isValidBlockIndex(degradedBlockIndex)) {
			mIndexOfDegradedBlock = degradedBlockIndex;
			outBlockIndex = degradedBlockIndex;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::findFreeCluster(::SFAT::ClusterIndexType& newClusterIndex, bool useFileDataStorage) {
		if (isActive() && useFileDataStorage) {
			// Try first to find a free cluster in the currently selected block
			uint32_t selectedBlockIndex = getSelectedBlockIndex();
			DEBUG_ASSERT(selectedBlockIndex >= mVolumeManager.getFirstFileDataBlockIndex(), "The selectedBlockIndex doesn't correspond to a file-data block!");
			::SFAT::ClusterIndexType freeClusterIndex = ::SFAT::ClusterValues::INVALID_VALUE;
			::SFAT::ErrorCode err = mVolumeManager.getFATDataManager().tryFindFreeClusterInBlock(freeClusterIndex, selectedBlockIndex);
			if (err == ::SFAT::ErrorCode::RESULT_OK) {
				if (freeClusterIndex <= ::SFAT::ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
					//Found a free cluster
					newClusterIndex = freeClusterIndex;
					return ::SFAT::ErrorCode::RESULT_OK;
				}
				//Everything is correct with the block, but it is full.
				//Time to switch the block.
				//Before switching a defragmentation could be done.
				err = defragmentFullBlock(selectedBlockIndex);
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					return err;
				}
			}
			// Otherwise we search in the rest of the blocks
		}
		return mVolumeManager.findFreeCluster(newClusterIndex, useFileDataStorage);
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::optimizeBlockContentConservative(uint32_t blockIndex, uint32_t lastChangedChunkIndex, const ::SFAT::BitSet& initialFreeClustersSet) {
		UNUSED1(lastChangedChunkIndex);
		if (!mVolumeManager.isInTransaction()) {
			// The block optimization is performed only for operations with an active transaction.
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		SFAT_ASSERT(::SFAT::isValidBlockIndex(blockIndex), "The block index should be valid!");
		uint32_t clustersPerBlock = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock();
		uint32_t firstFileDataBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();

		::SFAT::BitSet finalFreeClustersSet;
		::SFAT::ErrorCode err = copyFreeClustersBitSet(finalFreeClustersSet, blockIndex + firstFileDataBlockIndex);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}
		::SFAT::ClusterIndexType clucterIndexOffset = (blockIndex + firstFileDataBlockIndex)*clustersPerBlock;

		::SFAT::BitSet safeToUseFreeClusters;
		::SFAT::BitSet::andOp(safeToUseFreeClusters, finalFreeClustersSet, initialFreeClustersSet);
		uint32_t countOccupiedClusters = static_cast<uint32_t>(safeToUseFreeClusters.getCountZeros());
		::SFAT::ClusterIndexType srcCluster = ::SFAT::ClusterValues::INVALID_VALUE;
		::SFAT::ClusterIndexType destCluster = ::SFAT::ClusterValues::INVALID_VALUE;
		uint32_t countMovedClusters = 0;
		if ((countOccupiedClusters > 0) && (countOccupiedClusters < clustersPerBlock)) {
			int destIndex = 0;
			int srcIndex = countOccupiedClusters; // We can only move clusters that are after this point
			// Iterate now through all clusters
			while (destIndex < countOccupiedClusters) {
				// Find a destination cluster
				//	- Was free before the transaction start ==> initialFreeClustersSet.getValue(static_cast<size_t>(destIndex)) == true
				//	- Is currently free ==> finalFreeClustersSet.getValue(static_cast<size_t>(destIndex))) == true
				do {
					if (safeToUseFreeClusters.getValue(static_cast<size_t>(destIndex))) {
						// Found a destination free cluster.
						break;
					}
					++destIndex;
				} while (destIndex < countOccupiedClusters);

				DEBUG_ASSERT((destIndex >= countOccupiedClusters) || finalFreeClustersSet.getValue(static_cast<size_t>(destIndex)), "If the destIndex is valid, it should point to a free cluster!");
				DEBUG_ASSERT((destIndex >= countOccupiedClusters) || initialFreeClustersSet.getValue(static_cast<size_t>(destIndex)), "We should not override a cluster that was occupied before the transaction start!");

				if (srcIndex <= destIndex) {
					// We move clusters only from higher indices to lower indices.
					srcIndex = destIndex + 1;
				}

				// Find a source cluster to be moved. Stop only on clusters that satisfy all of the conditions:
				//  - Are currently not free ==> !finalFreeClustersSet.getValue(static_cast<size_t>(srcIndex)
				//  - Were either free before, or the following is true (srcIndex >= countOccupiedClusters)
				//		==> (initialFreeClustersSet.getValue(static_cast<size_t>(srcIndex)) || (srcIndex >= countOccupiedClusters))
				while (srcIndex < clustersPerBlock) {
					if (!finalFreeClustersSet.getValue(static_cast<size_t>(srcIndex)) &&
						(initialFreeClustersSet.getValue(static_cast<size_t>(srcIndex)))) {
						break;
					}
					++srcIndex;
				}

				DEBUG_ASSERT((srcIndex >= clustersPerBlock) || !finalFreeClustersSet.getValue(static_cast<size_t>(srcIndex)), "If srcIndex is valid, it should point to an occupied cluster!");

				if ((srcIndex < clustersPerBlock) && (destIndex < countOccupiedClusters)) {
					// Move from srcCluster to destCluster
					srcCluster = clucterIndexOffset + static_cast<::SFAT::ClusterIndexType>(srcIndex);
					destCluster = clucterIndexOffset + static_cast<::SFAT::ClusterIndexType>(destIndex);
					// Move the cluster
					err = moveCluster(srcCluster, destCluster);
					if (err != ::SFAT::ErrorCode::RESULT_OK) {
						ALOGE(LOG_AREA_PLATFORM, "Defragmentation failed (Block optimization)! Error code %4u", err);
						return err;
					}
					finalFreeClustersSet.setValue(srcIndex, true);
					//Note! The following operation is redundant, so we skip it: finalFreeClustersSet.setValue(destIndex, false);
					++destIndex;
					++srcIndex;
					++countMovedClusters;
				}
			}
		}

		ALOGI(LOG_AREA_FILE, "Count moved clusters: %u", countMovedClusters);

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickDataPlacementStrategy::optimizeBlockContent(uint32_t blockIndex, uint32_t lastChangedChunkIndex, const ::SFAT::BitSet& initialFreeClustersSet) {
		UNUSED2(lastChangedChunkIndex, initialFreeClustersSet);
		if (!mVolumeManager.isInTransaction()) {
			// The block optimization is performed only for operations with an active transaction.
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		SFAT_ASSERT(::SFAT::isValidBlockIndex(blockIndex), "The block index should be valid!");
		uint32_t clustersPerBlock = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock();
		uint32_t firstFileDataBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();

		::SFAT::BitSet finalFreeClustersSet;
		::SFAT::ErrorCode err = copyFreeClustersBitSet(finalFreeClustersSet, blockIndex + firstFileDataBlockIndex);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}
		::SFAT::ClusterIndexType clucterIndexOffset = (blockIndex + firstFileDataBlockIndex)*clustersPerBlock;

		uint32_t countOccupiedClusters = static_cast<uint32_t>(finalFreeClustersSet.getCountZeros());
		::SFAT::ClusterIndexType srcCluster = ::SFAT::ClusterValues::INVALID_VALUE;
		::SFAT::ClusterIndexType destCluster = ::SFAT::ClusterValues::INVALID_VALUE;
		uint32_t countMovedClusters = 0;
		if ((countOccupiedClusters > 0) && (countOccupiedClusters < clustersPerBlock)) {
			int destIndex = 0;
			int srcIndex = countOccupiedClusters; // We can only move clusters that are after this point
			// Iterate now through all clusters
			while (destIndex < countOccupiedClusters) {
				// Find a destination cluster
				//	- Is currently free ==> finalFreeClustersSet.getValue(static_cast<size_t>(destIndex))) == true
				do {
					if (finalFreeClustersSet.getValue(static_cast<size_t>(destIndex))) {
						// Found a destination free cluster.
						break;
					}
					++destIndex;
				} while (destIndex < countOccupiedClusters);

				if (destIndex >= countOccupiedClusters) {
					// All occupied cluster should be moved at this point
					break;
				}

				DEBUG_ASSERT(finalFreeClustersSet.getValue(static_cast<size_t>(destIndex)), "If the destIndex is valid, it should point to a free cluster!");

				if (srcIndex <= destIndex) {
					// We move clusters only from higher indices to lower indices.
					srcIndex = destIndex + 1;
				}

				// Find a source cluster to be moved. Stop only on clusters that satisfy all of the conditions:
				//  - Are currently not free ==> !finalFreeClustersSet.getValue(static_cast<size_t>(srcIndex)
				while (srcIndex < clustersPerBlock) {
					if (!finalFreeClustersSet.getValue(static_cast<size_t>(srcIndex))) {
						break;
					}
					++srcIndex;
				}

				if (srcIndex >= clustersPerBlock) {
					// No more clusters to be moved
					break;
				}

				DEBUG_ASSERT(!finalFreeClustersSet.getValue(static_cast<size_t>(srcIndex)), "The srcIndex should point to an occupied cluster!");

				if (destIndex < countOccupiedClusters) {
					// Move from srcCluster to destCluster
					srcCluster = clucterIndexOffset + static_cast<::SFAT::ClusterIndexType>(srcIndex);
					destCluster = clucterIndexOffset + static_cast<::SFAT::ClusterIndexType>(destIndex);
					// Move the cluster
					err = moveCluster(srcCluster, destCluster);
					if (err != ::SFAT::ErrorCode::RESULT_OK) {
						ALOGE(LOG_AREA_PLATFORM, "Defragmentation failed (Block optimization)! Error code %4u", err);
						return err;
					}
					finalFreeClustersSet.setValue(srcIndex, true);
					finalFreeClustersSet.setValue(destIndex, false);
					++destIndex;
					++srcIndex;
					++countMovedClusters;
				}
			}
		}

		ALOGI(LOG_AREA_FILE, "Count moved clusters: %u", countMovedClusters);

		return ::SFAT::ErrorCode::RESULT_OK;
	}

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

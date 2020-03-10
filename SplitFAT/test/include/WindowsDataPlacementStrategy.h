/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <memory>

#include "SplitFAT/LowLevelAccess.h"
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/DataPlacementStrategyBase.h"

///Unit-test classes forward declaration

namespace SFAT {

	class VolumeManager;

	// The idea of using this class is to keep the current state of the optimization job.
	// Especially if there is some data as part of the block analysis that could be cached among several transactions.
	class DegradedBlockOptimizationJob {
	public:
		DegradedBlockOptimizationJob(uint32_t blockIndex);
		uint32_t getBlockIndex() const;

	private:
		uint32_t mBlockIndex; // An index of a block that we will optimize
		bool	 mFinished;
	};

	class WindowsDataPlacementStrategy : public DataPlacementStrategyBase {
	public:
		WindowsDataPlacementStrategy(VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem);
		virtual ErrorCode prepareForWriteTransaction() override;
		virtual ErrorCode performDefragmentaionOnTransactionEnd() override;
		virtual ErrorCode findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage) override;

	private:
		static float calculateDegradationScore(const FATBlockTableType& table);

		uint32_t	getSelectedBlockIndex() const;
		ErrorCode	findBlockForOptimization(uint32_t& blockIndex);
		uint32_t	getCountClustersWritten() const;
		ErrorCode	defragmentFullBlock(uint32_t blockIndex);
		ErrorCode	fixDegradedBlock(uint32_t blockIndex);

	private:
		uint32_t mMaxFreeClustersInABlock;
		uint32_t mBlockIndexFound;
		std::unique_ptr<DegradedBlockOptimizationJob> mOptimizationJob;
	};

} // namespace SFAT

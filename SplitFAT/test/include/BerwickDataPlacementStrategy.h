/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#if defined(MCPE_PLATFORM_ORBIS)

#include <stdint.h>
#include <memory>

#include "SplitFAT/LowLevelAccess.h"
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/DataPlacementStrategyBase.h"

#else

#include "BerwickToWindowsPort.h"

#include <stdint.h>
#include <memory>

#include "SplitFAT/LowLevelAccess.h"
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/DataPlacementStrategyBase.h"

#endif

#define SPLITFAT_ENABLE_DEFRAGMENTATION	1

///Unit-test classes forward declaration

namespace Core { namespace SFAT {

	class VolumeManager;

	class BerwickDataPlacementStrategy : public ::SFAT::DataPlacementStrategyBase {
	public:
		BerwickDataPlacementStrategy(::SFAT::VolumeManager& volumeManager, ::SFAT::VirtualFileSystem& virtualFileSystem);
		virtual ::SFAT::ErrorCode prepareForWriteTransaction() override;
		virtual ::SFAT::ErrorCode performDefragmentaionOnTransactionEnd() override;
		virtual ::SFAT::ErrorCode findFreeCluster(::SFAT::ClusterIndexType& newClusterIndex, bool useFileDataStorage) override;

		// Moves all content as close as possible toward the beginning of the block.
		// Follows the following rules:
		//	- Uses only free cluster spaces that were free before the transaction start and remained free at the end of the transaction.
		//		This prevents overwriting data that we may need to recover if the transaction is interrupted.
		//	- Similarly, while moving clusters, should not write over clusters that were occupied before the transaction,
		//		even if they are also moved or deleted.
		::SFAT::ErrorCode optimizeBlockContentConservative(uint32_t blockIndex, uint32_t lastChangedChunkIndex, const ::SFAT::BitSet& initialFreeClustersSet);

		// Moves all content as close as possible toward the beginning of the block.
		// This optimization depends on the block-transaction and being able to revert the block virtualization on step back.
		// The move operation does not have limitations.
		// It can use any free cluster space and also can move any cluster (new allocated or pre-transaction allocated).
		::SFAT::ErrorCode optimizeBlockContent(uint32_t blockIndex, uint32_t lastChangedChunkIndex, const ::SFAT::BitSet& initialFreeClustersSet);

	private:
		static float calculateDegradationScore(const ::SFAT::FATBlockTableType& table);

		uint32_t	getSelectedBlockIndex() const;
		::SFAT::ErrorCode	findBlockForOptimization(uint32_t& blockIndex);
		uint32_t	getCountClustersWritten() const;
		::SFAT::ErrorCode	defragmentFullBlock(uint32_t blockIndex);
		::SFAT::ErrorCode	fixDegradedBlock(uint32_t blockIndex);

	private:
		uint32_t mMaxFreeClustersInABlock;
		uint32_t mBlockIndexFound;
		uint32_t mIndexOfDegradedBlock; // An index of a degraded that should be optimized.
	};

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

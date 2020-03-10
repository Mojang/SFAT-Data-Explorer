/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/DataPlacementStrategyBase.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/VirtualFileSystem.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/CRC.h"
#include <algorithm>

namespace SFAT {

	/**************************************************************************
	*	DataPlacementStrategyBase implementation
	**************************************************************************/

	DataPlacementStrategyBase::DataPlacementStrategyBase(VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem)
		: mVolumeManager(volumeManager)
		, mVirtualFileSystem(virtualFileSystem)
		, mIsActive(false) {
	}

	bool DataPlacementStrategyBase::isActive() const {
		return mIsActive;
	}

	ErrorCode DataPlacementStrategyBase::moveCluster(ClusterIndexType sourceClusterIndex, ClusterIndexType destClusterIndex) {
		return mVirtualFileSystem.moveCluster(sourceClusterIndex, destClusterIndex);
	}

	ErrorCode DataPlacementStrategyBase::copyFreeClustersBitSet(BitSet& destBitSet, uint32_t blockIndex) {
		return mVolumeManager.copyFreeClusterBitSet(destBitSet, blockIndex);
	}

	uint32_t DataPlacementStrategyBase::getScratchBlockIndex() const {
		return mVolumeManager.getBlockVirtualization().getScratchBlockIndex();
	}

	uint32_t DataPlacementStrategyBase::getPhysicalBlockIndex(uint32_t virtualBlockIndex) const {
		return mVolumeManager.getBlockVirtualization().getPhysicalBlockIndex(virtualBlockIndex);
	}

	ErrorCode DataPlacementStrategyBase::swapScratchBlockWithVirtualBlock(uint32_t virtualBlockIndex) {
		return mVolumeManager.getBlockVirtualization().swapScratchBlockWithVirtualBlock(virtualBlockIndex);
	}

} // namespace SFAT

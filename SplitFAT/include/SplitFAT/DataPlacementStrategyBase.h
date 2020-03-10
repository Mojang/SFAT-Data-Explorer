/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <functional>
#include "Common.h"

///Unit-test classes forward declaration

namespace SFAT {

	class VolumeManager;
	class VirtualFileSystem;
	class BitSet;

	class DataPlacementStrategyBase {
	public:
		DataPlacementStrategyBase(VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem);
		virtual ~DataPlacementStrategyBase() = default;
		bool isActive() const;
		ErrorCode moveCluster(ClusterIndexType sourceClusterIndex, ClusterIndexType destClusterIndex);
		ErrorCode copyFreeClustersBitSet(BitSet& destBitSet, uint32_t blockIndex);

		uint32_t getScratchBlockIndex() const;
		uint32_t getPhysicalBlockIndex(uint32_t virtualBlockIndex) const;
		ErrorCode swapScratchBlockWithVirtualBlock(uint32_t virtualBlockIndex);


		virtual ErrorCode prepareForWriteTransaction() = 0;
		virtual ErrorCode performDefragmentaionOnTransactionEnd() = 0;
		virtual ErrorCode findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage) = 0;

	protected:
		VolumeManager& mVolumeManager;
		VirtualFileSystem& mVirtualFileSystem;
		bool mIsActive;
	};

} // namespace SFAT

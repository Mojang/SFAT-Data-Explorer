/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>

namespace SFAT {


	/**
	 * Stores general Volume parameters that get uptated with the expanding of the Volume.
	 */
	struct VolumeControlData {
		volatile uint32_t mCountAllocatedDataBlocks;
		volatile uint32_t mCountAllocatedFATBlocks;

		volatile uint32_t mCountTotalDataClusters;	//TODO: Take care to update it. Add a unit-test.
	};

	/**
	 * Stores parameters specific to particular FAT-data block and the corresponding Cluster-data block.
	 */
	struct BlockControlData {
		// CRC of the FAT
		uint32_t mCRC; //TODO: Take care to update it. Add a unit-test.
		uint32_t mBlockIndex;
	};

} // namespace SFAT


/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "AbstractFileSystem.h"
#include "LowLevelAccess.h"

namespace SFAT {

	class VolumeManager;
	class VolumeDescriptor;
	struct VolumeDescriptorExtraParameters;

	struct BlockVirtualizationHeader {
		enum {
			MAX_ID_COUNT = 8,
			MAX_ALLOWED_BLOCKS_COUNT = 64, // The count of 60 blocks (256MB each) will allow the allocation of up to 15GB
		};

		uint16_t verificationCode = 0; // Must be 0x5FA7
		uint8_t id = 0;
		uint8_t headerSize = 0;
		uint8_t virtualBlocksCount = 0; // 23 virtual blocks
		uint8_t scratchBlockIndex = 0;
		uint32_t dataCRC = 0;

		// This headerCRC must be last
		uint32_t headerCRC = 0;
	};

	struct BlockVirtualizationDescriptor {
		void initialCreate(uint8_t virtualBlocksCount, uint8_t scratchBlockIndex);
		bool verify();
		void cleanup();

		BlockVirtualizationHeader header;
		uint8_t blockIndices[BlockVirtualizationHeader::MAX_ALLOWED_BLOCKS_COUNT] = { 0 };
	};

	class BlockVirtualization {
	public:
		BlockVirtualization(VolumeManager& volumeManager);
		ErrorCode setup();
		ErrorCode shutdown();

		uint32_t getScratchBlockIndex() const;
		uint32_t getPhysicalBlockIndex(uint32_t virtualBlockIndex) const;
		uint32_t getPhysicalBlockIndexForClusterReading(ClusterIndexType clusterIndex) const;
		uint32_t getActiveDescriptorIndex() const;
		uint32_t getActiveDescriptorCRC() const;

		ErrorCode setBlockVirtualizationData(const VolumeDescriptorExtraParameters& extraParameters);

		ErrorCode swapScratchBlockWithVirtualBlock(uint32_t virtualBlockIndex);

	private:
		ErrorCode _writeBlockVirtualizationData();
		ErrorCode _readBlockVirtualizationData();
		ErrorCode _writeBlockVirtualizationData(FileHandle& file);
		ErrorCode _readBlockVirtualizationData(FileHandle& file);
		bool _updateDescriptor(BlockVirtualizationDescriptor& descriptor);
		uint8_t _getMaxVirtualBlocksCount() const;
		void _updateCRC(BlockVirtualizationDescriptor& descriptor);
		void PrintStatus(const char* szTitle);

	private:
		VolumeManager& mVolumeManager;
		size_t mDescriptorIndex;
	};

} // namespace SFAT
/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/BlockVirtualization.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/CRC.h"
#include "SplitFAT/VolumeDescriptor.h"
#include <string.h>

namespace SFAT {

	/**************************************************************************
	*	BlockVirtualizationDescriptor implementation
	**************************************************************************/

	void BlockVirtualizationDescriptor::initialCreate(uint8_t virtualBlocksCount, uint8_t scratchBlockIndex) {
		SFAT_ASSERT(virtualBlocksCount < BlockVirtualizationHeader::MAX_ALLOWED_BLOCKS_COUNT, "The total count of blocks exceeds the allowed maximum of 255!");

		cleanup();

		header.verificationCode = 0x5FA7;
		header.id = 0;
		header.headerSize = sizeof(BlockVirtualizationHeader);
		header.virtualBlocksCount = virtualBlocksCount;
		header.scratchBlockIndex = scratchBlockIndex;

		// The virtual indices are in range [0, virtualBlocksCount-1].
		// The physical block indices are in range [0, virtualBlocksCount]. One of the blocks will be the scratch block.
		// Thus the elements of the array are in [0, virtualBlocksCount]
		// A physical index can be get with blockIndex[virtualBlockIndex]

		uint8_t counter = 0;
		for (size_t i = 0; i < virtualBlocksCount; ++i, ++counter) {
			if (i == scratchBlockIndex) {
				++counter; // Skip this physical block index as it will be used for the scratch block
			}
			blockIndices[i] = static_cast<uint8_t>(counter);
		}

		header.dataCRC = CRC32::calculate(blockIndices, virtualBlocksCount);
		header.headerCRC = CRC32::calculate(&header, sizeof(BlockVirtualizationHeader) - sizeof(BlockVirtualizationHeader::headerCRC));
	}

	bool BlockVirtualizationDescriptor::verify() {
		if (header.verificationCode != 0x5FA7) {
			return false;
		}
		uint32_t headerCRC = CRC32::calculate(&header, sizeof(BlockVirtualizationHeader) - sizeof(BlockVirtualizationHeader::headerCRC));
		if (headerCRC != header.headerCRC) {
			return false;
		}
		uint32_t dataCRC = CRC32::calculate(blockIndices, header.virtualBlocksCount);
		if (dataCRC != header.dataCRC) {
			return false;
		}

		// Sanity check
		///////////////////////////////////////////////////////////////////////////
		if (header.id >= BlockVirtualizationHeader::MAX_ID_COUNT) {
			return false;
		}
		if (header.headerSize != sizeof(BlockVirtualizationHeader)) {
			return false;
		}
		if (header.virtualBlocksCount >= BlockVirtualizationHeader::MAX_ALLOWED_BLOCKS_COUNT) {
			return false;
		}
		if (header.scratchBlockIndex >= BlockVirtualizationHeader::MAX_ALLOWED_BLOCKS_COUNT) {
			return false;
		}

		return true;
	}

	void BlockVirtualizationDescriptor::cleanup() {
		memset(&header, 0, sizeof(header));
		memset(&blockIndices, 0, sizeof(blockIndices));
	}

	/**************************************************************************
	*	BlockVirtualization implementation
	**************************************************************************/

	BlockVirtualization::BlockVirtualization(VolumeManager& volumeManager)
		: mVolumeManager(volumeManager)
		, mDescriptorIndex(0) {
		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		extraParameters.descriptors[0].cleanup();
		extraParameters.descriptors[1].cleanup();
	}

	uint8_t BlockVirtualization::_getMaxVirtualBlocksCount() const {
		const uint8_t maxBlockCount = static_cast<uint8_t>(mVolumeManager.getVolumeDescriptor().getMaxBlocksCount());
		const uint8_t maxVirtualBlocksCount = maxBlockCount - static_cast<uint8_t>(mVolumeManager.getVolumeDescriptor().getFirstFileDataBlocksIndex());
		return maxVirtualBlocksCount;
	}

	void BlockVirtualization::_updateCRC(BlockVirtualizationDescriptor& descriptor) {
		descriptor.header.dataCRC = CRC32::calculate(descriptor.blockIndices, descriptor.header.virtualBlocksCount);
		descriptor.header.headerCRC = CRC32::calculate(&descriptor.header, sizeof(BlockVirtualizationHeader) - sizeof(BlockVirtualizationHeader::headerCRC));
	}

	bool BlockVirtualization::_updateDescriptor(BlockVirtualizationDescriptor& descriptor) {
		bool isUpdated = false;
		const uint8_t maxVirtualBlocksCount = _getMaxVirtualBlocksCount();
		if (descriptor.header.virtualBlocksCount > maxVirtualBlocksCount) {
			descriptor.header.virtualBlocksCount = maxVirtualBlocksCount;
			isUpdated = true;
		}

		if (descriptor.header.scratchBlockIndex > maxVirtualBlocksCount) {
			descriptor.header.scratchBlockIndex = maxVirtualBlocksCount;
			isUpdated = true;
		}

		if (isUpdated) {
			_updateCRC(descriptor);
		}

		return isUpdated;
	}

	ErrorCode BlockVirtualization::setup() {
		ErrorCode err = _readBlockVirtualizationData();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();

		bool descr0correct = extraParameters.descriptors[0].verify();
		bool descr1correct = extraParameters.descriptors[1].verify();
		if (descr0correct) {
			mDescriptorIndex = 0;
			if (descr1correct) {
				// Select the successor
				decltype(BlockVirtualizationHeader::id) id0 = extraParameters.descriptors[0].header.id;
				decltype(BlockVirtualizationHeader::id) id1 = extraParameters.descriptors[1].header.id;
				if (((id0 + 1) % BlockVirtualizationHeader::MAX_ID_COUNT) == id1) {
					mDescriptorIndex = 1;
				}
			}

			// Check if the selected descriptor needs an update
			if (_updateDescriptor(extraParameters.descriptors[mDescriptorIndex])) {
				err = _writeBlockVirtualizationData();
			}
		}
		else if (descr1correct) {
			// Only descriptor 1 is correct
			mDescriptorIndex = 1;
		}
		else {
			// Non of the descriptors is correct or initialized.
			mDescriptorIndex = 0;
			const uint8_t maxVirtualBlocksCount = _getMaxVirtualBlocksCount();;
#ifdef _WIN32
			uint8_t selectedScratchBlockIndex = static_cast<uint8_t>(mVolumeManager.getCountAllocatedDataBlocks());
			if (selectedScratchBlockIndex > maxVirtualBlocksCount) {
				selectedScratchBlockIndex = maxVirtualBlocksCount;
			}
#else
			const uint8_t selectedScratchBlockIndex = maxVirtualBlocksCount;
#endif
			extraParameters.descriptors[0].initialCreate(maxVirtualBlocksCount, selectedScratchBlockIndex);
			extraParameters.descriptors[1].cleanup();

			err = _writeBlockVirtualizationData();
		}

		PrintStatus("VBlock init");

		return err;
	}

	ErrorCode BlockVirtualization::shutdown() {
		return ErrorCode::RESULT_OK;
	}

	uint32_t BlockVirtualization::getPhysicalBlockIndex(uint32_t virtualBlockIndex) const {
		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		BlockVirtualizationDescriptor& bvd = extraParameters.descriptors[mDescriptorIndex];
		SFAT_ASSERT(virtualBlockIndex < bvd.header.virtualBlocksCount, "The virtual-block index is out of range!");

		return bvd.blockIndices[virtualBlockIndex];
	}

	uint32_t BlockVirtualization::getPhysicalBlockIndexForClusterReading(ClusterIndexType clusterIndex) const {
		uint32_t virtualBlockIndex = mVolumeManager.getBlockIndex(clusterIndex);
		if (virtualBlockIndex < mVolumeManager.getFirstFileDataBlockIndex()) {
			return static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE);
		}
		virtualBlockIndex -= mVolumeManager.getFirstFileDataBlockIndex();
		return getPhysicalBlockIndex(virtualBlockIndex);
	}

	uint32_t BlockVirtualization::getScratchBlockIndex() const {
		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		BlockVirtualizationDescriptor& bvd = extraParameters.descriptors[mDescriptorIndex];
		return bvd.header.scratchBlockIndex;
	}

	ErrorCode BlockVirtualization::swapScratchBlockWithVirtualBlock(uint32_t virtualBlockIndex) {
		ErrorCode err = ErrorCode::RESULT_OK;
		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		BlockVirtualizationDescriptor& currentDescriptor = extraParameters.descriptors[mDescriptorIndex];
		BlockVirtualizationDescriptor& newDescriptor = extraParameters.descriptors[mDescriptorIndex ^ 1];

		SFAT_ASSERT(virtualBlockIndex < currentDescriptor.header.virtualBlocksCount, "The virtual-block index is out of range!");

		const uint8_t physicalBlockIndex = currentDescriptor.blockIndices[virtualBlockIndex];
		SFAT_ASSERT(physicalBlockIndex != currentDescriptor.header.scratchBlockIndex, "Can't swap the blocks, because the indices are the same!");

		newDescriptor = currentDescriptor;
		newDescriptor.header.id = (newDescriptor.header.id + 1) % BlockVirtualizationHeader::MAX_ID_COUNT; // Assign the successor of the current id

		newDescriptor.blockIndices[virtualBlockIndex] = currentDescriptor.header.scratchBlockIndex;
		newDescriptor.header.scratchBlockIndex = physicalBlockIndex;

		newDescriptor.header.dataCRC = CRC32::calculate(newDescriptor.blockIndices, currentDescriptor.header.virtualBlocksCount);
		newDescriptor.header.headerCRC = CRC32::calculate(&newDescriptor.header, sizeof(BlockVirtualizationHeader) - sizeof(BlockVirtualizationHeader::headerCRC));

		err = _writeBlockVirtualizationData();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_BLOCK_VIRTUALIZATION, "Was not able to save the block-virtualization file!");
			newDescriptor.cleanup();
			return err;
		}

		// Update the descriptor index
		mDescriptorIndex ^= 1;

		PrintStatus("VBlock swap");

		return ErrorCode::RESULT_OK;
	}

	void BlockVirtualization::PrintStatus(const char* szTitle) {
		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		BlockVirtualizationDescriptor& currentDescriptor = extraParameters.descriptors[mDescriptorIndex];

		static char buf[512];
		int pos = 0;
		for (int i = 0; i < currentDescriptor.header.virtualBlocksCount; ++i) {
			sprintf(buf + pos, "[%02u]%02u ", i, currentDescriptor.blockIndices[i]);
			pos += 7;
		}
		SFAT_LOGI(LogArea::LA_BLOCK_VIRTUALIZATION, "%s - Scratch:%u, Indices:%s", szTitle, currentDescriptor.header.scratchBlockIndex, buf);
	}

	ErrorCode BlockVirtualization::_writeBlockVirtualizationData(FileHandle& file) {
		if (!file.isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

		FilePositionType position = mVolumeManager.getVolumeDescriptorPosition();
		position += reinterpret_cast<FilePositionType>(&(static_cast<VolumeDescriptor*>(NULL)->mExtraParameters.descriptors[0]));

		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		size_t sizeWritten = 0;
		size_t sizeToWrite = sizeof(BlockVirtualizationDescriptor) * 2;
		ErrorCode err = file.writeAtPosition(&extraParameters.descriptors[0], sizeToWrite, position, sizeWritten);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_BLOCK_VIRTUALIZATION, "Error #%08X while writing the block virtualization data!");
			return err;
		}
		if (sizeWritten != sizeToWrite) {
			SFAT_LOGE(LogArea::LA_BLOCK_VIRTUALIZATION, "Error #%08X. Size doesn't match while writing the block virtualization data!");
			return ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}

		err = file.flush();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_BLOCK_VIRTUALIZATION, "Error #%08X while flushing the block virtualization data!");
			return err;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode BlockVirtualization::_readBlockVirtualizationData(FileHandle& file) {
		if (!file.isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

		FilePositionType position = mVolumeManager.getVolumeDescriptorPosition();
		position += reinterpret_cast<FilePositionType>(&(static_cast<VolumeDescriptor*>(NULL)->mExtraParameters.descriptors[0]));

		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		size_t sizeRead = 0;
		size_t sizeToRead = sizeof(BlockVirtualizationDescriptor) * 2;
		ErrorCode err = file.readAtPosition(&extraParameters.descriptors[0], sizeToRead, position, sizeRead);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_BLOCK_VIRTUALIZATION, "Error #%08X while reading the block virtualization data!");
			return err;
		}
		if (sizeRead != sizeToRead) {
			SFAT_LOGE(LogArea::LA_BLOCK_VIRTUALIZATION, "Error #%08X. Size doesn't match while reading the block virtualization data!");
			return ErrorCode::ERROR_READING_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode BlockVirtualization::_writeBlockVirtualizationData() {
		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");

		return _writeBlockVirtualizationData(file);
	}

	ErrorCode BlockVirtualization::_readBlockVirtualizationData() {
		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_READ);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");

		return _readBlockVirtualizationData(file);
	}

	uint32_t BlockVirtualization::getActiveDescriptorIndex() const {
		return static_cast<uint32_t>(mDescriptorIndex);
	}

	uint32_t BlockVirtualization::getActiveDescriptorCRC() const {
		VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();
		BlockVirtualizationDescriptor& currentDescriptor = extraParameters.descriptors[mDescriptorIndex];
		return currentDescriptor.header.headerCRC;
	}

	ErrorCode BlockVirtualization::setBlockVirtualizationData(const VolumeDescriptorExtraParameters& extraParameters) {
		VolumeDescriptorExtraParameters& params = mVolumeManager.getVolumeDescriptorExtraParameters();
		params = extraParameters;
		ErrorCode err = _writeBlockVirtualizationData();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return setup();
	}

} // namespace SFAT
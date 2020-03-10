/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/VolumeDescriptor.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/HelperFunctions.h"
#include <string.h>

namespace SFAT {

	static const uint32_t gVolumeVerificationCode = 0x5FA7C0DE;
	static const uint32_t gLastVersion = 0x0007;
	/*
		- From version 0002 to 0003, introduced double linked list for the cluster-chains
		- From version 0004 to 0005, changed the storage capacity from 15GB (60 blocks) to 7.5GB (30 blocks)
		- From version 0005 to 0006, the FATCellValue type is changed. The new functionality is able to read the old format.
		- From version 0006 to 0007, the total data blocks become 24. Added block-virtualization.
		  There is an encoded CRC-16 per cluster now and additional flags.
	*/

	/**************************************************************************
	*	VolumeDescriptor implementation
	**************************************************************************/

	VolumeDescriptor::VolumeDescriptor()
		: mVolumeVerificationCode(0)
		, mFlags(VDF_DEFAULT) {
	}

	bool VolumeDescriptor::isInitialized() const {
		return mVolumeVerificationCode == gVolumeVerificationCode;
	}

	uint32_t VolumeDescriptor::getClustersPerFATBlock() const {
		return (mBytesPerVolumeBlock + mClusterSizeInBytes - 1) / mClusterSizeInBytes;
	}

	uint32_t VolumeDescriptor::getMaxBlocksCount() const {
		return mMaxBlocksCount;
	}

	uint32_t VolumeDescriptor::getFirstFileDataBlocksIndex() const {
		return mFirstFileDataBlockIndex;
	}

	uint32_t VolumeDescriptor::getFATOffset() const {
		return sizeof(VolumeDescriptor) + sizeof(VolumeControlData);
	}

	uint32_t VolumeDescriptor::getClusterIndexSize() const {
		return sizeof(FATCellValueType);
	}

	uint32_t VolumeDescriptor::getByteSizeOfFATBlock() const {
		return getClusterIndexSize() * getClustersPerFATBlock();
	}

	uint32_t VolumeDescriptor::getVerificationCode() const {
		return mVolumeVerificationCode;
	}

	bool VolumeDescriptor::isASingleFileStorage() const {
		return (mFlags & VolumeDescriptorFlags::VDF_SINGLE_FILE_VOLUME) != 0;
	}

	bool VolumeDescriptor::isScratchBlockSupported() const {
		return true;
	}

	FileSizeType VolumeDescriptor::getDataBlockSize() const {
		if (!isASingleFileStorage()) {
			return mBytesPerVolumeBlock;
		}
		else {
			SFAT_ASSERT(false, "Not implemented");
			return 0;
		}
	}

	uint32_t VolumeDescriptor::getClusterSize() const {
		return mClusterSizeInBytes;
	}

	uint32_t VolumeDescriptor::getChunkSize() const {
		return mChunkSizeInBytes;
	}

	void VolumeDescriptor::initializeWithDefaults() {
		//A magic number to verify this is a known type of container.
		mVolumeVerificationCode = gVolumeVerificationCode;

		//Version of the container
		mVersion = gLastVersion;

		// Size of the volume descriptor block in bytes.
		mVolumeDescriptorSize = sizeof(VolumeDescriptor);

		// Size of the volume control data in bytes.
		mVolumeControlDataSize = sizeof(VolumeControlData);

		// Size of the block control data in bytes.
		mBlockControlDataSize = sizeof(BlockControlData);

		mFlags = VDF_DEFAULT | VDF_SCRATCH_BLOCK_SUPPORT;

		// Specifies the maximum allowed data / FAT blocks.
		// 60 blocks of 256MB for total of 15GB
		// 24 blocks for 6.5GB
		// Note! Sychronize with the value of BerwickFileLargeWrites::mTotalBlocksCount.
		mMaxBlocksCount = TOTAL_BLOCKS_COUNT_VERSION_7 + DefaultSetupValues::FIRST_FILE_DATA_BLOCK_INDEX;  //The additional block(s) are for the directories and is on separate storage.
		if (isScratchBlockSupported()) {
			--mMaxBlocksCount; //Leave one data block for scratch block
		}

		// The offset is given in bytes.
		mFirstClusterOffset = 0;

		// Cluster size in bytes. For example 8KB.
		mClusterSizeInBytes = DefaultSetupValues::CLUSTER_SIZE;

		// Chunk size in bytes. Represents the smallest size that can be read and written.
		mChunkSizeInBytes = DefaultSetupValues::CHUNK_SIZE;

		// Bytes per volume (256MB).
		mBytesPerVolumeBlock = 256 * (1 << 20L);
		//mBytesPerVolumeBlock = 10 * (1 << 20L);

		// Number of FAT copies.
		mFATCopies = 1;

		// First file data block index. Most of the time it will be 0, but in our case we need 1, to leave one data block for directories only.
		mFirstFileDataBlockIndex = DefaultSetupValues::FIRST_FILE_DATA_BLOCK_INDEX;

		/******************************************************
			Directory and File Descriptor Record parameters
		******************************************************/

		// Bytes per file descriptor record.
		mFileDescriptorRecordStorageSize = smallestPowerOf2GreaterOrEqual(sizeof(FileDescriptorRecord));

		// Filename size in symbols
		mMaxFileNameLength = sizeof(FileDescriptorRecord::mEntityName); //Not used!

		// Bytes per symbol, 1 for ASCII / UTF8, 2 for UTF16, etc.
		mBytesPerSymbol = sizeof(FileDescriptorRecord::mEntityName[0]); //Not used!

		/******************************************************
			Future parameters
		******************************************************/

		memset(mRawBytes, 0, FUTURE_PARAMETERS_BUFFER_SIZE);

	}

	uint32_t VolumeDescriptor::getFileDescriptorRecordStorageSize() const {
		return mFileDescriptorRecordStorageSize;
	}

	//static
	uint32_t VolumeDescriptor::getLastVersion() {
		return gLastVersion;
	}

	uint32_t VolumeDescriptor::getCurrentVersion() const {
		return mVersion;
	}

	void VolumeDescriptor::initializeWithTestValues() {
		mVolumeVerificationCode = 0x7E57DA7A;
		mVersion = 0x123;
		mVolumeDescriptorSize = 0x345;
		mVolumeControlDataSize = 0x456;
		mBlockControlDataSize = 0x567;
		mMaxBlocksCount = 0x678;
		mFirstClusterOffset = 0x789;
		mClusterSizeInBytes = 0x89A;
		mBytesPerVolumeBlock = 0x9AB;
		mFATCopies = 0xABC;
		mFlags = 0x234;
		mChunkSizeInBytes = 0x345;
		mFirstFileDataBlockIndex = 0x456;

		/******************************************************
			Directory and File Descriptor Record parameters
		******************************************************/
		mFileDescriptorRecordStorageSize = 0xBCD;
		mMaxFileNameLength = 0xCDE;
		mBytesPerSymbol = 0xDEF;

		/******************************************************
			Future parameters
		******************************************************/

		memset(mRawBytes, 0xA5, FUTURE_PARAMETERS_BUFFER_SIZE);
	}

	bool VolumeDescriptor::compare(const VolumeDescriptor& vd) const {
		bool bRes = (0 == memcmp(vd.mRawBytes, mRawBytes, FUTURE_PARAMETERS_BUFFER_SIZE));
		bRes = bRes &&
			(vd.mVolumeVerificationCode == mVolumeVerificationCode) &&
			(vd.mVersion == mVersion) &&
			(vd.mVolumeDescriptorSize == mVolumeDescriptorSize) &&
			(vd.mVolumeControlDataSize == mVolumeControlDataSize) &&
			(vd.mBlockControlDataSize == mBlockControlDataSize) &&
			(vd.mMaxBlocksCount == mMaxBlocksCount) &&
			(vd.mFirstClusterOffset == mFirstClusterOffset) &&
			(vd.mClusterSizeInBytes == mClusterSizeInBytes) &&
			(vd.mChunkSizeInBytes == mChunkSizeInBytes) &&
			(vd.mBytesPerVolumeBlock == mBytesPerVolumeBlock) &&
			(vd.mFATCopies == mFATCopies) &&
			(vd.mFirstFileDataBlockIndex == mFirstFileDataBlockIndex) &&
			(vd.mFlags == mFlags) &&
		
			(vd.mFileDescriptorRecordStorageSize == mFileDescriptorRecordStorageSize) &&
			(vd.mMaxFileNameLength == mMaxFileNameLength) &&
			(vd.mBytesPerSymbol == mBytesPerSymbol);

		return bRes;
	}

	bool VolumeDescriptor::verifyConsistency() const {
		//TODO: Implement a more complete test!
		return (mVolumeVerificationCode == gVolumeVerificationCode);
	}

	VolumeDescriptorExtraParameters& VolumeDescriptor::getExtraParameters() {
		return mExtraParameters;
	}

} // namespace SFAT
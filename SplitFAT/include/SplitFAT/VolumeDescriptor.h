/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include "SplitFAT/ControlStructures.h"
#include "SplitFAT/BlockVirtualization.h"

namespace SFAT {

	enum VolumeDescriptorFlags : uint32_t {
		VDF_DEFAULT = 0,
		VDF_SINGLE_FILE_VOLUME = 1,
		VDF_SCRATCH_BLOCK_SUPPORT = 2,
	};

	struct VolumeDescriptorExtraParameters {
		BlockVirtualizationDescriptor descriptors[2];
	};

	class VolumeManager;

	/**
	 * Stores the general parameters of the volume
	 *
	 * Most of the parameters stored in the VolumeDescriptor are constant throughout the existance of the Volume
	 * An exception to the rule are the extra parameters.
	 */
	class VolumeDescriptor
	{
		friend class VolumeManager;

	public:
		enum FixedConstants {
			// Don't change this value, as it will require version changes and most probably conversion.
			FUTURE_PARAMETERS_BUFFER_SIZE = 512, // 512 bytes
			TOTAL_BLOCKS_COUNT_VERSION_7 = 24,
		};

	public:
		VolumeDescriptor();

		void initializeWithDefaults();
		bool isInitialized() const;

		uint32_t getClustersPerFATBlock() const;
		uint32_t getFATOffset() const;
		uint32_t getClusterIndexSize() const;
		uint32_t getByteSizeOfFATBlock() const;
		uint32_t getMaxBlocksCount() const;
		uint32_t getFirstFileDataBlocksIndex() const;
		uint32_t getVerificationCode() const;
		bool isASingleFileStorage() const;
		bool isScratchBlockSupported() const;
		FileSizeType getDataBlockSize() const;
		uint32_t getClusterSize() const;
		uint32_t getChunkSize() const;

		uint32_t getFileDescriptorRecordStorageSize() const;


		// To be used for unit testing
		void initializeWithTestValues();
		bool compare(const VolumeDescriptor& vd) const; // It is a bit easier to spot what is different with this function.

		static uint32_t getLastVersion();
		uint32_t getCurrentVersion() const;
		bool verifyConsistency() const;

		VolumeDescriptorExtraParameters& getExtraParameters();

	private:
		/*
			A magic number to verify this is a known type of container.
		*/
		uint32_t mVolumeVerificationCode; // = 0x5FA7C0DE;

		/*
			Version of the container
		*/
		uint32_t mVersion; // = 0x0006;

		/*
			Size of the volume descriptor block in bytes.
		*/
		uint32_t mVolumeDescriptorSize;

		/*
			Size of the volume control data in bytes.
		*/
		uint32_t mVolumeControlDataSize;

		/*
			Size of the block control data in bytes.
		*/
		uint32_t mBlockControlDataSize;

		/*
			Specifies the maximum allowed data/FAT blocks.
		*/
		uint32_t mMaxBlocksCount; // = 60 for 15GB

		/*
			The offset is given in bytes.
		*/
		uint32_t mFirstClusterOffset;

		/*
			Cluster size in bytes. For example 8KB.
		*/
		uint32_t mClusterSizeInBytes;

		/*
			Chunk size in bytes. For example 256KB.
		*/
		uint32_t mChunkSizeInBytes;

		/*
			Bytes per volume (256MB) for example.
		*/
		uint32_t mBytesPerVolumeBlock;

		/*
			Number of FAT copies.
		*/
		uint32_t mFATCopies;

		/*
			First file data block index. For example 1
		*/
		uint32_t mFirstFileDataBlockIndex;

		/*
			Is the volume stored in a single file, or two different files are used.
			In case of two different files, the storage will have this representation
				- Control Data File - VolumeDescriptor, array of FAT blocks
				- Data File - Array of clusters
			In case of a single file, the representation will be the following
				- Single File - VolumeDescriptor, array of ( FAT block, Data block )
		*/
		uint32_t mFlags; // isASingleFileStorage;


		/******************************************************
			Directory and File Descriptor Record parameters
		******************************************************/

		/*
			Bytes per file descriptor record.
		*/
		uint32_t mFileDescriptorRecordStorageSize;

		/*
			Filename size in symbols
		*/
		uint32_t mMaxFileNameLength;

		/*
			Bytes per symbol, 1 for ASCII / UTF8, 2 for UTF16, etc.
		*/
		uint32_t mBytesPerSymbol;

	public:
		/******************************************************
			Space for future parameters
		******************************************************/
		union {
			uint8_t mRawBytes[FUTURE_PARAMETERS_BUFFER_SIZE]; // The size should remain fixed to 512 bytes
			VolumeDescriptorExtraParameters mExtraParameters;
		};
	};


} // namespace SFAT
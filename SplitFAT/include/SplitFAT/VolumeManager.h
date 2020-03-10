/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "SplitFAT/Common.h"
#include "SplitFAT/utils/Mutex.h"
#include "SplitFAT/VolumeDescriptor.h"
#include "SplitFAT/ControlStructures.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/SplitFATConfigurationBase.h"
#include "SplitFAT/Transaction.h"
#include "SplitFAT/BlockVirtualization.h"

///Unit-test classes forward declaration
#if !defined(MCPE_PUBLISH)
class LowLevelUnitTest_VolumeDescriptorReadWrite_Test;
class LowLevelUnitTest_ClusterWriteRead_Test;
class VirtualFileSystemTests_ExpandFile_Test;
class LowLevelUnitTest_BlockAllocation_Test;
class BlockVirtualizationUnitTest; 
#endif //!defined(MCPE_PUBLISH)

namespace SFAT {

	class FATDataManager;
	class DataBlockManager;
	class VolumeDescriptor;
	class FileStorageBase;

	enum class FileSystemState {
		FSS_UNKNOWN,
		FSS_ERROR,
		FSS_STORAGE_SETUP,
		FSS_CREATED, // Physical storage created; Completely empty, no Root directory, 0 allocated FAT Block and 0 allocated Clusters.
		FSS_READY,	 // Fully functional.
	};

	/*
		Status report
	*/
	class StatusReport {
	public:
		FileSystemState mState;
		std::string		mDescription;
	};


	/**
	 *  Management of the low-level volume specific data and tasks.
	 *
	 *  Responsibilities:
	 *	- Creates a bridge between the physical storage layer (AbstractFileSystem) and the virtual system layer (VirtualFileSystem).
	 *	- Works with the FATDataManager and DataClusterManager.
	 *	- Implements an interface for working with clusters and FAT cells and volume control data, hiding the implementation from the higher layer.
	 *	  - Read/Write FAT cell.
	 *	  - Read/Write Cluster.
	 *	  - VolumeDescriptor initialize/read/write
	 *	  - VolumeControlData initialize/read/write
	 *	- Should not know about the lower level AbstractFileSystem.
	 *	- Should not know how the FAT cells and Clusters will be used. No knowledge about the organization of the VirtualFileSystem.
	 *	- Implements transaction for low level operations.
	 *	- Data caching for FAT and the control blocks. (Not implemented yet)
	 */
	class VolumeManager {
		// Unit-test classes have access to the private methods and members
#if !defined(MCPE_PUBLISH)
		friend class LowLevelUnitTest_VolumeDescriptorReadWrite_Test;
		friend class LowLevelUnitTest_ClusterWriteRead_Test;
		friend class VirtualFileSystemTests_ExpandFile_Test;
		friend class LowLevelUnitTest_BlockAllocation_Test;
		friend class TransactionUnitTest_RestoreFromTransaction_Test;
		friend class BlockVirtualizationUnitTest;
#endif //!defined(MCPE_PUBLISH)

	public:

		VolumeManager();
		~VolumeManager();

		ErrorCode setup(std::shared_ptr<SplitFATConfigurationBase> lowLevelFileAccess);

		ErrorCode createIfDoesNotExist(); // This function should be very safe in sense that it should not create new volume if there is an existing one. If it is broken somehow, it should try first to recover it.
		ErrorCode recoverVolume();
		ErrorCode openVolume();
		ErrorCode createVolume();
		ErrorCode removeVolume();

		/**
		 * Allocates FATBlock and ClusterDataBlock.
		 *
		 * @param blockIndexToAllocate the index of the block to be allocated.
		 * @returns the error code. On success - ErrorCode::RESULT_OK.
		 */
		ErrorCode allocateBlockByIndex(uint32_t blockIndexToAllocate);
		ErrorCode preallocateAllFATDataBlocks();
		ErrorCode preloadAllFATDataBlocks();
		ErrorCode blockSwitch();

		const VolumeDescriptor& getVolumeDescriptor() const;
		VolumeDescriptorExtraParameters& getVolumeDescriptorExtraParameters();
		uint32_t getCountAllocatedFATBlocks() const;
		void setCountAllocatedFATBlocks(uint32_t count);
		uint32_t getCountAllocatedDataBlocks() const;

		uint32_t getMaxPossibleBlocksCount() const;
		uint32_t getMaxPossibleFATBlocksCount() const;
		uint32_t getFileDescriptorRecordStorageSize() const;

		//For unit-testing
		bool clusterDataFileExists() const;
		bool fatDataFileExists() const;

		FilePositionType getDataBlockStartPosition(uint32_t blockIndex) const;
		FilePositionType getFATBlockStartPosition(uint32_t blockIndex) const;
		FilePositionType getVolumeControlDataPosition() const;
		FilePositionType getVolumeDescriptorPosition() const;
		uint32_t getCountTotalClusters() const;

		// Only to be used from FATDataManager/FATDataBlock and DataBlockManager.
		//TODO: Hide it from the higher levels (VirtualFileSystem, etc.).
		SplitFATConfigurationBase& getLowLevelFileAccess();
		const SplitFATConfigurationBase& getLowLevelFileAccess() const;

		ErrorCode fastConsistencyCheck() const;

		// Transaction control functions
		bool isInTransaction() const;
		ErrorCode startTransaction();
		ErrorCode endTransaction();
		ErrorCode logFileDescriptorChange(ClusterIndexType descriptorClusterIndex, const FileDescriptorRecord& oldRecord, const FileDescriptorRecord& newRecord);
		ErrorCode logFATCellChange(ClusterIndexType cellIndex, const FATBlockTableType& buffer);
		ErrorCode executeOnFATBlock(uint32_t blockIndex, FATBlockCallbackType callback);
		ErrorCode tryRestoreFromTransactionFile();

		// Low level storage access functions
		ErrorCode setFATCell(ClusterIndexType cellIndex, FATCellValueType value);
		ErrorCode getFATCell(ClusterIndexType cellIndex, FATCellValueType& value);
		ErrorCode readCluster(std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex);
		ErrorCode writeCluster(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex);
		ErrorCode verifyCRCOnRead(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex); // Should be called from the DataBlockManager inside the multi-thread synchronization block.
		ErrorCode updateCRCOnWrite(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex); // Should be called from the DataBlockManager inside the multi-thread synchronization block.
		ErrorCode findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage);
		ErrorCode copyFreeClusterBitSet(BitSet& destBitSet, uint32_t blockIndex);
		uint32_t  getClusterSize() const;
		uint32_t  getChunkSize() const;
		FileSizeType getDataBlockSize() const;
		ErrorCode flush();
		ErrorCode immediateFlush();

		// Low level storage access for the defragmentation
		FATDataManager& getFATDataManager();
		DataBlockManager& getDataBlockManager();

		FileSystemState getState() const;
		void setState(FileSystemState state);

		//TODO: Make accessible only to DataBlockManager, FATDataManager
		uint32_t getBlockIndex(ClusterIndexType clusterIndex) const;
		uint32_t getFirstFileDataBlockIndex() const;
		// Used in DataBlockManager
		bool isFileDataCluster(ClusterIndexType clusterIndex) const;
		ClusterIndexType getFirstFileDataClusterIndex() const;
		ErrorCode getCountFreeClusters(uint32_t& countFreeClusters, uint32_t blockIndex);
		ErrorCode getFreeSpace(FileSizeType& countFreeBytes);

		BlockVirtualization& getBlockVirtualization();
		ErrorCode _versionUpdate();

		//For testing purposes only
#if !defined(MCPE_PUBLISH)
		ErrorCode discardFATCachedChanges();
		ErrorCode discardDirectoryCachedChanges();
#endif //!defined(MCPE_PUBLISH)

	private:
		void _initializeWithDefaults();
		void _setCountAllocatedDataBlocks(uint32_t count);

		ClusterIndexType _getFirstClusterIndex(uint32_t blockIndex) const;
		ErrorCode _readVolumeDescriptor();
		ErrorCode _writeVolumeDescriptor() const;
		ErrorCode _readVolumeControlData();
		ErrorCode _writeVolumeControlData() const;

		ErrorCode _getCountFreeClusters(uint32_t& countFreeClusters);

	private:
		VolumeDescriptor	mVolumeDescriptor;
		std::unique_ptr<FATDataManager>	mFATDataManager;
		std::unique_ptr<DataBlockManager>	mDataBlockManager;
		SFATMutex				mVolumeExpansionMutex;
		VolumeControlData	mVolumeControlData;
		std::shared_ptr<SplitFATConfigurationBase>	mLowLevelAccess;
		TransactionEventsLog mTransaction;
		BlockVirtualization mBlockVirtualization;

		FileSystemState	mState;
	};

} //namespace SFAT
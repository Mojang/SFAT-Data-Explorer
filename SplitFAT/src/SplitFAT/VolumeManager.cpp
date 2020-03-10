/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/DataBlockManager.h"

#if (SPLIT_FAT__ENABLE_CRC_PER_CLUSTER == 1)
#	include "SplitFAT/utils/CRC.h"
#endif

#define	SFAT_ENABLE_TRACKING_OF_A_PARTICULAR_CLUSTER	0

namespace SFAT {

	/**************************************************************************
	*	VolumeManager implementation
	**************************************************************************/
	VolumeManager::VolumeManager() 
		: mTransaction(*this)
		, mBlockVirtualization(*this)
		, mState(FileSystemState::FSS_UNKNOWN) {

		_initializeWithDefaults();
		mFATDataManager = std::make_unique<FATDataManager>(*this);
		mDataBlockManager = std::make_unique<DataBlockManager>(*this);
	}

	VolumeManager::~VolumeManager() {
		ErrorCode err = flush();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Failed to write FAT on closing VolumeManager!");
		}
	}

	void VolumeManager::_initializeWithDefaults() {
		mVolumeDescriptor.initializeWithDefaults();
		mVolumeControlData.mCountAllocatedFATBlocks = 0;
		mVolumeControlData.mCountAllocatedDataBlocks = 0;
		mVolumeControlData.mCountTotalDataClusters = 0; // Keeps the count of total allocated clusters. Thus there is no need to initialize all clusters in an allocated Cluster-data block.
	}

	ErrorCode VolumeManager::setup(std::shared_ptr<SplitFATConfigurationBase> lowLevelFileAccess) {
		mLowLevelAccess = std::move(lowLevelFileAccess);

		SFAT_ASSERT(mLowLevelAccess->isReady(), "At this stage of the process the lowLevelFileAccess object is expected to be ready!");
		if (mLowLevelAccess->isReady()) {
			setState(FileSystemState::FSS_STORAGE_SETUP);
			return ErrorCode::RESULT_OK;
		}

		return ErrorCode::ERROR_LOW_LEVEL_STORAGE_IS_NOT_SETUP;
	}


	SplitFATConfigurationBase& VolumeManager::getLowLevelFileAccess() {
		return *mLowLevelAccess;
	}

	const SplitFATConfigurationBase& VolumeManager::getLowLevelFileAccess() const {
		return *mLowLevelAccess;
	}

	ErrorCode VolumeManager::openVolume() {
		if (!getLowLevelFileAccess().isReady()) {
			return ErrorCode::ERROR_LOW_LEVEL_STORAGE_IS_NOT_SETUP;
		}

		_initializeWithDefaults();

		ErrorCode err = getLowLevelFileAccess().open();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		err = _readVolumeDescriptor();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		err = _readVolumeControlData();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (!mVolumeDescriptor.verifyConsistency()) {
			return ErrorCode::ERROR_OPENING_FILE_LOW_LEVEL;
		}

		err = _versionUpdate();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return mBlockVirtualization.setup();
	}

	ErrorCode VolumeManager::_versionUpdate() {
		if ((mVolumeDescriptor.getCurrentVersion() == 6) && (mVolumeDescriptor.getLastVersion() == 7)) {
			mVolumeDescriptor.mMaxBlocksCount = VolumeDescriptor::TOTAL_BLOCKS_COUNT_VERSION_7;
			mVolumeDescriptor.mFlags = VDF_DEFAULT | VDF_SCRATCH_BLOCK_SUPPORT;
			if (mVolumeControlData.mCountAllocatedFATBlocks > mVolumeDescriptor.mMaxBlocksCount) {
				mVolumeControlData.mCountAllocatedFATBlocks = mVolumeDescriptor.mMaxBlocksCount;
			}
			if (mVolumeControlData.mCountAllocatedDataBlocks > mVolumeDescriptor.mMaxBlocksCount) {
				mVolumeControlData.mCountAllocatedDataBlocks = mVolumeDescriptor.mMaxBlocksCount;
				mVolumeControlData.mCountTotalDataClusters = getCountAllocatedDataBlocks()*mVolumeDescriptor.getClustersPerFATBlock();
			}

			// Last update the version
			mVolumeDescriptor.mVersion = mVolumeDescriptor.getLastVersion();

			ErrorCode err = _writeVolumeDescriptor();
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			err = _writeVolumeControlData();
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::createIfDoesNotExist() {
		if (!getLowLevelFileAccess().isReady()) {
			return ErrorCode::ERROR_LOW_LEVEL_STORAGE_IS_NOT_SETUP;
		}

		ErrorCode err = ErrorCode::ERROR_VOLUME_CAN_NOT_BE_OPENED;
		if (clusterDataFileExists() && fatDataFileExists()) {
			err = openVolume();
			if (err == ErrorCode::RESULT_OK) {
				if ((mVolumeDescriptor.getCurrentVersion() <= 0x0004) && (mVolumeDescriptor.getLastVersion() > 0x0004)) {
					// Because of incompatibility the old data storage will be removed and a new storage will be created on its place.
					
					// Remove first the old one
					err = removeVolume();
					if (err != ErrorCode::RESULT_OK) {
						setState(FileSystemState::FSS_ERROR);
						return err;
					}
				}
				else {
					err = fastConsistencyCheck();
					if (err == ErrorCode::RESULT_OK) {
						setState(FileSystemState::FSS_READY);
						return ErrorCode::RESULT_OK;
					}

					err = recoverVolume();
					if (err == ErrorCode::RESULT_OK) {
						setState(FileSystemState::FSS_READY);
						return ErrorCode::RESULT_OK;
					}

					// Unable to open correctly.
					// TODO: Create a report
				}
			}
		}
		else if (clusterDataFileExists() || fatDataFileExists()) {
			// TODO: Create a report

			err = removeVolume();
			if (err != ErrorCode::RESULT_OK) {
				setState(FileSystemState::FSS_ERROR);
				return err;
			}
		}

		err = createVolume();
		if (err == ErrorCode::RESULT_OK) {
			setState(FileSystemState::FSS_CREATED);
			return ErrorCode::RESULT_OK;
		}

		setState(FileSystemState::FSS_ERROR);
		return err;
	}

	ErrorCode VolumeManager::recoverVolume() {
		return ErrorCode::NOT_IMPLEMENTED;
	}

	ErrorCode VolumeManager::fastConsistencyCheck() const {
		//TODO: Implement it!
		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::createVolume()	{
		if (!getLowLevelFileAccess().isReady()) {
			return ErrorCode::ERROR_LOW_LEVEL_STORAGE_IS_NOT_SETUP;
		}

		_initializeWithDefaults();

		ErrorCode err = getLowLevelFileAccess().create();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		err = _writeVolumeDescriptor();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		err = _writeVolumeControlData();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return mBlockVirtualization.setup();
	}

	ErrorCode VolumeManager::removeVolume() {
		ErrorCode err = getLowLevelFileAccess().close();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Trying to remove the volume, but can't close the volume physical files.");
			return err;
		}

		err = getLowLevelFileAccess().remove();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't delete the volume physical files.");
		}

		setCountAllocatedFATBlocks(0);
		_setCountAllocatedDataBlocks(0);

		return err;
	}

	const VolumeDescriptor& VolumeManager::getVolumeDescriptor() const {
		return mVolumeDescriptor;
	}

	VolumeDescriptorExtraParameters& VolumeManager::getVolumeDescriptorExtraParameters() {
		return mVolumeDescriptor.getExtraParameters();
	}

	uint32_t VolumeManager::getCountAllocatedFATBlocks() const {
		return mVolumeControlData.mCountAllocatedFATBlocks;
	}

	void VolumeManager::setCountAllocatedFATBlocks(uint32_t count) {
		mVolumeControlData.mCountAllocatedFATBlocks = count;
	}

	uint32_t VolumeManager::getCountAllocatedDataBlocks() const {
		return mVolumeControlData.mCountAllocatedDataBlocks;
	}

	void VolumeManager::_setCountAllocatedDataBlocks(uint32_t count) {
		mVolumeControlData.mCountAllocatedDataBlocks = count;
	}

	uint32_t VolumeManager::getCountTotalClusters() const {
		return mVolumeControlData.mCountTotalDataClusters;
	}

	uint32_t VolumeManager::getMaxPossibleBlocksCount() const {
		//TODO: Consider estimating the current available space on the physical storage
		return mVolumeDescriptor.getMaxBlocksCount();
	}

	uint32_t VolumeManager::getMaxPossibleFATBlocksCount() const {
		//TODO: Consider estimating the current available space on the physical storage
		return mVolumeDescriptor.getMaxBlocksCount();
	}

	uint32_t VolumeManager::getFileDescriptorRecordStorageSize() const {
		return mVolumeDescriptor.getFileDescriptorRecordStorageSize();
	}

	ErrorCode VolumeManager::allocateBlockByIndex(uint32_t blockIndexToAllocate) {
		SFAT_ASSERT(mFATDataManager != nullptr, "The FAT object should be created! Is the VolumeManager initialized?");
		SFAT_ASSERT(mDataBlockManager != nullptr, "The DataBlockManager object should be created! Is the VolumeManager initialized?");

		if ((blockIndexToAllocate < getCountAllocatedDataBlocks()) && (blockIndexToAllocate < getCountAllocatedFATBlocks())) {
			// Nothing to be allocated.
			return ErrorCode::RESULT_OK;
		}

		if (blockIndexToAllocate >= getMaxPossibleBlocksCount()) {
			return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		SFATLockGuard lockGuard(mVolumeExpansionMutex);

		// Update the counts after the lockGuard.
		const uint32_t countFATBlocks = getCountAllocatedFATBlocks();
		const uint32_t countClusterDataBlocks = getCountAllocatedDataBlocks();

		// Check once again.
		if ((blockIndexToAllocate < countClusterDataBlocks) && (blockIndexToAllocate < countFATBlocks)) {
			// Nothing to be allocated.
			return ErrorCode::RESULT_OK;
		}

		// Allocate as much FAT blocks as needed.
		for (uint32_t fatBlockIndex = countFATBlocks; fatBlockIndex <= blockIndexToAllocate; ++fatBlockIndex) {
			SFAT_ASSERT(mFATDataManager->canExpand(), "The FAT should be able to expand!");
			ErrorCode errFAT = mFATDataManager->allocateFATBlock(fatBlockIndex);
			if (errFAT != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "The allocation of new FAT-data block failed!");
				return errFAT;
			}
			setCountAllocatedFATBlocks(fatBlockIndex + 1);
		}

		// Allocate as much Data blocks as needed.
		for (uint32_t dataBlockIndex = countClusterDataBlocks; dataBlockIndex <= blockIndexToAllocate; ++dataBlockIndex) {
			ErrorCode errClusterData = mLowLevelAccess->allocateDataBlock(*this, dataBlockIndex);
			if (errClusterData != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "The allocation of new cluster-data block failed!");
				return errClusterData;
			}
			_setCountAllocatedDataBlocks(dataBlockIndex + 1);
		}

		mVolumeControlData.mCountTotalDataClusters = getCountAllocatedDataBlocks()*mVolumeDescriptor.getClustersPerFATBlock();
		ErrorCode err = _writeVolumeControlData();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::_readVolumeDescriptor() {
		FileHandle file = getLowLevelFileAccess().getFATDataFile(AccessMode::AM_READ);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");
		FilePositionType position = getVolumeDescriptorPosition();

		size_t sizeRead = 0;
		ErrorCode err = file.readAtPosition(&mVolumeDescriptor, sizeof(VolumeDescriptor), position, sizeRead);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while reading the volume-descriptor data!");
			return err;
		}
		if (sizeRead != sizeof(VolumeDescriptor)) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't read the volume descriptor data.");
			return ErrorCode::ERROR_READING_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::_writeVolumeDescriptor() const {
		FileHandle file = getLowLevelFileAccess().getFATDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");
		FilePositionType position = getVolumeDescriptorPosition();

		size_t sizeWritten = 0;
		ErrorCode err = file.writeAtPosition(&mVolumeDescriptor, sizeof(VolumeDescriptor), position, sizeWritten);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while writing the volume-descriptor data!");
			return err;
		}
		if (sizeWritten != sizeof(VolumeDescriptor)) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't write the volume descriptor data.");
			return ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::_readVolumeControlData() {
		FileHandle file = getLowLevelFileAccess().getFATDataFile(AccessMode::AM_READ);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");
		FilePositionType position = getVolumeControlDataPosition();

		size_t sizeRead = 0;
		ErrorCode err = file.readAtPosition(&mVolumeControlData, sizeof(VolumeControlData), position, sizeRead);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while reading the volume-control data!");
			return err;
		}
		if (sizeRead != sizeof(VolumeControlData)) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't read the volume control data.");
			return ErrorCode::ERROR_READING_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::_writeVolumeControlData() const {
		FileHandle file = getLowLevelFileAccess().getFATDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");
		FilePositionType position = getVolumeControlDataPosition();

		size_t bytesToWrite = sizeof(VolumeControlData);
		size_t bytesWritten = 0;
		ErrorCode err = file.writeAtPosition(&mVolumeControlData, bytesToWrite, position, bytesWritten);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while writing the volume-control data!");
			return err;
		}
		if (bytesToWrite != bytesWritten) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't write the volume-control data!");
			return ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}

		err = file.flush();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while writing the volume-control data!");
			return err;
		}

		return ErrorCode::RESULT_OK;
	}

	bool VolumeManager::clusterDataFileExists() const {
		return getLowLevelFileAccess().clusterDataFileExists();
	}

	bool VolumeManager::fatDataFileExists() const {
		return getLowLevelFileAccess().fatDataFileExists();
	}

	FilePositionType VolumeManager::getDataBlockStartPosition(uint32_t blockIndex) const {
		FilePositionType offset = static_cast<FilePositionType>(mVolumeDescriptor.getDataBlockSize()) * blockIndex;
		return offset;
	}

	FilePositionType VolumeManager::getFATBlockStartPosition(uint32_t blockIndex ) const {
		FilePositionType offset = static_cast<FilePositionType>(mVolumeDescriptor.getByteSizeOfFATBlock() + sizeof(BlockControlData)) * blockIndex + mVolumeDescriptor.getFATOffset();
		return offset;
	}

	FilePositionType VolumeManager::getVolumeControlDataPosition() const {
		FilePositionType offset = sizeof(VolumeDescriptor);
		return offset;
	}

	FilePositionType VolumeManager::getVolumeDescriptorPosition() const {
		return 0;
	}

	bool VolumeManager::isFileDataCluster(ClusterIndexType clusterIndex) const {
		return (clusterIndex >= getFirstFileDataClusterIndex());
	}

	ClusterIndexType VolumeManager::getFirstFileDataClusterIndex() const {
		return getFirstFileDataBlockIndex() * mVolumeDescriptor.getClustersPerFATBlock();
	}

	uint32_t VolumeManager::getFirstFileDataBlockIndex() const {
		return mVolumeDescriptor.getFirstFileDataBlocksIndex();
	}

	ErrorCode VolumeManager::setFATCell(ClusterIndexType cellIndex, FATCellValueType value) {
		if (!value.isEndOfChain() && (cellIndex == value.getNext())) {
			// A cell should not point to itself!
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Trying to write an invalid value in the FAT!");
			return ErrorCode::ERROR_WRITING_INVALID_FAT_CELL_VALUE;
		}
		uint32_t cellBlockIndex = getBlockIndex(cellIndex);
		uint32_t nextCellBlockIndex = getBlockIndex(value.getNext());
		uint32_t blockIndex = cellBlockIndex; // A block that we may need to allocate
		if ((value.getNext() <= LAST_CLUSTER_INDEX_VALUE) && (blockIndex < nextCellBlockIndex)) {
			blockIndex = nextCellBlockIndex;
		}

		if ((blockIndex >= getCountAllocatedFATBlocks()) || (blockIndex >= getCountAllocatedDataBlocks())) {
			//
			// We need to have enough allocated blocks for both - FAT and cluster-data.
			ErrorCode err = allocateBlockByIndex(blockIndex);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		return mFATDataManager->setValue(cellIndex, value);
	}

	ErrorCode VolumeManager::getFATCell(ClusterIndexType cellIndex, FATCellValueType& value) {
		return mFATDataManager->getValue(cellIndex, value);
	}

	uint32_t VolumeManager::getBlockIndex(ClusterIndexType clusterIndex) const {
		return clusterIndex / mVolumeDescriptor.getClustersPerFATBlock();
	}

	ClusterIndexType VolumeManager::_getFirstClusterIndex(uint32_t blockIndex) const {
		return blockIndex * mVolumeDescriptor.getClustersPerFATBlock();
	}

	// This function should be called from the DataBlockManager inside the multi-thread synchronization block.
	ErrorCode VolumeManager::verifyCRCOnRead(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex) {
		ErrorCode err = ErrorCode::RESULT_OK;

#if (SPLIT_FAT__ENABLE_CRC_PER_CLUSTER == 1)
		uint32_t calculatedCrc = CRC16::calculate(buffer.data(), getClusterSize());
		FATCellValueType cellValue = FATCellValueType::invalidCellValue();
		err = getFATCell(clusterIndex, cellValue);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "Error reading FAT cell #%08X!", clusterIndex);
		}
		else {
			if (cellValue.isCRCInitialized()) {
				uint32_t storedCRC = static_cast<uint32_t>(cellValue.decodeCRC());
				if (calculatedCrc != storedCRC) {
					SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "CRC doesn't match for cluster #%08X! Calculated CRC: 0x%04X, Stored CRC: 0x%04X", clusterIndex, calculatedCrc, storedCRC);
#if !defined(MCPE_PUBLISH)
					return ErrorCode::ERROR_READING_CLUSTER_DATA_CRC_DOES_NOT_MATCH;
#else
					return ErrorCode::RESULT_OK;
#endif
				}
			}
		}
#endif

		return err;
	}

	// Should be called from the DataBlockManager inside the multi-thread synchronization block.
	ErrorCode VolumeManager::updateCRCOnWrite(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex) {
#if (SPLIT_FAT__ENABLE_CRC_PER_CLUSTER == 1)
		uint16_t calculatedCrc = CRC16::calculate(buffer.data(), getClusterSize());
		FATCellValueType cellValue = FATCellValueType::invalidCellValue();
		ErrorCode err = getFATCell(clusterIndex, cellValue);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "Error reading FAT cell #%08X!", clusterIndex);
			return err;
		}
		cellValue.encodeCRC(calculatedCrc);
		cellValue.setClusterInitialized(true);
		err = setFATCell(clusterIndex, cellValue);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "Error writing FAT cell #%08X!", clusterIndex);
			return err;
		}
#endif
		return ErrorCode::RESULT_OK;
	}

	ErrorCode VolumeManager::readCluster(std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex) {
		bool isDirectoryData = !isFileDataCluster(clusterIndex);
#if !defined(MCPE_PUBLISH) && (SFAT_ENABLE_TRACKING_OF_A_PARTICULAR_CLUSTER == 1)
		static ClusterIndexType clustersOfInterest[] = { 0x00008021 };
		for (int i = 0; i < sizeof(clustersOfInterest) / sizeof(clustersOfInterest[0]); ++i) {
			if (clusterIndex == clustersOfInterest[i]) {
				uint32_t physicalBlockIndex = getBlockVirtualization().getPhysicalBlockIndexForClusterReading(clusterIndex);
				SFAT_LOGI(LogArea::LA_VOLUME_MANAGER, "Reads cluster 0x%08x, phy. block: %u", clusterIndex, physicalBlockIndex);
			}
		}
#endif //!defined(MCPE_PUBLISH) && (SFAT_ENABLE_TRACKING_OF_A_PARTICULAR_CLUSTER == 1)

		ErrorCode err = mDataBlockManager->readCluster(buffer, clusterIndex, isDirectoryData);

		return err;
	}

	ErrorCode VolumeManager::writeCluster(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex) {
		bool isDirectoryData = !isFileDataCluster(clusterIndex);
#if !defined(MCPE_PUBLISH) && (SFAT_ENABLE_TRACKING_OF_A_PARTICULAR_CLUSTER == 1)
		static ClusterIndexType clustersOfInterest[] = { 0x00008021 };
		for (int i = 0; i < sizeof(clustersOfInterest) / sizeof(clustersOfInterest[0]); ++i) {
			if (clusterIndex == clustersOfInterest[i]) {
				uint32_t physicalBlockIndex = getBlockVirtualization().getScratchBlockIndex();
				SFAT_LOGI(LogArea::LA_VOLUME_MANAGER, "Writes cluster 0x%08x, phy. block: %u", clusterIndex, physicalBlockIndex);
			}
		}
#endif //!defined(MCPE_PUBLISH)
		return mDataBlockManager->writeCluster(buffer, clusterIndex, isDirectoryData);
	}

	ErrorCode VolumeManager::findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage) {
		ErrorCode err = mFATDataManager->tryFindFreeClusterInAllocatedBlocks(newClusterIndex, useFileDataStorage);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (newClusterIndex <= ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
			return ErrorCode::RESULT_OK;
		}

		uint32_t blockIndex = getCountAllocatedDataBlocks();
		err = allocateBlockByIndex(blockIndex);
		if (err == ErrorCode::RESULT_OK) {
			SFAT_ASSERT(blockIndex != static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE), "At that point we are supposed to have a new block allocated correctly!");
			newClusterIndex = _getFirstClusterIndex(blockIndex);
		}

		return err;
	}

	ErrorCode VolumeManager::copyFreeClusterBitSet(BitSet& destBitSet, uint32_t blockIndex) {
		const BitSet* bitSet = mFATDataManager->getFreeClustersSet(blockIndex);
		if (nullptr != bitSet) {
			destBitSet = *bitSet;
			return ErrorCode::RESULT_OK;
		}
		return ErrorCode::ERROR_BLOCK_INDEX_OUT_OF_RANGE;
	}

	FileSystemState VolumeManager::getState() const {
		return mState;
	}

	void VolumeManager::setState(FileSystemState state) {
		mState = state;
	}

	ErrorCode VolumeManager::_getCountFreeClusters(uint32_t& countFreeClusters) {
		return mFATDataManager->getCountFreeClusters(countFreeClusters);
	}

	ErrorCode VolumeManager::getCountFreeClusters(uint32_t& countFreeClusters, uint32_t blockIndex) {
		return mFATDataManager->getCountFreeClusters(countFreeClusters, blockIndex);
	}

	uint32_t  VolumeManager::getClusterSize() const {
		return mVolumeDescriptor.getClusterSize();
	}

	uint32_t  VolumeManager::getChunkSize() const {
		return mVolumeDescriptor.getChunkSize();
	}

	FileSizeType  VolumeManager::getDataBlockSize() const {
		return mVolumeDescriptor.getDataBlockSize();
	}

	ErrorCode VolumeManager::startTransaction() {
		return mTransaction.start();
	}

	ErrorCode VolumeManager::endTransaction() {
		return mTransaction.commit();
	}

	ErrorCode VolumeManager::tryRestoreFromTransactionFile() {
		return mTransaction.tryRestoreFromTransactionFile();
	}

	ErrorCode VolumeManager::logFileDescriptorChange(ClusterIndexType descriptorClusterIndex, const FileDescriptorRecord& oldRecord, const FileDescriptorRecord& newRecord) {
		return mTransaction.logFileDescriptorChange(descriptorClusterIndex, oldRecord, newRecord);
	}

	ErrorCode VolumeManager::logFATCellChange(ClusterIndexType cellIndex, const FATBlockTableType& buffer) {
		return mTransaction.logFATCellChange(cellIndex, buffer);
	}

	bool VolumeManager::isInTransaction() const {
		return mTransaction.isInTransaction();
	}

	ErrorCode VolumeManager::executeOnFATBlock(uint32_t blockIndex, FATBlockCallbackType callback) {
		return mFATDataManager->executeOnBlock(blockIndex, callback);
	}

	ErrorCode VolumeManager::flush() {
		ErrorCode err = ErrorCode::RESULT_OK;
		if (!isInTransaction()) {
			err = immediateFlush();
		}
		return err;
	}

	ErrorCode VolumeManager::immediateFlush() {
		// Write the cached FAT data to the corresponding physical file
		ErrorCode err = mFATDataManager->flush();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "The FAT-data wasn't written correctly on the physical storage!");
			return err;
		}

		// Write the cached cluster-data to the corresponding physical file
		err = mDataBlockManager->flush();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "The cluster-data wasn't written correctly on the physical storage!");
			return err;
		}

		// Flush the FAT physical file 
		err = getLowLevelFileAccess().flushFATDataFile();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "The physical file for the FAT-data wasn't flushed correctly!");
			return err;
		}

		// Flush the cluster data physical file 
		err = getLowLevelFileAccess().flushClusterDataFile();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "The physical file for the cluster-data wasn't flushed correctly!");
			return err;
		}

		return err;
	}

	ErrorCode VolumeManager::getFreeSpace(FileSizeType& countFreeBytes) {
		countFreeBytes = 0;
		uint32_t countFreeClusters;
		ErrorCode err = _getCountFreeClusters(countFreeClusters);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		countFreeBytes = static_cast<FileSizeType>(countFreeClusters)*mVolumeDescriptor.getClusterSize();

		return ErrorCode::RESULT_OK;
	}

	BlockVirtualization& VolumeManager::getBlockVirtualization() {
		return mBlockVirtualization;
	}

	FATDataManager& VolumeManager::getFATDataManager() {
		return *mFATDataManager;
	}

	DataBlockManager& VolumeManager::getDataBlockManager() {
		return *mDataBlockManager;
	}

	ErrorCode VolumeManager::preallocateAllFATDataBlocks() {
		return mFATDataManager->preallocateAllFATDataBlocks();
	}

	ErrorCode VolumeManager::preloadAllFATDataBlocks() {
		return mFATDataManager->preloadAllFATDataBlocks();
	}

	ErrorCode VolumeManager::blockSwitch() {
		return ErrorCode::NOT_IMPLEMENTED;
	}

	//For testing purposes only
#if !defined(MCPE_PUBLISH)
	ErrorCode VolumeManager::discardFATCachedChanges() {
		ErrorCode err = mFATDataManager->discardCachedChanges();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "The FAT-data wasn't written correctly on the physical storage!");
			return err;
		}
		return err;
	}

	ErrorCode VolumeManager::discardDirectoryCachedChanges() {
		// Skip writing the cached cluster-data to the corresponding physical file
		ErrorCode err = mDataBlockManager->discardCachedChanges();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VOLUME_MANAGER, "The cluster-data wasn't read correctly from the physical storage!");
			return err;
		}
		return err;
	}
#endif //!defined(MCPE_PUBLISH)

} // namespace SFAT

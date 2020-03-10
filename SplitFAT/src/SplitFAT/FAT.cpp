/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeDescriptor.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/CRC.h"

namespace SFAT {

	/**************************************************************************
	*	FATBlock implementation
	**************************************************************************/

	FATBlock::FATBlock(VolumeManager& volumeManager, uint32_t blockIndex) // ClusterIndexType startCluster, ClusterIndexType clustersCount);
		: mVolumeDescriptor(volumeManager.getVolumeDescriptor())
		, mBlockIndex(blockIndex)
		, mIsCacheInSync(false) {

		SFAT_ASSERT(getVolumeDescriptor().isInitialized(), "The VolumeDescriptor is not initialized!");
		uint32_t clustersPerBlock = getVolumeDescriptor().getClustersPerFATBlock();
		mStartClusterIndex = mBlockIndex * clustersPerBlock;
		mEndClusterIndex = mStartClusterIndex + clustersPerBlock - 1;
#if (SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED == 1)
		mBlockControlData.mCRC = 0; //calculateCRC32();
		mBlockControlData.mBlockIndex = mBlockIndex;
#endif
		FATCellValueType freeCellValue = FATCellValueType::freeCellValue();
		mTable.resize(clustersPerBlock, freeCellValue);
		// Fill up the set of the free clusters with all clusters from this block
#if (SPLIT_FAT__USE_BITSET == 1)
		mFreeClustersBitSet.setSize(clustersPerBlock);
		mFreeClustersBitSet.setAll(true);
#else
		for(ClusterIndexType index = mStartClusterIndex; index <= mEndClusterIndex; ++index) {
			mFreeClustersSet.insert(index);
		}
#endif
	}

	const VolumeDescriptor& FATBlock::getVolumeDescriptor() const {
		return mVolumeDescriptor;
	}

	FATCellValueType FATBlock::getValue(ClusterIndexType index) const {
		SFAT_ASSERT((index >= mStartClusterIndex) && (index <= mEndClusterIndex), "Cluster index out of range!");
		SFAT_ASSERT(mTable.size() == getVolumeDescriptor().getClustersPerFATBlock(), "The FATBlock table is invalid size!");

		return mTable[index - mStartClusterIndex];
	}

	void FATBlock::setValue(ClusterIndexType index, FATCellValueType value) {
		SFAT_ASSERT((index >= mStartClusterIndex) && (index <= mEndClusterIndex), "Cluster index out of range!");
		SFAT_ASSERT(mTable.size() == getVolumeDescriptor().getClustersPerFATBlock(), "The FATBlock table is invalid size!");

		mTable[index - mStartClusterIndex] = value;
		// Update the free-clusters set
#if (SPLIT_FAT__USE_BITSET == 1)
		mFreeClustersBitSet.setValue(index - mStartClusterIndex, value.isFreeCluster());
#else
		if (value.isFreeCluster()) {
			mFreeClustersSet.insert(index);
		}
		else {
			mFreeClustersSet.erase(index);
		}
#endif
		mIsCacheInSync = false;
	}

	// Reads from specific place
	ErrorCode FATBlock::read(FileHandle& file, FilePositionType filePosition) {
		SFAT_ASSERT(file.isOpen(), "The file in not opened or in a proper read/write mode!");
		SFAT_ASSERT(mTable.size() == getVolumeDescriptor().getClustersPerFATBlock(), "The FATBlock table is invalid size!");

		size_t countBytesToRead = static_cast<size_t>(sizeof(BlockControlData));
		size_t bytesRead = 0;
		ErrorCode err;
#if (SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED == 1)
		err = file.readAtPosition(&mBlockControlData, countBytesToRead, filePosition, bytesRead);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_FAT_READ, "Error #%08X during reading!");
			return err;
		}
		if (bytesRead != countBytesToRead) {
			SFAT_LOGE(LogArea::LA_FAT_READ, "The read size is less than the requested for reading!");
			return err;
		}
#endif

		filePosition += countBytesToRead;
		countBytesToRead = static_cast<size_t>(getVolumeDescriptor().getByteSizeOfFATBlock());
		bytesRead = 0;
		err = file.readAtPosition(mTable.data(), countBytesToRead, filePosition, bytesRead);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_FAT_READ, "Error #%08X during reading!");
			return err;
		}
		if (bytesRead != countBytesToRead) {
			SFAT_LOGE(LogArea::LA_FAT_READ, "The read size is less than the requested for reading!");
			err = ErrorCode::ERROR_READING;
		}

		// Find all free clusters from this block and fill the mFreeClustersSet
#if (SPLIT_FAT__USE_BITSET == 1)
		mFreeClustersBitSet.setAll(false);
		ClusterIndexType cellsCount = static_cast<ClusterIndexType>(mTable.size());
		for (ClusterIndexType cellIndex = 0; cellIndex < cellsCount; ++cellIndex) {
			if (mTable[cellIndex].isFreeCluster()) {
				mFreeClustersBitSet.setValue(cellIndex, true);
			}
		}
#else
		mFreeClustersSet.clear();
		ClusterIndexType cellsCount = static_cast<ClusterIndexType>(mTable.size());
		for (ClusterIndexType cellIndex = 0; cellIndex < cellsCount; ++cellIndex) {
			if (mTable[cellIndex].isFreeCluster()) {
				mFreeClustersSet.insert(mStartClusterIndex + cellIndex);
			}
		}
#endif

		mIsCacheInSync = true;
		return err;
	}

	// Writes to specific place
	ErrorCode FATBlock::write(FileHandle& file, FilePositionType filePosition) const {
		SFAT_ASSERT(file.isOpen(), "The file in not opened or in a proper read/write mode!");
		SFAT_ASSERT(mTable.size() == getVolumeDescriptor().getClustersPerFATBlock(), "The FATBlock table is invalid size!");

		ErrorCode err;
		// Write first the block-control data
		size_t countBytesToWrite = static_cast<size_t>(sizeof(BlockControlData));
		size_t bytesWritten = 0;
#if (SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED == 1)
		err = file.writeAtPosition(&mBlockControlData, countBytesToWrite, filePosition, bytesWritten);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_FAT_WRITE, "Error #%08X during writing!");
			return err;
		}
		if (bytesWritten != countBytesToWrite) {
			SFAT_LOGE(LogArea::LA_FAT_WRITE, "The written size is less than the requested for writing!");
			return ErrorCode::ERROR_WRITING;
		}
		bytesWritten = 0;
#endif //if (BLOCK_CONTROL_DATA_SAVING_ENABLED == 1)

		filePosition += countBytesToWrite;
		countBytesToWrite = static_cast<size_t>(getVolumeDescriptor().getByteSizeOfFATBlock());
		err = file.writeAtPosition(mTable.data(), countBytesToWrite, filePosition, bytesWritten);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_FAT_WRITE, "Error #%08X during writing!");
			return err;
		}
		err = ErrorCode::RESULT_OK;
		if (bytesWritten != countBytesToWrite) {
			SFAT_LOGE(LogArea::LA_FAT_WRITE, "The written size is less than the requested for writing!");
			err = ErrorCode::ERROR_WRITING;
		}

		return err;
	}

	ErrorCode FATBlock::flush(FileHandle& file, FilePositionType filePosition) {
		if (!mIsCacheInSync) {
			ErrorCode err = write(file, filePosition);
			mIsCacheInSync = (err == ErrorCode::RESULT_OK);
			return err;
		}
		return ErrorCode::RESULT_OK;
	}

	uint32_t FATBlock::calculateCRC32() const {
		const void* data = reinterpret_cast<const void*>(mTable.data());
		uint32_t sizeInBytes = getVolumeDescriptor().getByteSizeOfFATBlock();
		return CRC32::calculate(data, sizeInBytes);
	}

	bool FATBlock::tryToFindFreeCluster(ClusterIndexType& newClusterIndex) const {
		return getFirstFreeClusterIndex(newClusterIndex);
	}

#if (SPLIT_FAT__BLOCK_CONTROL_DATA_READING_WRITING_ENABLED == 1)
	BlockControlData& FATBlock::getBlockControlData() {
		return mBlockControlData;
	}
#endif

	uint32_t FATBlock::getCountFreeClusters() const {
#if (SPLIT_FAT__USE_BITSET == 1)
		return static_cast<uint32_t>(mFreeClustersBitSet.getCountOnes());
#else
		return static_cast<uint32_t>(mFreeClustersSet.size());
#endif
	}

	FATBlockTableType& FATBlock::getTable() {
		return mTable;
	}

	bool FATBlock::isCacheInSync() const {
		return mIsCacheInSync;
	}

	void FATBlock::markOutOfSync() {
		mIsCacheInSync = true;
	}

	bool FATBlock::getFirstFreeClusterIndex(ClusterIndexType& clusterIndex) const {
#if (SPLIT_FAT__USE_BITSET == 1)
		size_t foundFreeLocalCell = ClusterValues::INVALID_VALUE;
		bool res = mFreeClustersBitSet.findFirstOne(foundFreeLocalCell);
		if (res) {
			clusterIndex = static_cast<ClusterIndexType>(foundFreeLocalCell) + mStartClusterIndex;
			return true;
		}
#else
		if (!mFreeClustersSet.empty()) {
			clusterIndex = *mFreeClustersSet.cbegin();
			return true;
		}
#endif

		return false;
	}

	//bool FATBlock::getLastFreeClusterIndex(ClusterIndexType& clusterIndex) const {
	//	if (!mFreeClustersSet.empty()) {
	//		clusterIndex = *mFreeClustersSet.cend();
	//		return true;
	//	}

	//	return false;
	//}

	const BitSet& FATBlock::getFreeClustersSet() const {
		return mFreeClustersBitSet;
	}


	/**************************************************************************
	*	FAT implementation
	**************************************************************************/

	FATDataManager::FATDataManager(VolumeManager& volumeManager)
		: mVolumeDescriptor(volumeManager.getVolumeDescriptor())
		, mVolumeManager(volumeManager) {

	}

	ErrorCode FATDataManager::_updateCache(uint32_t blockIndex) {
		//
		// Note! The cache can be updated only for FAT blocks that have been already allocated.

		SFAT_ASSERT(mVolumeManager.getCountAllocatedFATBlocks() <= mVolumeManager.getMaxPossibleFATBlocksCount(), "Have allocated more FAT blocks than the allowed maximum!");

		if (blockIndex >= mVolumeManager.getCountAllocatedFATBlocks()) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "The block with index %u is not yet allocated!", blockIndex); //In case of error, the VolumeManager::allocateBlockByIndex() has the resposibility to allocate.
			return ErrorCode::ERROR_BLOCK_INDEX_OUT_OF_RANGE;
		}

		if ((blockIndex < static_cast<uint32_t>(mFATBlocksCache.size())) && (mFATBlocksCache[blockIndex] != nullptr)) {
			// No need to do anything. The data is already cached.
			return ErrorCode::RESULT_OK;
		}

		SFATLockGuard lockGuard(mFATBlockReadWriteMutex);

		// Check again. Another thread could have already cached the block before the lock.
		if ((blockIndex < static_cast<uint32_t>(mFATBlocksCache.size())) && (mFATBlocksCache[blockIndex] != nullptr)) {
			// No need to do anything. The data is already cached.
			return ErrorCode::RESULT_OK;
		}

		if (blockIndex >= static_cast<uint32_t>(mFATBlocksCache.size())) {
			mFATBlocksCache.resize(blockIndex + 1);
			SFAT_LOGI(LogArea::LA_PHYSICAL_DISK, "Expanded the FAT cache %u block(s).", blockIndex + 1);
		}

		ErrorCode err = ErrorCode::RESULT_OK;
		std::unique_ptr<FATBlock> fatBlockPtr = std::make_unique<FATBlock>(mVolumeManager, blockIndex);;
		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_READ);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open for reading!");
		FilePositionType offset = mVolumeManager.getFATBlockStartPosition(blockIndex);
		err = fatBlockPtr->read(file, offset);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't read a FATBlock which should be allocated!");
		}
		mFATBlocksCache[blockIndex] = std::move(fatBlockPtr);

		return err;
	}

	ErrorCode FATDataManager::getValue(ClusterIndexType index, FATCellValueType& value) {
		uint32_t blockIndex = mVolumeManager.getBlockIndex(index);
		if (blockIndex >= mVolumeManager.getCountAllocatedFATBlocks()) {
			//
			// There is no such block allocated.
			// Thus, the value can be only ClusterValues::FREE_CLUSTER
			// Though we better return an error, because we shouldn't have this as a case.
			value = FATCellValueType::freeCellValue();
			return ErrorCode::ERROR_TRYING_TO_READ_NOT_ALLOCATED_FAT_BLOCK;
		}

		ErrorCode err = _updateCache(blockIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		SFAT_ASSERT(blockIndex < mFATBlocksCache.size(), "Trying to read a FAT cell which is out of range!");
		SFAT_ASSERT(mFATBlocksCache[blockIndex] != nullptr, "The FAT block should be already cached!");
		value = mFATBlocksCache[blockIndex]->getValue(index);

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::setValue(ClusterIndexType index, FATCellValueType value) {
		uint32_t blockIndex = mVolumeManager.getBlockIndex(index);
		ErrorCode err = _updateCache(blockIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		SFAT_ASSERT(blockIndex < mFATBlocksCache.size(), "Trying to read a FAT cell which is out of range!");
		SFAT_ASSERT(mFATBlocksCache[blockIndex] != nullptr, "The FAT block should be cached at that point!");

		if (mFATBlocksCache[blockIndex] == nullptr) {
			return ErrorCode::ERROR_FAT_NOT_CACHED;
		}

		FATBlock& block = *mFATBlocksCache[blockIndex];

		// Take care for the transaction data here.
		if (mVolumeManager.isInTransaction() && block.isCacheInSync()) {
			mVolumeManager.logFATCellChange(index, block.getTable());
		}

		block.setValue(index, value);

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::allocateFATBlock(uint32_t blockIndex) {
		if (blockIndex >= mVolumeManager.getMaxPossibleFATBlocksCount()) {
			return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		volatile uint32_t currentBlocksCount = mVolumeManager.getCountAllocatedFATBlocks();
		if (currentBlocksCount > blockIndex) {
			SFAT_LOGW(LogArea::LA_PHYSICAL_DISK, "The FATBlock with same index is already created!");
			return ErrorCode::RESULT_OK;
		}

		SFAT_ASSERT(currentBlocksCount == blockIndex, "The FAT can only expand with one FATDataBlock at the time!");

		SFATLockGuard lockGuard(mFATBlockReadWriteMutex);

		// Check again if the block is not allocated in meantime.
		currentBlocksCount = mVolumeManager.getCountAllocatedFATBlocks();
		if (currentBlocksCount > blockIndex) {
			return ErrorCode::RESULT_OK;
		}

		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");

		std::unique_ptr<FATBlock> block = std::make_unique<FATBlock>(mVolumeManager, currentBlocksCount);
		FilePositionType offset = mVolumeManager.getFATBlockStartPosition(currentBlocksCount);
		ErrorCode err = block->write(file, offset);
		if (err != ErrorCode::RESULT_OK) {
			return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		//Add to the cache
		SFAT_ASSERT(static_cast<uint32_t>(mFATBlocksCache.size()) <= currentBlocksCount, "The FATBlock shold not be already in the cache.");
		mFATBlocksCache.resize(currentBlocksCount + 1);
		mFATBlocksCache[currentBlocksCount] = std::move(block);

		return err;
	}

	bool FATDataManager::canExpand() const	{
		uint32_t currentBlocksCount = mVolumeManager.getCountAllocatedFATBlocks();
		return (currentBlocksCount < mVolumeManager.getMaxPossibleFATBlocksCount());
	}

	ErrorCode FATDataManager::tryFindFreeClusterInAllocatedBlocks(ClusterIndexType& newClusterIndex, bool useFileDataStorage) {
		// Loop first through all allocated blocks and update the cache when necessary,
		// finally if there is no free cluster, allocate a new FATBlock/ClusterDataBlock. The last is not responsibility of the current function.

		// Correction necessary to ensure a faster access to the clusters used for directories.
		//  - Reserve block index 0 entirely for Directories
		//  - Use block index 1+ for File Data.
		newClusterIndex = ClusterValues::INVALID_VALUE;
		uint32_t countBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		uint32_t startBlockIndex = 0;
		if (useFileDataStorage) {
			startBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();
		}
		else if (countBlocks > 0) {
			countBlocks = mVolumeManager.getFirstFileDataBlockIndex();
		}

		for (uint32_t blockIndex = startBlockIndex; blockIndex < countBlocks; ++blockIndex) {
			if ((blockIndex >= mFATBlocksCache.size()) || (mFATBlocksCache[blockIndex] == nullptr)) {
				ErrorCode err = _updateCache(blockIndex);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
			}

			if (mFATBlocksCache[blockIndex]->tryToFindFreeCluster(newClusterIndex)) {
				//We just found a free cluster in the current FAT block.
				return ErrorCode::RESULT_OK;
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::tryFindFreeClusterInBlock(ClusterIndexType& newClusterIndex, uint32_t blockIndex) {
		if (blockIndex >= mVolumeManager.getMaxPossibleFATBlocksCount()) {
			SFAT_LOGE(SFAT::LogArea::LA_FAT_READ, "Invalid FAT block index %u of [0, %u]", blockIndex, mVolumeManager.getMaxPossibleFATBlocksCount() - 1);
			return ErrorCode::ERROR_INVALID_FAT_BLOCK_INDEX;
		}
		if ((blockIndex >= mFATBlocksCache.size()) || (mFATBlocksCache[blockIndex] == nullptr)) {
			ErrorCode err = preallocateAllFATDataBlocks();
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			SFAT_ASSERT((mFATBlocksCache.size() > blockIndex) && (mFATBlocksCache[blockIndex] != nullptr), "The FAT data block should be allocated correctly!");
		}

		if (mFATBlocksCache[blockIndex]->tryToFindFreeCluster(newClusterIndex)) {
			//We just found a free cluster in the current FAT block.
			return ErrorCode::RESULT_OK;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::flush() {
		SFATLockGuard lockGuard(mFATBlockReadWriteMutex);

		ErrorCode finalErr = ErrorCode::RESULT_OK;
		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_WRITE);
		if (file.isOpen()) {
			uint32_t countBlocks = static_cast<uint32_t>(mFATBlocksCache.size());
			for (uint32_t blockIndex = 0; blockIndex < countBlocks; ++blockIndex) {
				if (mFATBlocksCache[blockIndex] != nullptr) {
					FilePositionType offset = mVolumeManager.getFATBlockStartPosition(blockIndex);
					ErrorCode err = mFATBlocksCache[blockIndex]->flush(file, offset);
					if (err != ErrorCode::RESULT_OK) {
						SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't save the FATDataBlock #%u!", blockIndex);
						finalErr = err;
					}
				}
			}
		}

		return finalErr;
	}

	//For testing purposes only
#if !defined(MCPE_PUBLISH)
	//Instead of writing down all cached changes, this function will ignore them and read from the physical storage.
	//It will simulate lost of the changes this way.
	ErrorCode FATDataManager::discardCachedChanges() {
		SFATLockGuard lockGuard(mFATBlockReadWriteMutex);

		ErrorCode finalErr = ErrorCode::RESULT_OK;
		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_READ);
		if (file.isOpen()) {
			uint32_t countBlocks = static_cast<uint32_t>(mFATBlocksCache.size());
			for (uint32_t blockIndex = 0; blockIndex < countBlocks; ++blockIndex) {
				if (mFATBlocksCache[blockIndex] != nullptr) {
					FilePositionType offset = mVolumeManager.getFATBlockStartPosition(blockIndex);
					ErrorCode err = mFATBlocksCache[blockIndex]->read(file, offset);
					if (err != ErrorCode::RESULT_OK) {
						SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't read the FATDataBlock #%u!", blockIndex);
						finalErr = err;
					}
				}
			}
		}

		return finalErr;
	}
#endif //!defined(MCPE_PUBLISH)

	ErrorCode FATDataManager::getCountFreeClusters(uint32_t& countFreeClusters, uint32_t blockIndex) {
		uint32_t countBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		if (blockIndex >= countBlocks) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "The blockIndex points to not allocated block!");
			return ErrorCode::ERROR_TRYING_TO_READ_NOT_ALLOCATED_FAT_BLOCK;
		}

		if (mFATBlocksCache[blockIndex] == nullptr) {
			ErrorCode err = _updateCache(blockIndex);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't load FATDataBlock #%u!", blockIndex);
				return err;
			}
		}
		countFreeClusters = mFATBlocksCache[blockIndex]->getCountFreeClusters();

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::getMaxCountFreeClustersInABlock(uint32_t& maxFreeClustersInABlock, uint32_t& blockIndexFound, uint32_t blockIndexToAvoid) {
		maxFreeClustersInABlock = 0;
		uint32_t countFreeClusters;
		uint32_t countBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		// We need to count only the data-block free clusters
		uint32_t startBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();
		uint32_t granularity = mVolumeDescriptor.getClustersPerFATBlock() / 4;
		uint32_t maxPossibleValue = (mVolumeDescriptor.getClustersPerFATBlock() + granularity - 1) / granularity;

		blockIndexFound = static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE);
		uint32_t currentMaxValue = 0;
		for (uint32_t blockIndex = startBlockIndex; blockIndex < countBlocks; ++blockIndex) {
			if ((blockIndex >= mFATBlocksCache.size()) || (mFATBlocksCache[blockIndex] == nullptr)) {
				ErrorCode err = _updateCache(blockIndex);
				if (err != ErrorCode::RESULT_OK) {
					SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't load FATDataBlock #%u!", blockIndex);
					return err;
				}
			}

			if (blockIndex == blockIndexToAvoid) {
				continue;
			}

			countFreeClusters = mFATBlocksCache[blockIndex]->getCountFreeClusters();
			uint32_t value = (countFreeClusters + granularity - 1) / granularity;
			if (value > currentMaxValue){
				maxFreeClustersInABlock = countFreeClusters;
				currentMaxValue = value;
				blockIndexFound = blockIndex;
				if (maxPossibleValue == currentMaxValue) {
					// There is no need to search more!
					break;
				}
			}
		}

		if (canExpand() && (maxFreeClustersInABlock < mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock())) {
			maxFreeClustersInABlock = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock();
			blockIndexFound = countBlocks;
		}

		if ((maxFreeClustersInABlock == 0) && (blockIndexToAvoid != static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE))) {
			// There was no block with empty space found. So as a last resort we have to use the block that was selected for defragmentation.
			maxFreeClustersInABlock = mFATBlocksCache[blockIndexToAvoid]->getCountFreeClusters();
			blockIndexFound = blockIndexToAvoid;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::getCountFreeClusters(uint32_t& countFreeClusters) {
		countFreeClusters = 0;
		uint32_t countBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		// We need to count only the data-block free clusters
		uint32_t startBlockIndex = mVolumeManager.getFirstFileDataBlockIndex();

		for (uint32_t blockIndex = startBlockIndex; blockIndex < countBlocks; ++blockIndex) {
			if ((blockIndex >= mFATBlocksCache.size()) || (mFATBlocksCache[blockIndex] == nullptr)) {
				ErrorCode err = _updateCache(blockIndex);
				if (err != ErrorCode::RESULT_OK) {
					SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't load FATDataBlock #%u!", blockIndex);
					return err;
				}
			}
			countFreeClusters += mFATBlocksCache[blockIndex]->getCountFreeClusters();
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::executeOnBlock(uint32_t blockIndex, FATBlockCallbackType callback) {
		ErrorCode err = _prepareBlock(blockIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		bool wasChanged = false;
		err = callback(blockIndex, mFATBlocksCache[blockIndex]->getTable(), wasChanged);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't restore FATDataBlock #%u!", blockIndex);
			return err;
		}

		if (wasChanged) {
			mFATBlocksCache[blockIndex]->markOutOfSync();
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::_prepareBlock(uint32_t blockIndex) {
		if (mFATBlocksCache.size() <= blockIndex) {
			if (blockIndex >= mVolumeManager.getMaxPossibleFATBlocksCount()) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "The block index is out of range #%u!", blockIndex);
				return ErrorCode::ERROR_BLOCK_INDEX_OUT_OF_RANGE;
			}
			mFATBlocksCache.resize(blockIndex + 1);
		}
		if (mFATBlocksCache[blockIndex] == nullptr) {
			ErrorCode err = _updateCache(blockIndex);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't load FATDataBlock #%u!", blockIndex);
				return err;
			}
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::preloadAllFATDataBlocks() {
		uint32_t currentBlocksCount = mVolumeManager.getCountAllocatedFATBlocks();
		uint32_t currentCachedBlocksCount = static_cast<uint32_t>(mFATBlocksCache.size());
		if (currentCachedBlocksCount == currentBlocksCount) {
			return ErrorCode::RESULT_OK;
		}

		SFAT_ASSERT(currentBlocksCount <= mVolumeManager.getMaxPossibleFATBlocksCount(), "The created FAT-data blocks should not be less or equal to the maximum allowed!");
		SFAT_ASSERT(mFATBlocksCache.size() <= mVolumeManager.getMaxPossibleFATBlocksCount(), "The cached FAT-data blocks should not be more than the maximum allowed!");

		// Preload in the cache all created FAT-data blocks.
		mFATBlocksCache.resize(currentBlocksCount);
		for (size_t blockIndex = currentCachedBlocksCount; blockIndex < currentBlocksCount; ++blockIndex) {
			ErrorCode err = _updateCache(static_cast<uint32_t>(blockIndex));
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't load FATDataBlock #%u!", blockIndex);
				return err;
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode FATDataManager::preallocateAllFATDataBlocks() {
		uint32_t maxFATBlocksCount = mVolumeManager.getMaxPossibleFATBlocksCount();

		volatile uint32_t currentBlocksCount = mVolumeManager.getCountAllocatedFATBlocks();
		if (currentBlocksCount >= maxFATBlocksCount) {
			return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		SFATLockGuard lockGuard(mFATBlockReadWriteMutex);

		// Check again if the block is not allocated in meantime.
		currentBlocksCount = mVolumeManager.getCountAllocatedFATBlocks();
		if (currentBlocksCount >= maxFATBlocksCount) {
			return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		FileHandle file = mVolumeManager.getLowLevelFileAccess().getFATDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The FAT data file should be open!");

		ErrorCode err = ErrorCode::RESULT_OK;
		mFATBlocksCache.resize(maxFATBlocksCount);
		while (currentBlocksCount < maxFATBlocksCount) {
			std::unique_ptr<FATBlock> block = std::make_unique<FATBlock>(mVolumeManager, currentBlocksCount);
			FilePositionType offset = mVolumeManager.getFATBlockStartPosition(currentBlocksCount);
			err = block->write(file, offset);
			if (err != ErrorCode::RESULT_OK) {
				err = ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
				mFATBlocksCache.resize(currentBlocksCount);
				break;
			}

			//Add to the cache
			mFATBlocksCache[currentBlocksCount] = std::move(block);
			++currentBlocksCount;
		}

		mVolumeManager.setCountAllocatedFATBlocks(currentBlocksCount);
		return err;
	}

	const BitSet* FATDataManager::getFreeClustersSet(uint32_t blockIndex) {
		ErrorCode err = _prepareBlock(blockIndex);
		if (err != ErrorCode::RESULT_OK) {
			return nullptr;
		}
		return &(mFATBlocksCache[blockIndex]->getFreeClustersSet());
	}

	ClusterIndexType FATDataManager::getStartClusterIndex(uint32_t blockIndex) const {
		return static_cast<ClusterIndexType>(blockIndex)*static_cast<ClusterIndexType>(mVolumeDescriptor.getClustersPerFATBlock());
	}

} // namespace SFAT
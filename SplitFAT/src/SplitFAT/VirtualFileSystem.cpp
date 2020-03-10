/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/VirtualFileSystem.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/FileManipulator.h"

#include "SplitFAT/FAT.h"

#include "SplitFAT/utils/HelperFunctions.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/PathString.h"
#include "SplitFAT/utils/SFATAssert.h"
#include <algorithm>
#include <string.h>
#include <vector>
#include <stack>

#define SPLIT_FAT_WARN_FOR_WRITE_OUT_OF_TRANSACTION		0
#define SPLITFAT_ENABLE_MOVECLUSTER_DEBUGGING	0
#define SPLITFAT_ENABLE_WARNING_FOUND_DIRECTORY_INSTEAD_OF_FILE	0

namespace SFAT {

	StackAutoElement::StackAutoElement(FileManipulatorStack& stack)
		: mStackRef(stack) {
		// Allocate new element at the top
		mStackRef.emplace();
	}

	StackAutoElement::~StackAutoElement() {
		// Deallocate the top element
		mStackRef.pop();
	}

	FileManipulator& StackAutoElement::getTop() {
		return mStackRef.top();
	}

	VirtualFileSystem::VirtualFileSystem()
		: mIsValid(false) {
		mRecoveryManager = std::make_unique<RecoveryManager>(mVolumeManager, *this);
	}

	VirtualFileSystem::~VirtualFileSystem() {
	}

	ErrorCode VirtualFileSystem::setup(std::shared_ptr<SplitFATConfigurationBase> lowLevelFileAccess) {

		ErrorCode err = mVolumeManager.setup(std::move(lowLevelFileAccess));
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

#if (SPLIT_FAT_ENABLE_DEFRAGMENTATION == 1)
		err = mVolumeManager.getLowLevelFileAccess().createDataPlacementStrategy(mDefragmentation, mVolumeManager, *this);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		SFAT_ASSERT(mDefragmentation != nullptr, "There should be DataPlacementStrategy created here!");
#endif

		// Update all cached parameters
		mClusterSize = mVolumeManager.getClusterSize();
		// Usually used by at most 2 threads, so 2 buffers. Should not keep more than 10 buffers if they are not used.
		mMemoryBufferPool = std::make_unique<MemoryBufferPool>(2, mClusterSize, 10);

		err = mVolumeManager.createIfDoesNotExist();
		mIsValid = (err == ErrorCode::RESULT_OK);
		if (err == ErrorCode::RESULT_OK) {
			if (mVolumeManager.getState() == FileSystemState::FSS_CREATED) {
				err = mVolumeManager.preallocateAllFATDataBlocks();
				if (err != ErrorCode::RESULT_OK) {
					mVolumeManager.setState(FileSystemState::FSS_ERROR);
				}
				else {
					// Needs Root directory created
					err = _createRoot();
					if (err == ErrorCode::RESULT_OK) {
						mVolumeManager.setState(FileSystemState::FSS_READY);
					}
					else {
						mVolumeManager.setState(FileSystemState::FSS_ERROR);
					}
				}
			}
		}

		if (err == ErrorCode::RESULT_OK) {
			err = mVolumeManager.preloadAllFATDataBlocks();
		}

		//Some sanity check
		SFAT_ASSERT(isPowerOf2(mClusterSize), "The cluster size must be power of 2.");
		SFAT_ASSERT(isPowerOf2(getFileDescriptorRecordStorageSize()), "The storage size per FileDescriptorRecord must be power of 2.");
		SFAT_ASSERT(mClusterSize > getFileDescriptorRecordStorageSize(), "The cluster size should be bigger than the storage size for FileDescriptorRecord!");

		if (err == ErrorCode::RESULT_OK) {
			// Check if there was an interrupted transaction and data that has to be restored.
			err = mVolumeManager.tryRestoreFromTransactionFile();
			if (err != ErrorCode::RESULT_OK) {
				//TODO: Use other method to restore to a correct file-system state
			}
		}

		return err;
	}

	ErrorCode VirtualFileSystem::_appendClusterToEndOfChain(const DescriptorLocation& location, ClusterIndexType endOfChainClusterIndex, ClusterIndexType& allocatedClusterIndex, bool useFileDataStorage) {
		ClusterIndexType newClusterIndex = ClusterValues::INVALID_VALUE;
		ErrorCode err = _findFreeCluster(newClusterIndex, useFileDataStorage);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't find free cluster!");
			return err;
		}

		SFAT_ASSERT(newClusterIndex <= ClusterValues::LAST_CLUSTER_INDEX_VALUE, "The cluster index is invalid!");
		SFAT_ASSERT(newClusterIndex != ClusterValues::ROOT_START_CLUSTER_INDEX, "The Root-start cluster index is not a correct index!");

		FATCellValueType newCellValue;
		newCellValue.makeEndOfChain();
		if (isValidClusterIndex(endOfChainClusterIndex)) {
			newCellValue.setPrev(endOfChainClusterIndex);
		}
		else {
			newCellValue.makeStartOfChain();
		}
		newCellValue.setClusterInitialized(false);

		uint32_t descriptorsPerCluster = mVolumeManager.getClusterSize() / mVolumeManager.getFileDescriptorRecordStorageSize();
		SFAT_ASSERT(descriptorsPerCluster < (1 << ClusterValues::FDRI_BITS_COUNT), "Can not encode the recordIndex in the file's first FATCell value.");
		newCellValue.encodeFileDescriptorLocation(location.mDescriptorClusterIndex, location.mRecordIndex % descriptorsPerCluster);

		err = mVolumeManager.setFATCell(newClusterIndex, newCellValue);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't write FAT cell #%u!", newClusterIndex);
			return err;
		}
		allocatedClusterIndex = newClusterIndex;

		FATCellValueType prevCellValue = FATCellValueType::invalidCellValue();
		if (isValidClusterIndex(endOfChainClusterIndex)) {
			err = mVolumeManager.getFATCell(endOfChainClusterIndex, prevCellValue);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't read FAT cell #%u!", endOfChainClusterIndex);
				return err;
			}
			SFAT_ASSERT(prevCellValue.isEndOfChain(), "This cluster should be the last one in the current chain!");
			prevCellValue.setNext(newClusterIndex);
			err = mVolumeManager.setFATCell(endOfChainClusterIndex, prevCellValue);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't write FAT cell #%u!", endOfChainClusterIndex);
				return err;
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::_expandClusterChain(const FileManipulator& fileManipulator, uint32_t countClusters, ClusterIndexType& resultStartClusterIndex, ClusterIndexType& resultEndClusterIndex, bool useFileDataStorage) {
		const ClusterIndexType startClusterIndex = fileManipulator.mFileDescriptorRecord.mStartCluster;
		ClusterIndexType endOfChainClusterIndex = fileManipulator.getLastCluster();
		SFAT_ASSERT(isValidClusterIndex(endOfChainClusterIndex) == isValidClusterIndex(startClusterIndex), "Both cluster indices have to be defined at the same time!");
#ifdef _DEBUG
		if (isValidClusterIndex(startClusterIndex)) {
			// There is a valid startClusterIndex, so we are expanding an existing cluster chain.
			// In that case we need to find the last cluster of the chain to attach the new allocated to.
			ClusterIndexType verificationEndOfChainClusterIndex = ClusterValues::INVALID_VALUE;
			ErrorCode err = _findLastClusterInChain(startClusterIndex, verificationEndOfChainClusterIndex);
			SFAT_ASSERT(err == ErrorCode::RESULT_OK, "There is a problem finding the last cluster in a chain!");
			SFAT_ASSERT(verificationEndOfChainClusterIndex == endOfChainClusterIndex, "The endOfChainClusterIndex is not correct!");
		}
#endif
		resultStartClusterIndex = startClusterIndex;
		ClusterIndexType allocatedClusterIndex;
		for (uint32_t i = 0; i < countClusters; ++i) {
			allocatedClusterIndex = ClusterValues::INVALID_VALUE;
			ErrorCode err = _appendClusterToEndOfChain(fileManipulator.getDescriptorLocation(), endOfChainClusterIndex, allocatedClusterIndex, useFileDataStorage);
			if (err != ErrorCode::RESULT_OK) {
				// Should we revert the allocated clusters here? There won't be need to revert if the transaction is made on higner level.
				// It is possible also the error to be coming from the physical storage, and it may break the revert process as well.
				return err;
			}
			SFAT_ASSERT(allocatedClusterIndex <= ClusterValues::LAST_CLUSTER_INDEX_VALUE, "The allocated cluster should have a valid index!");
			if (resultStartClusterIndex > ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
				// The startClusterIndex was invalid, which means, we are creating a new cluster chain.
				// In that case, assign the first allocated cluster to the resultStartClusterIndex.
				resultStartClusterIndex = allocatedClusterIndex;
			}
			endOfChainClusterIndex = allocatedClusterIndex;
		}
		resultEndClusterIndex = endOfChainClusterIndex;

		return ErrorCode::RESULT_OK;
	}


	ErrorCode VirtualFileSystem::_findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage) {
#if (SPLIT_FAT_ENABLE_DEFRAGMENTATION == 1)
		newClusterIndex = ClusterValues::INVALID_VALUE;
		ErrorCode err = mDefragmentation->findFreeCluster(newClusterIndex, useFileDataStorage);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (isValidClusterIndex(newClusterIndex)) {
			return ErrorCode::RESULT_OK;
		}
#endif
		return mVolumeManager.findFreeCluster(newClusterIndex, useFileDataStorage);
	}

	ErrorCode VirtualFileSystem::_createRoot() {
		ErrorCode err;
		if (mVolumeManager.getCountAllocatedDataBlocks() <= mVolumeManager.getFirstFileDataBlockIndex()) {
			err = mVolumeManager.allocateBlockByIndex(mVolumeManager.getFirstFileDataBlockIndex());
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}
		FATCellValueType value = FATCellValueType::singleElementClusterChainValue();
		value.encodeFileDescriptorLocation(0, 0);
		err = mVolumeManager.setFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		std::vector<uint8_t> buffer(_getClusterSize(), 0);

		SFAT_ASSERT(mVolumeManager.getCountAllocatedDataBlocks() == mVolumeManager.getFirstFileDataBlockIndex() + 1, "The first file-data block should be allocated!");
		SFAT_ASSERT(mVolumeManager.getCountAllocatedFATBlocks() >= mVolumeManager.getCountAllocatedDataBlocks(), "The FAT blocks should be able to cover all data blocks!");

		err = mVolumeManager.writeCluster(buffer, 0);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		FileManipulator rootFM;
		err = _createRootDirFileManipulator(rootFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		err = _writeFileDescriptor(rootFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		
		err = mVolumeManager.flush();
		if (err == ErrorCode::RESULT_OK){
			mVolumeManager.setState(FileSystemState::FSS_READY);
		}

		return err;
	}

	ErrorCode VirtualFileSystem::_iterateThroughClusterChain(ClusterIndexType initialClusterIndex, std::function<ErrorCode(bool& doQuit, ClusterIndexType, FATCellValueType)> callback, bool iterateForward /* = true*/, uint32_t maxClusterCount /* = 0*/) {
		if (initialClusterIndex > ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "An invalid start cluster index is provided!");
			return ErrorCode::ERROR_INVALID_CLUSTER_INDEX;
		}

		uint32_t totalClusters = mVolumeManager.getCountTotalClusters();
		if ((maxClusterCount == 0) || (maxClusterCount > totalClusters)) {
			maxClusterCount = totalClusters;
		}

		SFAT_ASSERT(initialClusterIndex < totalClusters, "The start cluster index is limited by the total reserved clusters");

		ClusterIndexType currentCluster = ClusterValues::INVALID_VALUE;
		FATCellValueType cellValue;
		if (iterateForward) {
			cellValue.setNext(initialClusterIndex);
		}
		else {
			cellValue.setPrev(initialClusterIndex);
		}
		bool doQuit = false;
		for (uint32_t counter = 0; counter < maxClusterCount; ++counter) {
			if (iterateForward) {
				currentCluster = cellValue.getNext();
			}
			else {
				currentCluster = cellValue.getPrev();
			}
			cellValue = FATCellValueType::invalidCellValue();
			ErrorCode err = mVolumeManager.getFATCell(currentCluster, cellValue);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't read FAT cell #%u!", currentCluster);
				return err;
			}

			SFAT_ASSERT(cellValue.isValid(), "The FAT cell value is supposed to be valid!");
			if (cellValue.isFreeCluster()) {
				mRecoveryManager->reportError(ErrorCode::ERROR_INCONSISTENCY_POINTING_TO_FREE_CLUSTER, "No FAT cell should point to a FREE cluster!", initialClusterIndex, counter, currentCluster);
				return ErrorCode::ERROR_INCONSISTENCY;
			}

			// Do here whatever is necessary with the currentCluster and cellValue
			err = callback(doQuit, currentCluster, cellValue);

			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Iteration through cluster chain stopped because of error.");
				return err;
			}

			if (doQuit) {
				break;
			}

			if (iterateForward) {
				if (cellValue.isEndOfChain()) {
					break;
				}
			}
			else {
				if (cellValue.isStartOfChain()) {
					break;
				}
			}
		}

		return ErrorCode::RESULT_OK;
	}

#if !defined(MCPE_PUBLISH)
	ErrorCode VirtualFileSystem::_printClusterChain(ClusterIndexType startClusterIndex) {
		int counter = 0;
		ErrorCode err = _iterateThroughClusterChain(startClusterIndex, 
			[&counter](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)doQuit; // Not used parameter

				if (cellValue.isStartOfChain() || cellValue.isEndOfChain()) {
					ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
					uint32_t recordIndex = static_cast<uint32_t>(-1);
					cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
					printf("count:%u, cluster:%u, FileDescriptor cluster:%u, record:%2u\n", counter, currentCluster, descriptorClusterIndex, recordIndex);
					if (!cellValue.isStartOfChain()) {
						printf("  prev:%u\n", cellValue.getPrev());
					}
					if (!cellValue.isEndOfChain()) {
						printf("  next:%u\n", cellValue.getNext());
					}
				}
				else {
					printf("count:%u, cluster:%u, next:%u, prev:%u\n", counter, currentCluster, cellValue.getNext(), cellValue.getPrev());
				}
				++counter;
				return ErrorCode::RESULT_OK;
			}
			);

		return err;
	}

	ErrorCode VirtualFileSystem::_loadClusterChain(ClusterIndexType startClusterIndex, ClusterChainVector &clusterChain) {
		clusterChain.clear();
		ErrorCode err = _iterateThroughClusterChain(startClusterIndex,
			[&clusterChain](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
			(void)doQuit; // Not used parameter

			clusterChain.emplace_back(currentCluster, cellValue);
			return ErrorCode::RESULT_OK;
		}
		);

		return err;
	}

	ErrorCode VirtualFileSystem::_findLastClusterInChain(ClusterIndexType startClusterIndex, ClusterIndexType& endOfChainClusterIndex) {
		ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
		ErrorCode err = _iterateThroughClusterChain(startClusterIndex,
			[&lastClusterIndex](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
			(void)doQuit; // Not used parameter

			if (cellValue.isEndOfChain()) {
				lastClusterIndex = currentCluster;
			}
			return ErrorCode::RESULT_OK;
		}
		);

		if (err == ErrorCode::RESULT_OK) {
			endOfChainClusterIndex = lastClusterIndex;
		}

		return err;
	}

	ErrorCode VirtualFileSystem::_findFirstClusterInChain(ClusterIndexType clusterIndex, ClusterIndexType& startOfChainClusterIndex) {
		ClusterIndexType startClusterIndex = ClusterValues::INVALID_VALUE;
		ErrorCode err = _iterateThroughClusterChain(clusterIndex,
				[&startClusterIndex](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)doQuit; // Not used parameter

				if (cellValue.isStartOfChain()) {
					startClusterIndex = currentCluster;
					doQuit = true;
				}
				return ErrorCode::RESULT_OK;
			},
			false // Iterate backward
		);

		if (err == ErrorCode::RESULT_OK) {
			startOfChainClusterIndex = startClusterIndex;
		}

		return err;
	}
#endif //!defined(MCPE_PUBLISH)

	ErrorCode VirtualFileSystem::removeVolume() {
		return mVolumeManager.removeVolume();
	}

	ErrorCode VirtualFileSystem::_updatePosition(FileManipulator& fileManipulator) {
		ClusterIndexType newClusterIndex = ClusterValues::INVALID_VALUE;
		ErrorCode err = _getClusterForPosition(fileManipulator.mFileDescriptorRecord, fileManipulator.mNextPosition, newClusterIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		SFAT_ASSERT(isValidClusterIndex(newClusterIndex), "A valid cluster should exist in the range of the updated file size!");
		fileManipulator.mPosition = fileManipulator.mNextPosition;
		fileManipulator.mPositionClusterIndex = newClusterIndex;

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::_updatePosition(FileManipulator& fileManipulator, size_t nextWriteSize) {
	
		if (nextWriteSize > 0) {
			// The update is called in preparation for a write operation
			SFAT_ASSERT(fileManipulator.hasAccessMode(AccessMode::AM_WRITE), "The access mode should include AM_WRITE for a write operation!");

			// Expand the file if it is necessary.
			size_t fileSizeRequired = fileManipulator.mNextPosition + nextWriteSize;
			ErrorCode err = _expandFile(fileManipulator, fileSizeRequired);
			if (err == ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND) {
				// The volume can't be expanded enough for entire file-write, but perhaps the next file position could be set.
				if (sizeToPosition(fileManipulator.getFileSize()) > fileManipulator.mNextPosition) {
					// We are hiding the previous error, because we have still a chance to partially write the data, finishing positioning.
					err = _updatePosition(fileManipulator);
				}
				return err;
			}
			else if (err != ErrorCode::RESULT_OK) {
				// Exit on any other type of error, because we can't actually prepare even one cluster storage for writing.
				return err;
			}
		}

		if (fileManipulator.mPosition == fileManipulator.mNextPosition) {
			// There is nothing to do
			return ErrorCode::RESULT_OK;
		}

		if (nextWriteSize == 0) {
			// The update is called in preparation for a read operation

			if (fileManipulator.mNextPosition >= sizeToPosition(fileManipulator.getFileSize())) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "In read only mode the file-position can not exceed the file-size");
				return ErrorCode::ERROR_POSITIONING_OUT_OF_RANGE;
			}
		}

		return _updatePosition(fileManipulator);
	}

	ErrorCode VirtualFileSystem::_expandFile(FileManipulator& fileManipulator, size_t newSize) {
		if ((fileManipulator.mAccessMode & AM_WRITE) == 0) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't expand file opened only for reading!");
			return ErrorCode::ERROR_EXPANDING_FILE_IN_READ_ACCESS_MODE;
		}

		size_t currentFileSize = fileManipulator.getFileSize();
		if (currentFileSize >= newSize) {
			// Nothing to be done here.
			return ErrorCode::RESULT_OK;
		}

		uint32_t currentClusterCount = getCountClustersForSize(currentFileSize);
		uint32_t newClusterCount = getCountClustersForSize(newSize);
		bool useFileDataStorage = fileManipulator.getFileDescriptorRecord().isFile();

		ErrorCode err = ErrorCode::RESULT_OK;
		if (currentClusterCount < newClusterCount) {
			ClusterIndexType resultExpansionStart = ClusterValues::INVALID_VALUE;
			ClusterIndexType resultEndClusterIndex = ClusterValues::INVALID_VALUE;
			//TODO: Test how it will behave when the volume is full. Will it allocate a few clusters and fail after that, and leave the clusters not attached?
			err = _expandClusterChain(fileManipulator, newClusterCount - currentClusterCount, resultExpansionStart, resultEndClusterIndex, useFileDataStorage);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			// Update the start of the chain with the first allocated cluster if the file was previously empty.
			if (!isValidClusterIndex(fileManipulator.mFileDescriptorRecord.mStartCluster)) {
				fileManipulator.mFileDescriptorRecord.mStartCluster = resultExpansionStart;
				fileManipulator.mPosition = 0;
				fileManipulator.mPositionClusterIndex = fileManipulator.mFileDescriptorRecord.mStartCluster;
				
				//For debugging
				fileManipulator.mFileDescriptorRecord.mOldClusterTrace = fileManipulator.mFileDescriptorRecord.mStartCluster;

				// Update FAT-cell value for the first cluster in the chain to contain encoded the FileDescriptorRecord location.
				FATCellValueType cellValue;
				ClusterIndexType currentCluster = fileManipulator.mFileDescriptorRecord.mStartCluster;
				err = mVolumeManager.getFATCell(currentCluster, cellValue);
				if (err != ErrorCode::RESULT_OK) {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Wasn't able to read FAT cell #%x!", currentCluster);
				}
				if (err == ErrorCode::RESULT_OK) {
					uint32_t descriptorsPerCluster = mVolumeManager.getClusterSize() / mVolumeManager.getFileDescriptorRecordStorageSize();
					SFAT_ASSERT(descriptorsPerCluster < (1 << ClusterValues::FDRI_BITS_COUNT), "Can not encode the recordIndex in the file's first FATCell value.");
					cellValue.encodeFileDescriptorLocation(fileManipulator.getDescriptorLocation().mDescriptorClusterIndex, 
															fileManipulator.getDescriptorLocation().mRecordIndex % descriptorsPerCluster);
					err = mVolumeManager.setFATCell(currentCluster, cellValue);
					if (err != ErrorCode::RESULT_OK) {
						SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Wasn't able to set cluster #%x to END-OF-CHAIN!", currentCluster);
					}
				}
			}
			// Update the last cluster index
			fileManipulator.mFileDescriptorRecord.mLastCluster = resultEndClusterIndex;
		}
		
		if (err == ErrorCode::RESULT_OK) {
			fileManipulator.mFileDescriptorRecord.mFileSize = newSize;
			fileManipulator.mFileDescriptorRecord.mTimeModified = time(0);
			err = _writeFileDescriptor(fileManipulator);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Failed to update the FileDescriptorRecord!");
			}
		}
		else {
			// For some reason the expanding failed.
			// We can check how many clusters were able to add and update the fileSize
			if (err == ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND) {
				uint32_t countClusters = 0;
				ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
				ErrorCode errCounting = _getCountClusters(fileManipulator.getStartCluster(), countClusters, lastClusterIndex);
				if (errCounting == ErrorCode::RESULT_OK) {
					size_t reservedSize = countClusters * _getClusterSize();
					fileManipulator.mFileDescriptorRecord.mFileSize = std::min(newSize, reservedSize);
					fileManipulator.mFileDescriptorRecord.mLastCluster = lastClusterIndex;
					fileManipulator.mFileDescriptorRecord.mTimeModified = time(0);
					ErrorCode errWriteDescriptor = _writeFileDescriptor(fileManipulator);
					if (errWriteDescriptor != ErrorCode::RESULT_OK) {
						SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Failed to update the FileDescriptorRecord!");
					}
				}
				else {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Failed to count the allocated clusters for file!");
				}
			}
		}

		return err;
	}

	ErrorCode VirtualFileSystem::_getClusterForPosition(const FileDescriptorRecord& record, size_t position, ClusterIndexType& clusterIndex) {
		ClusterIndexType clusterInverseCounter = static_cast<uint32_t>(position / static_cast<size_t>(_getClusterSize()));
		ClusterIndexType foundClusterIndex = ClusterValues::INVALID_VALUE;
		ErrorCode err = _iterateThroughClusterChain(record.mStartCluster,
			[&foundClusterIndex, &clusterInverseCounter](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
			(void)cellValue; // Not used parameter

			if (clusterInverseCounter == 0) {
				foundClusterIndex = currentCluster;
				doQuit = true;
			}
			else {
				--clusterInverseCounter;
			}
			return ErrorCode::RESULT_OK;
		}
		);

		if (err == ErrorCode::RESULT_OK) {
			clusterIndex = foundClusterIndex;
		}

		return err;
	}

	FileDescriptorRecord* VirtualFileSystem::_getFileDescriptorRecordInCluster(uint8_t *clusterData, uint32_t relativeClusterIndex) {
		uint8_t* recordAddress = clusterData + relativeClusterIndex*getFileDescriptorRecordStorageSize();
		return reinterpret_cast<FileDescriptorRecord*>(recordAddress);
	}


	ErrorCode VirtualFileSystem::_writeFileDescriptor(const FileManipulator& fileManipulator) {
		auto handle = mMemoryBufferPool->acquireBuffer();
		auto& clusterDataBuffer = handle->get();

		ErrorCode err = mVolumeManager.readCluster(clusterDataBuffer, fileManipulator.mLocation.mDescriptorClusterIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		uint32_t relativeRecordIndex = fileManipulator.mLocation.mRecordIndex % _getRecordsPerCluster();
		FileDescriptorRecord* record = _getFileDescriptorRecordInCluster(clusterDataBuffer.data(), relativeRecordIndex);

		if (isInTransaction()) {
			err = mVolumeManager.logFileDescriptorChange(fileManipulator.mLocation.mDescriptorClusterIndex, *record, fileManipulator.mFileDescriptorRecord);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		*record = fileManipulator.mFileDescriptorRecord;

		err = mVolumeManager.writeCluster(clusterDataBuffer, fileManipulator.mLocation.mDescriptorClusterIndex);

		return err;
	}

	uint32_t VirtualFileSystem::_getRecordsPerCluster() const {
		return _getClusterSize() / getFileDescriptorRecordStorageSize();
	}

	bool VirtualFileSystem::fileExists(const PathString& path) {
		if (path.isEmpty()) {
			return false;
		}

		FileManipulator fileManipulator;
		ErrorCode err = createGenericFileManipulatorForFilePath(path, fileManipulator);
		if (err != ErrorCode::RESULT_OK) {
			return false;
		}
		return fileManipulator.isValid();
	}

	bool VirtualFileSystem::directoryExists(const PathString& path) {
		if (path.isEmpty() || path.isRoot()) {
			// An empty path is considered the Root directory.
			return true;
		}

		FileManipulator directoryFM;
		ErrorCode err = _createFileManipulatorForDirectoryPath(path, directoryFM);
		if (err != ErrorCode::RESULT_OK) {
			return false;
		}
		return directoryFM.isValid();
	}

	bool VirtualFileSystem::fileOrDirectoryExists(const PathString& path) {
		if (path.isEmpty() || path.isRoot()) {
			// An empty path is considered the Root directory.
			return true;
		}

		FileManipulator entityFM;
		ErrorCode err = createGenericFileManipulatorForExistingEntity(path, entityFM);
		if (err != ErrorCode::RESULT_OK) {
			return false;
		}
		return entityFM.isValid();
	}

	uint32_t VirtualFileSystem::getFileDescriptorRecordStorageSize() const {
		return mVolumeManager.getFileDescriptorRecordStorageSize();
	}

	uint32_t VirtualFileSystem::getCountClustersForSize(size_t size) const {
		return static_cast<uint32_t>((size + _getClusterSize() - 1) / static_cast<size_t>(_getClusterSize()));
	}


	ErrorCode VirtualFileSystem::_createRootDirFileManipulator(FileManipulator& fileManipulator) {
		//The Root directory doesn't have a parent-directory, so the location is irrelevant
		DescriptorLocation location;
		// The first record in the root directory will point to the root directory 
		location.mDescriptorClusterIndex = ClusterValues::ROOT_START_CLUSTER_INDEX; // ClusterValues::INVALID_VALUE;
		location.mDirectoryStartClusterIndex = ClusterValues::ROOT_START_CLUSTER_INDEX; // ClusterValues::INVALID_VALUE;
		location.mRecordIndex = 0;

		FileDescriptorRecord record;
		record.mAttributes = static_cast<uint32_t>(FileAttributes::BINARY) | static_cast<uint32_t>(FileAttributes::HIDDEN);
		strncpy(record.mEntityName, ".", sizeof(FileDescriptorRecord::mEntityName));
		record.mFileSize = 0; /// Irrelevant for directory
		record.mStartCluster = ClusterValues::ROOT_START_CLUSTER_INDEX;
		record.mLastCluster = ClusterValues::ROOT_START_CLUSTER_INDEX;
		record.mUniqueID = 0; /// Not used for now.
		record.mCRC = 0; /// Not used
		time_t localTime = time(0);
		record.mTimeCreated = localTime;
		record.mTimeModified = localTime;

		ErrorCode err = _createFileManipulatorForDirectory(location, record, fileManipulator);
		fileManipulator.mFullPath = "/";

		return err;
	}

	ErrorCode VirtualFileSystem::_createFileManipulatorForDirectory(const DescriptorLocation& location, const FileDescriptorRecord& record, FileManipulator& fileManipulator) {

		return _createFileManipulatorForExisting(location, record, AM_BINARY | AM_READ | AM_WRITE, fileManipulator);
	}

	ErrorCode VirtualFileSystem::_createFileManipulatorForExisting(const DescriptorLocation& location, const FileDescriptorRecord& record, uint32_t accessMode, FileManipulator& fileManipulator) {
		fileManipulator.mAccessMode = accessMode;

		// Store the location of the FileDescriptorRecord in case we need to update it.
		fileManipulator.mLocation = location;

		// Cache the FileDescriptorRecord
		fileManipulator.mFileDescriptorRecord = record;

		fileManipulator.mPosition = 0; // Set it to the start of the file
		fileManipulator.mNextPosition = fileManipulator.mPosition; /// No planed position movement
		fileManipulator.mPositionClusterIndex = fileManipulator.getFileDescriptorRecord().mStartCluster;

		if (record.isDirectory()) {
			// The fileSize of the directory is not saved in the descriptor, but can be calculated from the clustersCount
			uint32_t countClusters = 0;
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			if (fileManipulator.getFileDescriptorRecord().mStartCluster <= ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
				ErrorCode err = _getCountClusters(fileManipulator.getFileDescriptorRecord().mStartCluster, countClusters, lastClusterIndex);
				if (err != ErrorCode::RESULT_OK) {
					fileManipulator.mIsValid = false;
					return err;
				}
			}
			fileManipulator.mFileDescriptorRecord.mFileSize = countClusters * _getClusterSize();
			fileManipulator.mFileDescriptorRecord.mLastCluster = lastClusterIndex;
		}

		//Considering that the file/directory exists, the fileManipulator should be valid.
		fileManipulator.mIsValid = true;

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::createGenericFileManipulatorForFilePath(const PathString& filePath, FileManipulator& fileManipulator) {
		// Make the file-manipulator initially invalid.
		fileManipulator.mIsValid = false;

		FileManipulator fileFM;
		ErrorCode err = createGenericFileManipulatorForExistingEntity(filePath, fileFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (!fileFM.isValid() || fileFM.getFileDescriptorRecord().isDeleted()) {
			return ErrorCode::RESULT_OK;
		}

		if (fileFM.getFileDescriptorRecord().isFile()) {
			fileManipulator = std::move(fileFM);
		}
#if (SPLITFAT_ENABLE_WARNING_FOUND_DIRECTORY_INSTEAD_OF_FILE == 1)
		else {
			SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Found a directory instead of file - %s", filePath.c_str());
		}
#endif
		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::_createFileManipulatorForDirectoryPath(PathString directoryPath, FileManipulator& fileManipulator) {
		// Make the file-manipulator initially invalid.
		fileManipulator.mIsValid = false;
		
		FileManipulator directoryFM;
		ErrorCode err = createGenericFileManipulatorForExistingEntity(directoryPath, directoryFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (!directoryFM.isValid() || directoryFM.getFileDescriptorRecord().isDeleted()) {
			return ErrorCode::RESULT_OK;
		}

		if (directoryFM.getFileDescriptorRecord().isFile()) {
			SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Found a file instead of directory - %s", directoryPath.c_str());
		}
		else {
			fileManipulator = std::move(directoryFM);
		}

		fileManipulator.mAccessMode = AccessMode::AM_BINARY | AccessMode::AM_READ | AccessMode::AM_WRITE;

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::createGenericFileManipulatorForExistingEntity(PathString entiryPath, FileManipulator& fileManipulator) {
		if (entiryPath.isEmpty() || entiryPath.isRoot()) {
			return _createRootDirFileManipulator(fileManipulator);
		}

		// Make it initially invalid.
		fileManipulator.mIsValid = false;

		FileManipulator parentDirFM;
		ErrorCode err = _createRootDirFileManipulator(parentDirFM);

		std::string entityName = entiryPath.getFirstPathEntity();
		SFAT_ASSERT(entityName.size() > 0, "The entity name is not supposed to be empty!");
		FileManipulator entityFM;

		uint32_t countNestedDirectories = 0;

		SFAT_ASSERT(parentDirFM.mFullPath.getLength() > 0, "The root path should be updated!");

		do {
			err = _findRecordInDirectory(parentDirFM, entityName, entityFM);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			if (!entityFM.isValid()) {
				// There is no error, but the fileManupilator will remain invalid, showing that the entity wasn't found.
				return ErrorCode::RESULT_OK;
			}

			std::string nextEntityName = entiryPath.getNextPathEntity();
			if (nextEntityName.empty()) {
				// All sub-directories of the entiryPath were found, so we have the final file manipulator in entityFM. Transfer it to fileManipulator.
				fileManipulator = std::move(entityFM);
				return ErrorCode::RESULT_OK;
			}

			// If we have to continue, the current entity should be a directory!
			if (!entityFM.getFileDescriptorRecord().isDirectory()) {
				SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Found a file instead of directory - %s", entiryPath.getCurrentPath().c_str());
				return ErrorCode::RESULT_OK;
			}

			// Transfer
			parentDirFM = std::move(entityFM);
			// Grant a read-access for the directory, which is necessary later to read from it with _findRecordInDirectory().
			parentDirFM.mAccessMode = AccessMode::AM_BINARY | AccessMode::AM_READ;
			entityName = std::move(nextEntityName);

			++countNestedDirectories;

		} while (countNestedDirectories < kMaxCountNestedDirectories);

		if (countNestedDirectories >= kMaxCountNestedDirectories) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Reached the maximum allowed depth of nested directories (%u)!", kMaxCountNestedDirectories);
			return ErrorCode::ERROR_REACHED_MAX_DIRECTORY_DEPTH;
		}

		return ErrorCode::RESULT_OK;
	}


	ErrorCode VirtualFileSystem::_findRecordInDirectory(FileManipulator& parentDirFM, const std::string& entityName, FileManipulator& outputFileManipulator) {

		outputFileManipulator.mIsValid = false;

		if (!parentDirFM.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the parent directory is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		size_t fileSize = 0;
		ErrorCode err = _getFileSize(parentDirFM, fileSize);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		err = seek(parentDirFM, 0, SeekMode::SM_SET); // Move to the begining of the directory
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		size_t sizeToRead = getFileDescriptorRecordStorageSize();
		size_t sizeRead = 0;
		std::vector<uint8_t> buffer(sizeToRead);
		uint32_t countRecords = 0;

		SFAT_ASSERT(parentDirFM.mFullPath.getLength() > 0, "The full path should be available here!");

		// Start reading the records in loop
		while (countRecords < kMaxCountEntitiesInDirectory)
		{
			err = read(parentDirFM, buffer.data(), sizeToRead, sizeRead);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			if (sizeToRead != sizeRead) {
				// Wasn't able to find anything!
				break;
			}

			const FileDescriptorRecord* record = reinterpret_cast<FileDescriptorRecord*>(buffer.data());
			if (record->isEmpty()) {
				// No more records in the directory.
				break;
			}

			if (!record->isDeleted() && !record->checkAttribute(FileAttributes::HIDDEN) && record->isSameName(entityName)) {
				// The name matches! Create file-manipulator for it.

				DescriptorLocation location;
				location.mDescriptorClusterIndex = parentDirFM.mPositionClusterIndex;
				location.mDirectoryStartClusterIndex = parentDirFM.getFileDescriptorRecord().mStartCluster;
				location.mRecordIndex = countRecords;

				err = _createFileManipulatorForExisting(location, *record, AM_UNSPECIFIED, outputFileManipulator);
				// Update the full path to the current entity
				outputFileManipulator.mFullPath = PathString::combinePath(parentDirFM.mFullPath, record->mEntityName);

				return err;
			}

			++countRecords;
		}
		SFAT_ASSERT(countRecords <= kMaxCountEntitiesInDirectory, "Not supposed to reach the maximal count FileDescriptorRecords!");

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::_iterateThroughDirectory(FileManipulator& parentDirFM, DirectoryIterationCallbackInternal callback) {

		if (!parentDirFM.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the parent directory is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		ErrorCode err = seek(parentDirFM, 0, SeekMode::SM_SET); // Move to the begining of the directory
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		size_t sizeToRead = getFileDescriptorRecordStorageSize();
		size_t sizeRead = 0;
		std::vector<uint8_t> buffer(sizeToRead);
		uint32_t countRecords = 0;
		bool doQuit = false;
		DescriptorLocation location;
		location.mDirectoryStartClusterIndex = parentDirFM.getFileDescriptorRecord().mStartCluster;

		// Start reading the record in loop
		while ((countRecords < kMaxCountEntitiesInDirectory) && (!doQuit)) {
			err = read(parentDirFM, buffer.data(), sizeToRead, sizeRead);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			if (sizeToRead != sizeRead) {
				// Finished the iteration
				break;
			}

			const FileDescriptorRecord* record = reinterpret_cast<FileDescriptorRecord*>(buffer.data());
			if (!record->isHidden()) {
				location.mDescriptorClusterIndex = parentDirFM.mPositionClusterIndex;
				location.mRecordIndex = countRecords;
				PathString fullPath = PathString::combinePath(parentDirFM.mFullPath, record->mEntityName);
				err = callback(doQuit, location, *record, fullPath.getString());
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
			}

			++countRecords;
		}
		SFAT_ASSERT(countRecords <= kMaxCountEntitiesInDirectory, "Not supposed to reach the maximal count FileDescriptorRecords!");

		return ErrorCode::RESULT_OK;
	}


	ErrorCode VirtualFileSystem::_getFileSize(const FileManipulator& fileManipulator, size_t& fileSize) const {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the file is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		fileSize = fileManipulator.getFileDescriptorRecord().mFileSize;

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::_getCountClusters(ClusterIndexType startClusterIndex, uint32_t& countClusters, ClusterIndexType& lastClusterIndex) {
		countClusters = 0;
		lastClusterIndex = ClusterValues::INVALID_VALUE;
		if (!isValidClusterIndex(startClusterIndex)) {
			// The chain is not created yet.
			return ErrorCode::RESULT_OK;
		}

		ErrorCode err = _iterateThroughClusterChain(startClusterIndex,
			[&countClusters, &lastClusterIndex](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
			(void)doQuit; // Not used parameter
			(void)cellValue; // Not used parameter

			++countClusters;
			lastClusterIndex = currentCluster;
			return ErrorCode::RESULT_OK;
		}
		);

		return err;
	}

	ErrorCode VirtualFileSystem::read(FileManipulator& fileManipulator, void* buffer, size_t sizeToRead, size_t& sizeRead) {
		sizeRead = 0;
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the file is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (!fileManipulator.hasAccessMode(AccessMode::AM_READ)) {
			return ErrorCode::ERROR_TRYING_TO_READ_FILE_WITHOUT_READ_ACCESS_MODE;
		}

		if (buffer == nullptr) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Null pointer memory buffer!");
			return ErrorCode::ERROR_NULL_POINTER_MEMORY_BUFFER;
		}

		if ((fileManipulator.mNextPosition >= sizeToPosition(fileManipulator.getFileSize())) || (sizeToRead == 0)) {
			// The next read is completely out size of the file size range.
			return ErrorCode::RESULT_OK;
		}

		ErrorCode err = _updatePosition(fileManipulator, 0);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (fileManipulator.mPosition + sizeToRead > fileManipulator.getFileSize()) {
			sizeToRead = fileManipulator.getFileSize() - fileManipulator.mPosition;
		}

		SFAT_ASSERT(fileManipulator.mPosition < sizeToPosition(fileManipulator.getFileSize()), "When reading a file, the position value should be less than the file-size!"); //The _updatePosition() function called above should return an error.

		uint32_t relativeStartCluster = static_cast<uint32_t>(fileManipulator.mPosition / _getClusterSize());
		uint32_t relativeEndCluster = static_cast<uint32_t>((fileManipulator.mPosition + sizeToRead - 1) / _getClusterSize());
		uint32_t countClustersToRead = relativeEndCluster - relativeStartCluster + 1;
		uint32_t clusterReadOffset = fileManipulator.mPosition % _getClusterSize();
		size_t	bytesRemainedToCopy = sizeToRead;
		uint8_t* outputBuffer = reinterpret_cast<uint8_t*>(buffer);

		std::vector<uint8_t>& clusterData = fileManipulator.getBuffer(_getClusterSize());

		uint32_t countClustersRead = 0;
		err = _iterateThroughClusterChain(fileManipulator.mPositionClusterIndex,
			[&outputBuffer, &bytesRemainedToCopy, &clusterReadOffset, &clusterData, &countClustersRead, countClustersToRead, this](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)cellValue; // Not used parameter

				// Read the cluster
				ErrorCode err = mVolumeManager.readCluster(clusterData, currentCluster);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}

				// Check how much exactly data to copy and copy it.
				uint32_t bytesToCopy = _getClusterSize() - clusterReadOffset;
				if (static_cast<size_t>(bytesToCopy) > bytesRemainedToCopy) {
					bytesToCopy = static_cast<uint32_t>(bytesRemainedToCopy);
				}
				memcpy(outputBuffer, clusterData.data() + clusterReadOffset, bytesToCopy);

				// Update the counters
				outputBuffer += bytesToCopy;
				bytesRemainedToCopy -= static_cast<size_t>(bytesToCopy);
				clusterReadOffset = 0;
				++countClustersRead;
				if (countClustersRead >= countClustersToRead) {
					doQuit = true;
				}
				return ErrorCode::RESULT_OK;
			}
		);

#if !defined(MCPE_PUBLISH)
		if (err != ErrorCode::RESULT_OK) {
			_logReadingError(err, fileManipulator);
		}
#endif //!defined(MCPE_PUBLISH)

		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Update the file position
		err = seek(fileManipulator, static_cast<int>(sizeToRead), SeekMode::SM_CURRENT);
		sizeRead = sizeToRead;

		return err;
	}

	uint32_t VirtualFileSystem::_getClusterSize() const {
		return mClusterSize;
	}

	ErrorCode VirtualFileSystem::seek(FileManipulator& fileManipulator, FilePositionType offset, SeekMode mode) {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the file is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		switch (mode) {
			case SeekMode::SM_CURRENT: {
				if ((offset < 0) && (fileManipulator.mPosition -offset)) {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The sum (mPosition + offset) should be positive or zero!");
					return ErrorCode::ERROR_INVALID_SEEK_PARAMETERS;
				}
				fileManipulator.mNextPosition = fileManipulator.mPosition + offset;
			} break;
			case SeekMode::SM_END: {
				if ((offset < 0) && (fileManipulator.getFileSize() < static_cast<size_t>(-offset))) {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The sum (fileSize + offset) should be positive or zero!");
					return ErrorCode::ERROR_INVALID_SEEK_PARAMETERS;
				}
				fileManipulator.mNextPosition = fileManipulator.getFileSize() + offset;
			} break;
			case SeekMode::SM_SET: {
				if (offset < 0) {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The offset should be positive or zero!");
					return ErrorCode::ERROR_INVALID_SEEK_PARAMETERS;
				}
				fileManipulator.mNextPosition = offset;
			} break;
		}

		SFAT_ASSERT(fileManipulator.mNextPosition >= 0, "The scheduled position should never be negative!");

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::write(FileManipulator& fileManipulator, const void* buffer, size_t sizeToWrite, size_t& sizeWritten) {
		sizeWritten = 0;
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the file is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

#if (SPLIT_FAT_WARN_FOR_WRITE_OUT_OF_TRANSACTION == 1)
		if (!isInTransaction()) {
			SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Writing outside of transaction! File: %s", fileManipulator.mFullPath.c_str());
		}
#endif //(SPLIT_FAT_WARN_FOR_WRITE_OUT_OF_TRANSACTION == 1)

		if (sizeToWrite == 0) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "File write with write size 0 is called!");
			return ErrorCode::RESULT_OK;
		}

		if (buffer == nullptr) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Null pointer memory buffer!");
			return ErrorCode::ERROR_NULL_POINTER_MEMORY_BUFFER;
		}

		// If the access mode is "append" move first the position to the end of the file.
		if (fileManipulator.hasAccessMode(AccessMode::AM_APPEND)) {
			ErrorCode err = seek(fileManipulator, 0, SeekMode::SM_END);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		// Allocates the amount of required clusters in the cluster chain and prepares for writing at the specific position
		ErrorCode err = _updatePosition(fileManipulator, sizeToWrite);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		//SplitFAT debug only
#if !defined(MCPE_PUBLISH)
		if ((sizeToWrite > 0) && (fileManipulator.mPosition == 0) &&
			(fileManipulator.getFileDescriptorRecord().isSameName("manifest.json")) &&
			(*((char*)buffer) == 0)) {
			SFAT_LOGI(LogArea::LA_VIRTUAL_DISK, "Writing into manifest file.");
		}
#endif //!defined(MCPE_PUBLISH)

		uint32_t relativeStartCluster = static_cast<uint32_t>(fileManipulator.mPosition / _getClusterSize());
		uint32_t relativeEndCluster = static_cast<uint32_t>((fileManipulator.mPosition + sizeToWrite - 1) / _getClusterSize());
		uint32_t countClustersToWrite = relativeEndCluster - relativeStartCluster + 1;
		uint32_t clusterWriteOffset = fileManipulator.mPosition % _getClusterSize();
		size_t	bytesRemainedToCopy = sizeToWrite;
		const uint8_t* inputBuffer = reinterpret_cast<const uint8_t*>(buffer);

		std::vector<uint8_t>& clusterData = fileManipulator.getBuffer(_getClusterSize());

		uint32_t countClustersWritten = 0;
		err = _iterateThroughClusterChain(fileManipulator.mPositionClusterIndex,
			[&sizeWritten, &fileManipulator, &inputBuffer, &bytesRemainedToCopy, &clusterWriteOffset, &clusterData, &countClustersWritten, countClustersToWrite, this](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
			(void)cellValue; // Not used parameter

			ErrorCode err = ErrorCode::RESULT_OK;

			// Check how much exactly data to copy at the current iteration.
			uint32_t bytesToCopy = _getClusterSize() - clusterWriteOffset;
			if (static_cast<size_t>(bytesToCopy) > bytesRemainedToCopy) {
				bytesToCopy = static_cast<uint32_t>(bytesRemainedToCopy);
			}

			size_t currentlyRequiredSize = fileManipulator.mPosition + sizeWritten + bytesToCopy;

			if (fileManipulator.getFileSize() < currentlyRequiredSize) {
				// There is not enough space
				doQuit = true;
				return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
			}

			if ((bytesToCopy != _getClusterSize()) && (cellValue.isClusterInitialized())) {
				// We don't write full cluster, so we have to merge the new content with the old one.
				// Read the cluster
				err = mVolumeManager.readCluster(clusterData, currentCluster);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
			}

			memcpy(clusterData.data() + clusterWriteOffset, inputBuffer, bytesToCopy);
			err = mVolumeManager.writeCluster(clusterData, currentCluster);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			err = seek(fileManipulator, static_cast<FilePositionType>(currentlyRequiredSize), SeekMode::SM_SET);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			// Update the counters
			inputBuffer += bytesToCopy;
			bytesRemainedToCopy -= static_cast<size_t>(bytesToCopy);
			clusterWriteOffset = 0;
			++countClustersWritten;
			sizeWritten += bytesToCopy;
			if (countClustersWritten >= countClustersToWrite) {
				doQuit = true;
			}
			return err;
		}
		);

		return err;
	}

	ErrorCode VirtualFileSystem::_createEntity(FileManipulator& parentDirFM, const std::string& entityName, uint32_t accessMode, uint32_t attributes, FileManipulator& outputFileManipulator) {

		SFAT_ASSERT((accessMode & AccessMode::AM_WRITE) != 0, "Creating new file requires access mode AM_WRITE!");

		if (!parentDirFM.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator for the parent directory is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		PathString normalizedPath(entityName);
		std::string normalizedEntityName = normalizedPath.getName();
		{
			FileManipulator existingEntityFM;
			ErrorCode err = _findRecordInDirectory(parentDirFM, normalizedEntityName, existingEntityFM);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			if (existingEntityFM.isValid()) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Can't create an entity! Such name already exsists in the same directory!");
				return ErrorCode::ERROR_FILE_OR_DIRECTORY_WITH_SAME_NAME_ALREADY_EXISTS;
			}
		}

		uint32_t selectedRecordIndex = kInvalidDirectoryEntityIndex;
		outputFileManipulator.mIsValid = false;
		// Try to find an empty (or not used) FileDescriptorRecord in already allocated cluster of the parent directory.
		ErrorCode err = _iterateThroughDirectory(parentDirFM, [&selectedRecordIndex](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			(void)fullPath; // Not used
			if (record.isEmpty() || record.isDeleted()) {
				selectedRecordIndex = location.mRecordIndex; //The internal record index;
				doQuit = true;
			}
			return ErrorCode::RESULT_OK;
		});

		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Check if there was free FileDescriptorRecord found. If not, we have to expand the directory with another cluster in the chain.
		if (selectedRecordIndex == kInvalidDirectoryEntityIndex) {
			//
			// Expand the parent directory if it is possible.

			SFAT_ASSERT((parentDirFM.getFileSize() % _getClusterSize()) == 0, "The file-size of a directory should be multiple of the cluster size!");

			// Check first if we are allowed to expand the directory
			uint32_t countRecords = static_cast<uint32_t>(parentDirFM.getFileSize() / getFileDescriptorRecordStorageSize());
			if (countRecords >= kMaxCountEntitiesInDirectory) {
				return ErrorCode::ERROR_MAXIMUM_ALLOWED_FILES_PER_DIRECTORY_REACHED;
			}
			err = _expandFile(parentDirFM, parentDirFM.getFileSize() + _getClusterSize());
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			selectedRecordIndex = countRecords;
		}

		SFAT_ASSERT(isValidClusterIndex(selectedRecordIndex), "The recordClusterIndex should be valid!");

		uint32_t descriptorClusterIndex = ClusterValues::INVALID_VALUE;
		err = _getClusterForPosition(parentDirFM.getFileDescriptorRecord(), selectedRecordIndex*getFileDescriptorRecordStorageSize(), descriptorClusterIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		FileManipulator fm;
		fm.mAccessMode = accessMode;

		fm.mFileDescriptorRecord.mAttributes = attributes;
		strncpy(fm.mFileDescriptorRecord.mEntityName, normalizedEntityName.c_str(), sizeof(FileDescriptorRecord::mEntityName));
		fm.mFileDescriptorRecord.mFileSize = 0;
		fm.mFileDescriptorRecord.mStartCluster = ClusterValues::INVALID_VALUE; // There is no cluster allocated because the file-size is 0
		fm.mFileDescriptorRecord.mLastCluster = ClusterValues::INVALID_VALUE; // There is no last cluster either
		fm.mFileDescriptorRecord.mUniqueID = 0; //TODO: Set with generated unique number
		fm.mFileDescriptorRecord.mCRC = 0x0; // Not yet used
		time_t localTime = time(0);
		fm.mFileDescriptorRecord.mTimeCreated = localTime;
		fm.mFileDescriptorRecord.mTimeModified = localTime;

		//To be used for debugging
		fm.mFileDescriptorRecord.mOldClusterTrace = ClusterValues::INVALID_VALUE;

		fm.mPosition = 0;
		fm.mNextPosition = fm.mPosition; // No planed movement
		fm.mPositionClusterIndex = fm.mFileDescriptorRecord.mStartCluster;

		fm.mLocation.mDescriptorClusterIndex = descriptorClusterIndex;
		fm.mLocation.mDirectoryStartClusterIndex = parentDirFM.getStartCluster();
		fm.mLocation.mRecordIndex = selectedRecordIndex;

		fm.mFullPath = PathString::combinePath(parentDirFM.mFullPath, fm.mFileDescriptorRecord.mEntityName);

		err = _writeFileDescriptor(fm);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		fm.mIsValid = true;
		outputFileManipulator = std::move(fm);

		return err;
	}

	ErrorCode VirtualFileSystem::_createEntity(const PathString& path, uint32_t mAccessMode, uint32_t attributes, FileManipulator& outputFileManipulator) {
		outputFileManipulator.mIsValid = false;

		if (path.isEmpty() || path.isRoot()) {
			return ErrorCode::ERROR_FILE_OR_DIRECTORY_WITH_SAME_NAME_ALREADY_EXISTS;
		}

		PathString parentDirectoryPath(path.getParentPath());
		FileManipulator parentDirFM;
		ErrorCode err = _createFileManipulatorForDirectoryPath(parentDirectoryPath, parentDirFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (!parentDirFM.isValid()) {
			return ErrorCode::ERROR_PARENT_DIRECTORY_DOES_NOT_EXIST;
		}

		return _createEntity(parentDirFM, path.getName(), mAccessMode, attributes, outputFileManipulator);
	}

	ErrorCode VirtualFileSystem::createFile(const PathString& filePath, uint32_t accessMode, bool isBinaryFile, FileManipulator& outputFileManipulator) {
		return _createEntity(filePath,
			accessMode | AccessMode::AM_WRITE,
			static_cast<uint32_t>(FileAttributes::FILE) | (isBinaryFile ? static_cast<uint32_t>(FileAttributes::BINARY) : 0),
			outputFileManipulator);
	}

	ErrorCode VirtualFileSystem::createDirectory(const PathString& directiryPath, FileManipulator& outputFileManipulator) {
		return _createEntity(directiryPath,
			AccessMode::AM_BINARY | AccessMode::AM_READ | AccessMode::AM_WRITE,
			static_cast<uint32_t>(FileAttributes::BINARY),
			outputFileManipulator);
	}

	ErrorCode VirtualFileSystem::_trunc(FileManipulator& fileManipulator, size_t newSize, bool deleteIfEmpty /*= false*/) {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		// If it is directory and we have to remove it, check if the directory is empty first.
		if (deleteIfEmpty && (newSize == 0) && fileManipulator.getFileDescriptorRecord().isDirectory()) {
			bool bRes = false;
			ErrorCode err = _isDirectoryEmpty(fileManipulator, bRes);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			if (!bRes) {
				return ErrorCode::ERROR_CANT_REMOVE_NOT_EMPTY_DIRECTORY;
			}
		}

		if (newSize > fileManipulator.getFileSize()) {
			// Can't truncate to a bigger size
			return ErrorCode::ERROR_CAN_NOT_TRUNCATE_FILE_TO_BIGGER_SIZE;
		}

		ClusterIndexType newLastClusterIndex = ClusterValues::INVALID_VALUE; // Free entire cluster chain if this value is kept in newLastClusterIndex
		ClusterIndexType clusterIndexToStartFrom = fileManipulator.getStartCluster();

		ErrorCode err = ErrorCode::RESULT_OK;
		if (newSize > 0) {
			err = _getClusterForPosition(fileManipulator.getFileDescriptorRecord(), newSize - 1, newLastClusterIndex);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			clusterIndexToStartFrom = newLastClusterIndex;
		}

		const DescriptorLocation& location = fileManipulator.getDescriptorLocation();

		if (clusterIndexToStartFrom <= ClusterValues::LAST_CLUSTER_INDEX_VALUE) {
			err = _iterateThroughClusterChain(clusterIndexToStartFrom,
				[&location, newLastClusterIndex, this](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)doQuit; // Not used parameter

				ErrorCode err = ErrorCode::RESULT_OK;
				if (newLastClusterIndex == currentCluster) {
					cellValue.makeEndOfChain();
					uint32_t descriptorsPerCluster = mVolumeManager.getClusterSize() / mVolumeManager.getFileDescriptorRecordStorageSize();
					cellValue.encodeFileDescriptorLocation(location.mDescriptorClusterIndex, location.mRecordIndex % descriptorsPerCluster);					err = mVolumeManager.setFATCell(currentCluster, cellValue);
					if (err != ErrorCode::RESULT_OK) {
						SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Wasn't able to set cluster #%x to END-OF-CHAIN!", currentCluster);
					}
				}
				else {
					FATCellValueType freeCellValue = FATCellValueType::freeCellValue();
					err = mVolumeManager.setFATCell(currentCluster, freeCellValue);
					if (err != ErrorCode::RESULT_OK) {
						SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Wasn't able to free cluster #%x", currentCluster);
					}
				}
				return err;
			}
			);
		}

		if (err == ErrorCode::RESULT_OK) {
			if (deleteIfEmpty && (newSize == 0)) {
				fileManipulator.mFileDescriptorRecord.mAttributes |= static_cast<uint32_t>(FileAttributes::DELETED);
				
				// Note that we are currently leaving the rest of the information as a trace to the deleted file or directory. This may not be necessary, but it's not wrong.
			}
			else {
				fileManipulator.mFileDescriptorRecord.mFileSize = newSize;
				fileManipulator.mFileDescriptorRecord.mTimeModified = time(0);
				fileManipulator.mFileDescriptorRecord.mLastCluster = newLastClusterIndex;
				if (newSize == 0) {
					fileManipulator.mFileDescriptorRecord.mStartCluster = ClusterValues::INVALID_VALUE; // Entire cluster chain is released, so reset this value 
					SFAT_ASSERT(fileManipulator.mFileDescriptorRecord.mLastCluster == ClusterValues::INVALID_VALUE, "The last cluster should be already set to invalid");
					fileManipulator.mFileDescriptorRecord.mOldClusterTrace = ClusterValues::INVALID_VALUE;
				}
			}
			err = _writeFileDescriptor(fileManipulator);
		}

		return err;
	}

	ErrorCode VirtualFileSystem::truncateFile(FileManipulator& fileManipulator, size_t newSize) {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (!fileManipulator.getFileDescriptorRecord().isFile()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator does not represent a file!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		return _trunc(fileManipulator, newSize, false);
	}

	ErrorCode VirtualFileSystem::_deleteFile(FileManipulator& fileManipulator) {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (!fileManipulator.getFileDescriptorRecord().isFile()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator does not represent a file!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		return _trunc(fileManipulator, 0, true);
	}

	ErrorCode VirtualFileSystem::_removeDirectory(FileManipulator& fileManipulator) {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (!fileManipulator.getFileDescriptorRecord().isDirectory()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator does not represent a directory!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (fileManipulator.isRootDirectory()) {
			return ErrorCode::ERROR_CAN_NOT_DELETE_ROOT_DIRECTORY;
		}

		return _trunc(fileManipulator, 0, true);
	}

	ErrorCode VirtualFileSystem::_isDirectoryEmpty(FileManipulator& fileManipulator, bool& result) {
		if (!fileManipulator.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (!fileManipulator.getFileDescriptorRecord().isDirectory()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator does not represent a directory!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		bool foundEntity = false;
		// Try to find an empty (or not used) FileDescriptorRecord in already allocated cluster of the parent directory.
		ErrorCode err = _iterateThroughDirectory(fileManipulator, [&foundEntity](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			(void)fullPath; // Not used
			(void)location; // Not used
			if (!record.isEmpty() && !record.isDeleted()) {
				foundEntity = true;
				doQuit = true;
			}
			return ErrorCode::RESULT_OK;
		});

		if (err == ErrorCode::RESULT_OK) {
			result = !foundEntity;
		}
		return err;
	}

	ErrorCode VirtualFileSystem::deleteFile(const PathString& filePath) {
		FileManipulator fm;
		ErrorCode err = createGenericFileManipulatorForFilePath(filePath, fm);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return _deleteFile(fm);
	}

	ErrorCode VirtualFileSystem::removeDirectory(const PathString& directoryPath) {
		FileManipulator fm;
		ErrorCode err = _createFileManipulatorForDirectoryPath(directoryPath, fm);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return _removeDirectory(fm);
	}

	ErrorCode VirtualFileSystem::flush(FileManipulator& fileManipulator) {
		ErrorCode err = ErrorCode::RESULT_OK;
		// Flushes the FAT on closing of file with a write access mode. Could be slow for many small files.
		if (fileManipulator.hasAccessMode(AccessMode::AM_WRITE)) {
			err = mVolumeManager.flush();
		}
		return err;
	}

	ErrorCode VirtualFileSystem::_renameEntity(const PathString& entityPath, const PathString& newName) {
		FileManipulator directoryFM;
		PathString directoryPath(entityPath.getParentPath());
		ErrorCode err = _createFileManipulatorForDirectoryPath(directoryPath, directoryFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (!directoryFM.isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file-manipulator is invalid!");
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		bool foundEntity = false;
		std::string newNameStr = newName.getName();
		// Check if there isn't an existing file or directory with same name.
		err = _iterateThroughDirectory(directoryFM, [&newNameStr, &foundEntity](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			(void)location; // Not used
			(void)fullPath; // Not used
			if (!record.isEmpty() && !record.isDeleted() && !record.isHidden() && record.isSameName(newNameStr)) {
				foundEntity = true;
				doQuit = true;
			}
			return ErrorCode::RESULT_OK;
		});

		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (foundEntity) {
			return ErrorCode::ERROR_CANT_RENAME_A_FILE__NAME_DUPLICATION;
		}

		FileManipulator entityFM;
		err = _findRecordInDirectory(directoryFM, entityPath.getName(), entityFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (entityFM.isValid()) {
			memset(entityFM.mFileDescriptorRecord.mEntityName, 0, sizeof(FileDescriptorRecord::mEntityName));
			strncpy(entityFM.mFileDescriptorRecord.mEntityName, newNameStr.c_str(), sizeof(FileDescriptorRecord::mEntityName));
			err = _writeFileDescriptor(entityFM);
		}

		return err;
	}

	ErrorCode VirtualFileSystem::renameFile(const PathString& filePath, const PathString& newName) {
		return _renameEntity(filePath, newName);
	}

	ErrorCode VirtualFileSystem::renameDirectory(const PathString& directoryPath, const PathString& newName) {
		return _renameEntity(directoryPath, newName);
	}

	ErrorCode VirtualFileSystem::getCountFreeClusters(uint32_t& countFreeClusters, uint32_t blockIndex) {
		return mVolumeManager.getCountFreeClusters(countFreeClusters, blockIndex);
	}

	ErrorCode VirtualFileSystem::iterateThroughDirectory(const PathString& directoryPath, uint32_t flags, DirectoryIterationCallback callback) {
		ErrorCode err = _iterateThroughDirectoryRecursively(directoryPath, flags, [&callback](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			(void)location;
			ErrorCode callBackError = callback(doQuit, record, fullPath);
			return callBackError;
		});
		return err;
	}

	ErrorCode VirtualFileSystem::_iterateThroughDirectoryRecursively(const PathString& directoryPath, uint32_t flags, DirectoryIterationCallbackInternal callback) {
		FileManipulatorStack fmStack;
		StackAutoElement autoStack(fmStack);
		FileManipulator& directoryFM = autoStack.getTop();
		ErrorCode err = _createFileManipulatorForDirectoryPath(directoryPath, directoryFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (!directoryFM.isValid()) {
			return ErrorCode::ERROR_DIRECTORY_NOT_FOUND;
		}

		autoStack.getTop().mFullPath = directoryPath;

		return _iterateThroughDirectoryRecursively(fmStack, flags, callback);
	}

	ErrorCode VirtualFileSystem::_iterateThroughDirectoryRecursively(FileManipulatorStack& fmStack, uint32_t flags, DirectoryIterationCallbackInternal callback) {
		if (fmStack.empty() || !fmStack.top().isValid()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Directory \"%s\" not found!", fmStack.top().mFullPath.c_str());
			return ErrorCode::ERROR_DIRECTORY_NOT_FOUND;
		}

		FileManipulator& parentDirFM = fmStack.top();
		SFAT_ASSERT(parentDirFM.getFileDescriptorRecord().isDirectory(), "The entity should be a directory!");

		ErrorCode err = _iterateThroughDirectory(parentDirFM, [this, &flags, &callback, &fmStack](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			if (record.isEmpty()) {
				doQuit = true;
			}
			else if (!record.isDeleted()) {

				bool shouldExecuteCallback = record.isFile() && ((flags & DI_FILE) != 0);
				shouldExecuteCallback |= record.isDirectory() && ((flags & DI_DIRECTORY) != 0);
				if (shouldExecuteCallback) {
					ErrorCode callBackError = callback(doQuit, location, record, fullPath);
					if (callBackError != ErrorCode::RESULT_OK) {
						return callBackError;
					}
				}

				// Iterate recusively through a sub-directory
				if (record.isDirectory() && ((flags & DI_RECURSIVE) != 0)) {
					StackAutoElement autoStack(fmStack);
					FileManipulator& subdirFileManipulator = autoStack.getTop();
					ErrorCode err = _createFileManipulatorForDirectory(location, record, subdirFileManipulator);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}
					subdirFileManipulator.mFullPath = fullPath;
					ErrorCode recursiveIterationError = _iterateThroughDirectoryRecursively(fmStack, flags, callback);
					if (recursiveIterationError != ErrorCode::RESULT_OK) {
						return recursiveIterationError;
					}
				}
			}

			return ErrorCode::RESULT_OK;
		});

		return err;
	}


	//
	// Transaction control functions
	//
	bool VirtualFileSystem::isInTransaction() const {
		return mVolumeManager.isInTransaction();
	}

	ErrorCode VirtualFileSystem::startTransaction() {
#if (SPLIT_FAT_ENABLE_DEFRAGMENTATION == 1)
		ErrorCode err = mDefragmentation->prepareForWriteTransaction();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Failed to prepare for defragmentation!");
		}
#endif
		return mVolumeManager.startTransaction();
	}

	ErrorCode VirtualFileSystem::endTransaction() {
#if (SPLIT_FAT_ENABLE_DEFRAGMENTATION == 1)
		if (mDefragmentation->isActive()) {
			ErrorCode localErr = mDefragmentation->performDefragmentaionOnTransactionEnd();
			if (localErr != ErrorCode::RESULT_OK) {
				// The failure of the defragmentation should be still safe to commit.
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Defragmentation failed!");
			}
		}
#endif
		return mVolumeManager.endTransaction();
	}

	ErrorCode VirtualFileSystem::tryRestoreFromTransactionFile() {
		return mVolumeManager.tryRestoreFromTransactionFile();
	}

	ErrorCode VirtualFileSystem::getFreeSpace(FileSizeType& countFreeBytes) {
		return mVolumeManager.getFreeSpace(countFreeBytes);
	}


	//
	// Defragmentation and recovery functions
	//
	ErrorCode VirtualFileSystem::moveCluster(ClusterIndexType sourceClusterIndex, ClusterIndexType destClusterIndex) {
		if (sourceClusterIndex == destClusterIndex) {
			SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Can't move a cluster! The source cluster is the same as the destination!");
			return ErrorCode::RESULT_OK;
		}

		if (!isValidClusterIndex(destClusterIndex) || !isValidClusterIndex(sourceClusterIndex)) {
			SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "Can't move a cluster! Invalid source or destination cluster index!");
			return ErrorCode::RESULT_OK;
		}

		FATCellValueType destCellValue;
		ErrorCode err = mVolumeManager.getFATCell(destClusterIndex, destCellValue);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (!destCellValue.isFreeCluster()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The destination cluster for cluster-move operation is not free!");
			return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
		}

		FATCellValueType srcCellValue;
		err = mVolumeManager.getFATCell(sourceClusterIndex, srcCellValue);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (srcCellValue.isFreeCluster()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The source cluster for cluster-move operation is free!");
			return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
		}

		//SplitFAT debug only
#if (SPLITFAT_ENABLE_MOVECLUSTER_DEBUGGING == 1) 
#if !defined(MCPE_PUBLISH)
		bool isFileOfInterest = false;
		{
			// Because it is a slow process do that only for the first cluster of a file
			if (srcCellValue.isStartOfChain()) {
				FileManipulator fileManipulator;
				ErrorCode localErr = findFileFromCluster(sourceClusterIndex, fileManipulator);
				SFAT_ASSERT(localErr == ErrorCode::RESULT_OK, "Should be able to create fileManipulator for the file containing this cluster!");
				SFAT_ASSERT(fileManipulator.isValid(), "The fileManipulator should be valid!");
				if ((localErr == ErrorCode::RESULT_OK) && fileManipulator.isValid()) {
					const auto& record = fileManipulator.getFileDescriptorRecord();
					if (record.isSameName("manifest.json") || record.isSameName("contents.json")) 
					{
						std::string fullFilePath;
						localErr = createFullFilePathFromFileManipulator(fileManipulator, fullFilePath);
						SFAT_ASSERT(localErr == ErrorCode::RESULT_OK, "Should be able to extract file path for the fileManipulator!");
						SFAT_LOGI(LogArea::LA_VIRTUAL_DISK, "Moving cluster 0x%08x to 0x%08x. File: %s", sourceClusterIndex, destClusterIndex, fullFilePath.c_str());
						isFileOfInterest = true;
					}
				}
			}
		}
#endif //!defined(MCPE_PUBLISH)
#endif //(SPLITFAT_ENABLE_MOVECLUSTER_DEBUGGING == 1) 

		auto handle = mMemoryBufferPool->acquireBuffer();
		auto& clusterDataBuffer = handle->get();

		// Copy the cluster data content
		{
			err = mVolumeManager.readCluster(clusterDataBuffer, sourceClusterIndex);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			err = mVolumeManager.writeCluster(clusterDataBuffer, destClusterIndex);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Wasn't able to copy the source cluster data to the destication cluster!");
				return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
			}

			//SplitFAT debug only
#if (SPLITFAT_ENABLE_MOVECLUSTER_DEBUGGING == 1) 
#if !defined(MCPE_PUBLISH)
			if (isFileOfInterest && (clusterDataBuffer[0] == 0)) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The file already contains 0!");
			}
#endif //!defined(MCPE_PUBLISH)
#endif //(SPLITFAT_ENABLE_MOVECLUSTER_DEBUGGING == 1) 

		}

		//
		// Allocate the dest cell
		//
		destCellValue = srcCellValue; // The destination FAT-cell value should point to the same previous/next clusters. All the flags and extra data should be the same.
		err = mVolumeManager.setFATCell(destClusterIndex, destCellValue);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		//
		// Update the FileDescriptorRecord if necessary
		//
		if (srcCellValue.isStartOfChain() || srcCellValue.isEndOfChain()) {

			ClusterIndexType descriptorClusterIndex;
			uint32_t relativeRecordIndex;
			srcCellValue.decodeFileDescriptorLocation(descriptorClusterIndex, relativeRecordIndex);

			// Update the startCluster in FileDescriptorRecord
			{
				err = mVolumeManager.readCluster(clusterDataBuffer, descriptorClusterIndex);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}

				FileDescriptorRecord* record = _getFileDescriptorRecordInCluster(clusterDataBuffer.data(), relativeRecordIndex);
				FileDescriptorRecord oldRecord = *record; //Copy the original one.

				if (srcCellValue.isStartOfChain()) {
					if (record->mStartCluster != sourceClusterIndex) {
						SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Inconsistency is detected! The first cluster in the chain doesn't point the FileDescriptorRecord correctly!");
						return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
					}
					record->mOldClusterTrace = record->mStartCluster;
					record->mStartCluster = destClusterIndex;
				}
				
				if (srcCellValue.isEndOfChain()) {
					if (record->mLastCluster != sourceClusterIndex) {
						SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Inconsistency is detected! The last cluster in the chain doesn't point the FileDescriptorRecord correctly!");
						return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
					}
					record->mLastCluster = destClusterIndex;
				}

				if (isInTransaction()) {
					err = mVolumeManager.logFileDescriptorChange(descriptorClusterIndex, oldRecord, *record);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}
				}

				err = mVolumeManager.writeCluster(clusterDataBuffer, descriptorClusterIndex);
				if (err != ErrorCode::RESULT_OK) {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Wasn't able to update the FileDescriptorRecord referensing the moved cluster!");
					return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
				}
			}
		}

		//
		// Update the previous node if necessary
		//
		if (!srcCellValue.isStartOfChain()) {
			// The cluster is not first in the chain. Update the previous one to point forward to the destination cluster.
			FATCellValueType prevCellValue;
			err = mVolumeManager.getFATCell(srcCellValue.getPrev(), prevCellValue);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			if (prevCellValue.getNext() != sourceClusterIndex) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Inconsistency is detected! The previous cluster doesn't point the current one as a next!");
				return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
			}

			prevCellValue.setNext(destClusterIndex);
			err = mVolumeManager.setFATCell(srcCellValue.getPrev(), prevCellValue);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		//
		// Update the next node if necessary
		//
		if (!srcCellValue.isEndOfChain()) {
			// The cluster is not the last in the chain. Update the next one to point back to the destination cluster.
			FATCellValueType nextCellValue;
			err = mVolumeManager.getFATCell(srcCellValue.getNext(), nextCellValue);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			if (nextCellValue.getPrev() != sourceClusterIndex) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Inconsistency is detected! The next cluster doesn't point the current one as a previous!");
				return ErrorCode::ERROR_CAN_NOT_MOVE_CLUSTER;
			}

			nextCellValue.setPrev(destClusterIndex);
			err = mVolumeManager.setFATCell(srcCellValue.getNext(), nextCellValue);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		//
		// Free the source cell
		//
		err = mVolumeManager.setFATCell(sourceClusterIndex, FATCellValueType::freeCellValue());
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return ErrorCode::RESULT_OK;
	}

#if !defined(MCPE_PUBLISH)
	ErrorCode VirtualFileSystem::_testReadFile(const std::string& path) {
		char filePathBuffer[256]; // Copied tha path to this buffer, so the content can be modified during debugging.
		strncpy(filePathBuffer, path.c_str(), sizeof(filePathBuffer));
		uint8_t initializerValue = 0xfe;
		std::vector<uint8_t> readBuffer;
		readBuffer.resize(_getClusterSize(), initializerValue);

		FileManipulator fileFM;
		ErrorCode err = createGenericFileManipulatorForFilePath(filePathBuffer, fileFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		fileFM.mAccessMode = AccessMode::AM_READ;
		size_t fileSize = fileFM.getFileSize();
		size_t totalBytesRead = 0;
		size_t sizeToRead = fileSize;
		size_t sizeRead;
		bool testTheOriginalCluster = false;
		while (totalBytesRead < fileSize) {
			sizeToRead = readBuffer.size();
			if (sizeToRead + totalBytesRead > fileSize) {
				sizeToRead = fileSize - totalBytesRead;
			}
			memset(readBuffer.data(), initializerValue, readBuffer.size());
			err = read(fileFM, readBuffer.data(), sizeToRead, sizeRead);
			if (err != ErrorCode::RESULT_OK) {
				break;
			}
			SFAT_ASSERT(sizeToRead == sizeRead, "The sizeRead should match sizeToRead!");
			if ((totalBytesRead == 0) && (readBuffer[0] == 0)) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The manifest.json file starts with 0!");
				testTheOriginalCluster = true;
			}
			totalBytesRead += sizeRead;
		}

		if (testTheOriginalCluster) {
			ClusterIndexType clusterIndex = fileFM.getFileDescriptorRecord().mOldClusterTrace;
			if (isValidClusterIndex(clusterIndex)) {
				FATCellValueType cellValue = FATCellValueType::invalidCellValue();
				err = mVolumeManager.getFATCell(clusterIndex, cellValue);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
				err = mVolumeManager.readCluster(readBuffer, clusterIndex);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
				if ((totalBytesRead == 0) && (readBuffer[0] == 0)) {
					SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "The previous cluster was also starting with 0!");
				}
			}
		}

		return err;
	}

#endif //!defined(MCPE_PUBLISH)

	ErrorCode VirtualFileSystem::_findFileDescriptorFromCluster(ClusterIndexType clusterIndex, FileDescriptorRecord& descriptorFound, ClusterIndexType& descriptorClusterIndex, uint32_t& relativeRecordIndex) {
		descriptorClusterIndex = ClusterValues::INVALID_VALUE;
		relativeRecordIndex = static_cast<uint32_t>(-1);

		if (!isValidClusterIndex(clusterIndex)) {
			return ErrorCode::ERROR_INVALID_CLUSTER_INDEX;
		}

		FATCellValueType localCellValue = FATCellValueType::invalidCellValue();
		ErrorCode err = mVolumeManager.getFATCell(clusterIndex, localCellValue);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (localCellValue.isFreeCluster()) {
			//TODO: Make this return better the result
			return ErrorCode::ERROR_FILE_COULD_NOT_BE_FOUND;
		}

		if (localCellValue.isStartOfChain() || localCellValue.isEndOfChain()) {
			localCellValue.decodeFileDescriptorLocation(descriptorClusterIndex, relativeRecordIndex);
		}
		else {
			err = _iterateThroughClusterChain(clusterIndex,
				[&descriptorClusterIndex, &relativeRecordIndex](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)currentCluster; // Not used parameter

				if (cellValue.isEndOfChain()) {
					cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, relativeRecordIndex);
					doQuit = true;
				}

				return ErrorCode::RESULT_OK;
			}
			);

			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}

		const uint32_t recordsPerCluster = mVolumeManager.getClusterSize() / mVolumeManager.getFileDescriptorRecordStorageSize();
		if (!isValidClusterIndex(descriptorClusterIndex) || (relativeRecordIndex >= recordsPerCluster)) {
			// Every allocated cluster should belong to a cluster-chain,
			// where the cell corresponding to the first cluster in the chain,
			// should point to a correct FileDescriptorRecord that describes the same cluster chain.
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Lost cluster is detected! Can't find FileDescriptorRecord!");
			return ErrorCode::ERROR_INCONSISTENCY;
		}

		std::vector<uint8_t> clusterDataBuffer;
		if (clusterDataBuffer.size() != static_cast<size_t>(mVolumeManager.getClusterSize())) {
			clusterDataBuffer.resize(mVolumeManager.getClusterSize());
		}

		err = mVolumeManager.readCluster(clusterDataBuffer, descriptorClusterIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		FileDescriptorRecord* record = _getFileDescriptorRecordInCluster(clusterDataBuffer.data(), relativeRecordIndex);
		descriptorFound = *record;

		//TODO: Should we verify that the descriptor is pointing to the same cluster chain start/end?

		return err;
	}

	ErrorCode VirtualFileSystem::findFileFromCluster(ClusterIndexType clusterIndex, FileManipulator& fileManipulator) {
		FileDescriptorRecord descriptorFound;
		ClusterIndexType descriptorClusterIndex;
		uint32_t relativeRecordIndex;
		fileManipulator.mIsValid = false;
		const uint32_t recordsPerCluster = mVolumeManager.getClusterSize() / mVolumeManager.getFileDescriptorRecordStorageSize();

		ErrorCode err = _findFileDescriptorFromCluster(clusterIndex, descriptorFound, descriptorClusterIndex, relativeRecordIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (!isValidClusterIndex(descriptorClusterIndex) || (relativeRecordIndex >= recordsPerCluster)) {
			return ErrorCode::ERROR_INVALID_CLUSTER_INDEX;
		}

		ClusterIndexType directoryStartClusterIndex = ClusterValues::INVALID_VALUE;
		uint32_t count = 0;
		err = _iterateThroughClusterChain(descriptorClusterIndex,
			[&directoryStartClusterIndex, &count](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)cellValue; // Not used parameter

				if (cellValue.isStartOfChain()) {
					directoryStartClusterIndex = currentCluster;
					doQuit = true;
				}
				++count;

				return ErrorCode::RESULT_OK;
			},
			false //Iterate backwards
		);

		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		DescriptorLocation location;
		location.mDirectoryStartClusterIndex = directoryStartClusterIndex;
		location.mDescriptorClusterIndex = descriptorClusterIndex;
		location.mRecordIndex = recordsPerCluster * count + relativeRecordIndex;

		return _createFileManipulatorForExisting(location, descriptorFound, AccessMode::AM_READ, fileManipulator);
	}

	ErrorCode VirtualFileSystem::createFullFilePathFromCluster(ClusterIndexType clusterIndex, std::string& fullFilePath) {
		FileManipulator fileManipulator;
		ErrorCode err = findFileFromCluster(clusterIndex, fileManipulator);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return createFullFilePathFromFileManipulator(fileManipulator, fullFilePath);
	}

	ErrorCode VirtualFileSystem::createFullFilePathFromFileManipulator(const FileManipulator& fileManipulator, std::string& fullFilePath) {
		if (!fileManipulator.isValid()) {
			return ErrorCode::ERROR_INVALID_FILE_MANIPULATOR;
		}

		if (fileManipulator.isRootDirectory()) {
			fullFilePath = "/";
			return ErrorCode::RESULT_OK;
		}

		fullFilePath = "/";
		fullFilePath += fileManipulator.getFileDescriptorRecord().mEntityName;
		ClusterIndexType parentDirectoryStartCluster = fileManipulator.getDescriptorLocation().mDirectoryStartClusterIndex;
		while (parentDirectoryStartCluster != ClusterValues::ROOT_START_CLUSTER_INDEX) {

			FileManipulator parentDirFM;
			ErrorCode err = findFileFromCluster(parentDirectoryStartCluster, parentDirFM);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			if (!parentDirFM.isValid()) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "There should be a valid parent directory!");
				return ErrorCode::ERROR_FILES_INTEGRITY;
			}

			if (!parentDirFM.getFileDescriptorRecord().isDirectory()) {
				SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Found file instead parent directory!");
				return ErrorCode::ERROR_FILES_INTEGRITY;
			}

			std::string newPath("/");
			newPath += parentDirFM.getFileDescriptorRecord().mEntityName;
			newPath += fullFilePath;
			fullFilePath = std::move(newPath);

			parentDirectoryStartCluster = parentDirFM.getDescriptorLocation().mDirectoryStartClusterIndex;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode VirtualFileSystem::executeDebugCommand(const std::string& path, const std::string& command) {
#if !defined(MCPE_PUBLISH)
		static const char szWhiteSpaceChars[] = " \t";
		size_t commandNameEnd = command.find_first_of(szWhiteSpaceChars, 0);
		std::string commandName;
		std::string commandParameters;
		if (commandNameEnd != std::string::npos) {
			commandName = command.substr(0, commandNameEnd);
			size_t parametersStartPos = command.find_first_not_of(szWhiteSpaceChars, commandNameEnd);
			if (parametersStartPos != std::string::npos) {
				commandParameters = command.substr(parametersStartPos);
			}
		}
		else {
			commandName = command;
		}

		if (commandName == "dataConsistencyTest") {
			ErrorCode err = mRecoveryManager->testDataConsistency();
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			size_t fileProblemsCount = mRecoveryManager->getFileProblemsCount();
			if (fileProblemsCount) {
				return ErrorCode::ERROR_FILES_INTEGRITY;
			}
		}

		if (commandName == "integrityTest") {
			ErrorCode errTestingFATIntegrity = mRecoveryManager->testIntegrity();
			ErrorCode errTestingFilesIntegrity = mRecoveryManager->scanAllFiles();
			if (errTestingFATIntegrity != ErrorCode::RESULT_OK) {
				return errTestingFATIntegrity;
			}
			if (errTestingFilesIntegrity != ErrorCode::RESULT_OK) {
				return errTestingFilesIntegrity;
			}

			size_t fatProblemsCount = mRecoveryManager->getFATProblemsCount();
			size_t fileProblemsCount = mRecoveryManager->getFileProblemsCount();
			if (fatProblemsCount && fileProblemsCount) {
				return ErrorCode::ERROR_INTEGRITY;
			}
			if (fatProblemsCount) {
				return ErrorCode::ERROR_FAT_INTEGRITY;
			}
			if (fileProblemsCount) {
				return ErrorCode::ERROR_FILES_INTEGRITY;
			}
		}
		else if (commandName == "discardFATCachedChanges") {
			return mVolumeManager.discardFATCachedChanges();
		}
		else if (commandName == "discardDirectoryCachedChanges") {
			return mVolumeManager.discardDirectoryCachedChanges();
		}
		else if (commandName == "readTest") {
			return _testReadFile(path);
		}
#else
		(void)path; // Not used parameter
		(void)command; // Not used parameter
#endif

		return ErrorCode::RESULT_OK;
	}

	void VirtualFileSystem::_logReadingError(ErrorCode err, const FileManipulator& fileManipulator) {
		if (fileManipulator.getFileDescriptorRecord().isFile()) {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Error #%02u: Cluster 0x%08x. File: %s", err, fileManipulator.mPositionClusterIndex, fileManipulator.mFullPath.c_str());
		}
		else {
			SFAT_LOGE(LogArea::LA_VIRTUAL_DISK, "Error #%02u: Cluster 0x%08x. Directory: %s", err, fileManipulator.mPositionClusterIndex, fileManipulator.mFullPath.c_str());
		}
	}

} // namespace SFAT

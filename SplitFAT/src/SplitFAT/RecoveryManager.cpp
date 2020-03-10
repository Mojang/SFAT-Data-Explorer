/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/RecoveryManager.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/VirtualFileSystem.h"
#include "SplitFAT/FileManipulator.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/CRC.h"
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/utils/PathString.h"
#include <algorithm>

namespace SFAT {

	/**************************************************************************
	*	RecoveryManager implementation
	**************************************************************************/

	RecoveryManager::RecoveryManager(VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem)
		: mVolumeManager(volumeManager)
		, mVirtualFileSystem(virtualFileSystem) {
	}

	void RecoveryManager::_registerError(IntegrityStatus status, ClusterIndexType clusterIndex) {
		CellTestResult result;
		result.mClusterIndex = clusterIndex;
		result.mStatus = status;
		mCellsWithProblem.push_back(result);
	}

	ErrorCode RecoveryManager::testIntegrity() {
		FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
		uint32_t countFATBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		uint32_t clustersPerBlock = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock();
		ClusterIndexType totalClusters = static_cast<ClusterIndexType>(countFATBlocks * clustersPerBlock);
		BitSet testedClustersSet(totalClusters);
		testedClustersSet.setAll(false);
		FATCellValueType cellValue;
		ErrorCode err = ErrorCode::RESULT_OK;
		mClusterDataBuffer.resize(mVolumeManager.getClusterSize());
		mCellsWithProblem.clear();
		for (ClusterIndexType clusterIndex = 0; clusterIndex < totalClusters; ++clusterIndex) {
			if (testedClustersSet.getValue(clusterIndex)) {
				continue;
			}

			err = fatMgr.getValue(clusterIndex, cellValue);
			if (err != ErrorCode::RESULT_OK) {
				break;
			}

			if (!cellValue.isFreeCluster()) {
				if (cellValue.isStartOfChain()) {
					//
					// The cell is first one in the chain, so the FileDescriptorRecord should point to this first element.
					//
					CellTestResult result;
					err = verifyFileDescriptorClusterIndex(cellValue, clusterIndex, true, result);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}
					if (result.mStatus != IntegrityStatus::NO_ERROR) {
						mCellsWithProblem.push_back(result);
					}
				}
				else {
					//
					// The cell is not first, so another (the previous) cell should point to the current one as next.
					//
					ClusterIndexType prevClusterIndex = cellValue.getPrev();
					if (!isValidClusterIndex(prevClusterIndex)) {
						_registerError(IntegrityStatus::INVALID_CELL_INDEX_FOR_PREVIOUS_CELL, clusterIndex);
					}
					else {
						FATCellValueType prevCellValue;
						err = fatMgr.getValue(prevClusterIndex, prevCellValue);
						if (err != ErrorCode::RESULT_OK) {
							break;
						}

						if (prevCellValue.isEndOfChain() || (prevCellValue.getNext() != clusterIndex)) {
							_registerError(IntegrityStatus::INVALID_CELL_INDEX_FOR_NEXT_CELL, clusterIndex);
						}
					}
				}


				if (cellValue.isEndOfChain()) {
					//
					// The cell is last one in the chain, so the FileDescriptorRecord should point to this last element.
					//
					CellTestResult result;
					err = verifyFileDescriptorClusterIndex(cellValue, clusterIndex, false, result);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}
					if (result.mStatus != IntegrityStatus::NO_ERROR) {
						mCellsWithProblem.push_back(result);
					}
				}
				else {
					//
					// The cell is not last in the chain, so another (the next) cell should point to this one as previous.
					//
					ClusterIndexType nextClusterIndex = cellValue.getNext();
					if (!isValidClusterIndex(nextClusterIndex)) {
						_registerError(IntegrityStatus::INVALID_CELL_INDEX_FOR_NEXT_CELL, clusterIndex);
					}
					else {
						FATCellValueType nextCellValue;
						err = fatMgr.getValue(nextClusterIndex, nextCellValue);
						if (err != ErrorCode::RESULT_OK) {
							break;
						}

						if (nextCellValue.isStartOfChain() || (nextCellValue.getPrev() != clusterIndex)) {
							_registerError(IntegrityStatus::INVALID_CELL_INDEX_FOR_PREVIOUS_CELL, clusterIndex);
						}
					}
				}

				testedClustersSet.setValue(static_cast<size_t>(clusterIndex), true);
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode RecoveryManager::testDataConsistency() {
		FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
		uint32_t countFATBlocks = mVolumeManager.getCountAllocatedFATBlocks();
		uint32_t clustersPerBlock = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock();
		ClusterIndexType totalClusters = static_cast<ClusterIndexType>(countFATBlocks * clustersPerBlock);
		BitSet testedClustersSet(totalClusters);
		testedClustersSet.setAll(false);
		FATCellValueType cellValue;
		ErrorCode err = ErrorCode::RESULT_OK;
		mClusterDataBuffer.resize(mVolumeManager.getClusterSize());
		//mCellsWithProblem.clear();
		mClusterChainsWithProblem.clear();
		ClusterChainTestResult result;
		result.mStatus = IntegrityStatus::CRC_DOES_NOT_MATCH_FOR_CLUSTER;

		for (ClusterIndexType clusterIndex = 0; clusterIndex < totalClusters; ++clusterIndex) {
			if (testedClustersSet.getValue(clusterIndex)) {
				continue;
			}

			err = fatMgr.getValue(clusterIndex, cellValue);
			if (err != ErrorCode::RESULT_OK) {
				break;
			}

			if (!cellValue.isFreeCluster()) {
				err = mVolumeManager.readCluster(mClusterDataBuffer, clusterIndex);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}

#if (SPLIT_FAT__ENABLE_CRC_PER_CLUSTER == 1)
				const uint32_t calculatedCrc = CRC16::calculate(mClusterDataBuffer.data(), mVolumeManager.getClusterSize());
#else
				const uint32 calculatedCrc = 0;
#endif
				if (calculatedCrc != static_cast<uint32_t>(cellValue.decodeCRC())) {
					std::string fullFilePath;
					FileManipulator fileManipulator;
					err = mVirtualFileSystem.findFileFromCluster(clusterIndex, fileManipulator);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}

					err = mVirtualFileSystem.createFullFilePathFromFileManipulator(fileManipulator, fullFilePath);
					result.mLocation = fileManipulator.getDescriptorLocation();
					mClusterChainsWithProblem.push_back(result);

					FileSizeType fileSize = fileManipulator.getFileSize();
					SFAT_LOGW(LogArea::LA_VIRTUAL_DISK, "CRC doesn't match for cluster #%08X from file \"%s\", size:%u", clusterIndex, fullFilePath.c_str(), fileSize);
				}

				testedClustersSet.setValue(static_cast<size_t>(clusterIndex), true);
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode RecoveryManager::verifyFileDescriptorClusterIndex(const FATCellValueType& cellValue, ClusterIndexType sourceClusterIndex, bool startCluster, CellTestResult& result) {
		
		if (startCluster) {
			SFAT_ASSERT(cellValue.isStartOfChain(), "Should be used to verify the start of the chain!");
		}
		else {
			SFAT_ASSERT(cellValue.isEndOfChain(), "Should be used to verify the end of the chain!");
		}

		ClusterIndexType descriptorClusterIndex;
		uint32_t relativeRecordIndex;
		cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, relativeRecordIndex);

		if (mClusterDataBuffer.size() != static_cast<size_t>(mVolumeManager.getClusterSize())) {
			mClusterDataBuffer.resize(mVolumeManager.getClusterSize());
		}

		ErrorCode err = mVolumeManager.readCluster(mClusterDataBuffer, descriptorClusterIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		FileDescriptorRecord* record = mVirtualFileSystem._getFileDescriptorRecordInCluster(mClusterDataBuffer.data(), relativeRecordIndex);

		result.mClusterIndex = sourceClusterIndex;
		result.mStatus = IntegrityStatus::NO_ERROR;
		if (startCluster) {
			// This should be the chain first cluster
			if (record->mStartCluster == sourceClusterIndex) {
				if (record->isDeleted()) {
					result.mStatus = IntegrityStatus::ALLOCATED_CELL_BELONGS_TO_DELETED_FILE;
				}
			}
			else {
				result.mStatus = IntegrityStatus::CLUSTER_CHAIN_START_MISMATCH;
			}
		}
		else {
			// This should be the chain last cluster
			if (record->mLastCluster == sourceClusterIndex) {
				if (record->isDeleted()) {
					result.mStatus = IntegrityStatus::ALLOCATED_CELL_BELONGS_TO_DELETED_FILE;
				}
			}
			else {
				result.mStatus = IntegrityStatus::CLUSTER_CHAIN_END_MISMATCH;
			}
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode RecoveryManager::scanAllFiles() {
		mClusterChainsWithProblem.clear();
		uint32_t totalFilesScanned = 0;
		ErrorCode err = mVirtualFileSystem._iterateThroughDirectoryRecursively(PathString("/"), DI_ALL | DI_RECURSIVE, [&totalFilesScanned, this](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			(void)doQuit; // Not used parameter

			if (record.isDeleted()) {
				return ErrorCode::RESULT_OK;
			}

			ClusterChainTestResult result;
			result.mStatus = IntegrityStatus::NO_ERROR;
			ErrorCode err = testSingleFileIntegrity(record, fullPath, result);

			if (result.mStatus != IntegrityStatus::NO_ERROR) {
				result.mLocation = location;
				mClusterChainsWithProblem.push_back(result);
			}

			++totalFilesScanned;
			
			return err;
		});

		return err;
	}

	ErrorCode RecoveryManager::testSingleFileIntegrity(const FileDescriptorRecord& record, const std::string& fullPath, ClusterChainTestResult& result) {
		(void)fullPath; // Not used parameter

		if (record.isDeleted()) {
			// We are not interested in this case
			return ErrorCode::RESULT_OK;
		}

		ClusterIndexType startClusterIndex = record.mStartCluster;
		ClusterIndexType lastClusterIndex = record.mLastCluster;
		if (isValidClusterIndex(startClusterIndex)) {

			//The last cluster index should be a valid index in this case
			if (!isValidClusterIndex(lastClusterIndex)) {
				result.mStatus = IntegrityStatus::INVALID_FILE_END_CLUSTER_INDEX;
				result.mClusterIndex = lastClusterIndex;
				return ErrorCode::RESULT_OK;
			}

			//
			// We have a cluster chain
			//
			if (record.isFile() && (record.mFileSize == 0)) {
				result.mStatus = IntegrityStatus::CLUSTER_CHAIN_ATTACHED_TO_AN_EMPTY_FILE;
				result.mClusterIndex = startClusterIndex; // This is expected to be 0, but it is not.
				return ErrorCode::RESULT_OK;
			}

			// Test the cluster chain
			FATDataManager& fatMgr = mVolumeManager.getFATDataManager();
			ErrorCode err;
			
			// Test whether the startClusterIndex points to an allocated cluster/cell
			{
				FATCellValueType startCellValue;
				err = fatMgr.getValue(startClusterIndex, startCellValue);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}

				if (startCellValue.isFreeCluster()) {
					result.mStatus = IntegrityStatus::REFERENCE_TO_NOT_ALLOCATED_CLUSTER_FOR_CHAIN_START;
					result.mClusterIndex = startClusterIndex;
					return ErrorCode::RESULT_OK;
				}
			}

			// Test whether the lastClusterIndex points to an allocated cluster/cell
			{
				FATCellValueType endCellValue;
				err = fatMgr.getValue(lastClusterIndex, endCellValue);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}

				if (endCellValue.isFreeCluster()) {
					result.mStatus = IntegrityStatus::REFERENCE_TO_NOT_ALLOCATED_CLUSTER_FOR_CHAIN_END;
					result.mClusterIndex = lastClusterIndex;
					return ErrorCode::RESULT_OK;
				}
			}

			uint32_t counter = 0;
			err = mVirtualFileSystem._iterateThroughClusterChain(startClusterIndex,
				[&counter, &result, &fatMgr](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
				(void)doQuit; // Not used parameter

				if (counter == 0) {
					// The first cluster should be the start cluster for the cluster chain!
					if (!cellValue.isStartOfChain()) {
						result.mStatus = IntegrityStatus::FIRST_CHAIN_CLUSTER_NOT_MARKED_AS_A_CHAIN_START;
						result.mClusterIndex = currentCluster;
						return ErrorCode::RESULT_OK;
					}
				}
				else {
					// Every cluster after the first one, should have previous cluster in the chain!
					if (cellValue.isStartOfChain()) {
						result.mStatus = IntegrityStatus::MIDDLE_CHAIN_CLUSTER_MARKED_AS_A_CHAIN_START;
						result.mClusterIndex = currentCluster;
						return ErrorCode::RESULT_OK;
					}

					ClusterIndexType prevClusterIndex = cellValue.getPrev();
					if (!isValidClusterIndex(prevClusterIndex)) {
						result.mStatus = IntegrityStatus::PREVIOUS_CLUSTER_OF_A_MIDDLE_CHAIN_CLUSTER_IS_INVALID;
						result.mClusterIndex = currentCluster;
						return ErrorCode::RESULT_OK;
					}

					FATCellValueType prevCellValue;
					ErrorCode err = fatMgr.getValue(prevClusterIndex, prevCellValue);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}

					if (prevCellValue.isEndOfChain() || (prevCellValue.getNext() != currentCluster)) {
						result.mStatus = IntegrityStatus::PREVIOUS_CLUSTER_OF_A_MIDDLE_CHAIN_CLUSTER_IS_WRONG;
						result.mClusterIndex = currentCluster;
						return ErrorCode::RESULT_OK;
					}
				}
				++counter;
				return ErrorCode::RESULT_OK;
			}
			);

			if (err != ErrorCode::RESULT_OK) {
				return err;
			}

			if (record.isFile()) {
				// Check whether the file-size corresponds to the number of clusters
				const uint32_t expectedClusterCount = static_cast<uint32_t>((record.mFileSize + mVolumeManager.getClusterSize() - 1) / mVolumeManager.getClusterSize());
				if (expectedClusterCount != counter) {
					result.mStatus = IntegrityStatus::FILE_SIZE_DOES_NOT_MATCH_THE_COUNT_OF_CLUSTER;
					return ErrorCode::RESULT_OK;
				}
			}
		}
		else {
			//The last cluster index should be invalid as well
			if (isValidClusterIndex(lastClusterIndex)) {
				result.mStatus = IntegrityStatus::FILE_END_CLUSTER_INDEX_MISMATCH;
				result.mClusterIndex = lastClusterIndex;
				return ErrorCode::RESULT_OK;
			}
			
			if (record.mFileSize != 0) {
				result.mStatus = IntegrityStatus::MISSING_CLUSTER_CHAIN_FOR_NOT_EMPTY_FILE;
				return ErrorCode::RESULT_OK;
			}
		}

		result.mStatus = IntegrityStatus::NO_ERROR;
		return ErrorCode::RESULT_OK;
	}

} // namespace SFAT


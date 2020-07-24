/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include <string>

namespace SFAT {

	class VolumeManager;
	class VirtualFileSystem;

	enum class IntegrityStatus {
		NO_ERROR = 0,
		INTERNAL_ERROR,
		CLUSTER_CHAIN_START_MISMATCH,
		CLUSTER_CHAIN_END_MISMATCH,
		INVALID_CELL_INDEX_FOR_PREVIOUS_CELL,
		INVALID_CELL_INDEX_FOR_NEXT_CELL,
		ALLOCATED_CELL_BELONGS_TO_DELETED_FILE,
		MISSING_CLUSTER_CHAIN_FOR_NOT_EMPTY_FILE,
		CLUSTER_CHAIN_ATTACHED_TO_AN_EMPTY_FILE,
		FIRST_CHAIN_CLUSTER_NOT_MARKED_AS_A_CHAIN_START,
		MIDDLE_CHAIN_CLUSTER_MARKED_AS_A_CHAIN_START,
		PREVIOUS_CLUSTER_OF_A_MIDDLE_CHAIN_CLUSTER_IS_INVALID,
		PREVIOUS_CLUSTER_OF_A_MIDDLE_CHAIN_CLUSTER_IS_WRONG,
		FILE_SIZE_DOES_NOT_MATCH_THE_COUNT_OF_CLUSTER,
		INVALID_FILE_START_CLUSTER_INDEX,
		INVALID_FILE_END_CLUSTER_INDEX,
		FILE_END_CLUSTER_INDEX_MISMATCH,
		REFERENCE_TO_NOT_ALLOCATED_CLUSTER_FOR_CHAIN_START,
		REFERENCE_TO_NOT_ALLOCATED_CLUSTER_FOR_CHAIN_END,
		CRC_DOES_NOT_MATCH_FOR_CLUSTER,
	};

	struct CellTestResult {
		IntegrityStatus mStatus;
		ClusterIndexType mClusterIndex; // Cluster/cell index
		uint32_t mRecordIndex;
	};

	struct ClusterChainTestResult {
		IntegrityStatus mStatus;
		ClusterIndexType mClusterIndex; // Cluster/cell index
		DescriptorLocation mLocation;
	};

	class RecoveryManager {
	public:
		RecoveryManager(VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem);
		virtual ~RecoveryManager() = default;
		void reportError(ErrorCode error, const char* szMessage, ...) {
			(void)error; // Not used parameter
			(void)szMessage; // Not used parameter
			//TODO: Implement!
		}

		ErrorCode testIntegrity();
		ErrorCode scanAllFiles();
		ErrorCode testDataConsistency();

		size_t getFATProblemsCount() const {
			return mCellsWithProblem.size();
		};

		size_t getFileProblemsCount() const {
			return mClusterChainsWithProblem.size();
		}

	private:
		void _registerError(IntegrityStatus status, ClusterIndexType clusterIndex);
		ErrorCode verifyFileDescriptorClusterIndex(const FATCellValueType& cellValue, ClusterIndexType sourceClusterIndex, bool startCluster, CellTestResult& result);
		ErrorCode testSingleFileIntegrity(const FileDescriptorRecord& record, const std::string& fullPath, ClusterChainTestResult& result);

	private:
		VolumeManager& mVolumeManager;
		VirtualFileSystem& mVirtualFileSystem;
		std::vector<uint8_t> mClusterDataBuffer;
		std::vector<CellTestResult> mCellsWithProblem;
		std::vector<ClusterChainTestResult> mClusterChainsWithProblem;
	};

} // namespace SFAT

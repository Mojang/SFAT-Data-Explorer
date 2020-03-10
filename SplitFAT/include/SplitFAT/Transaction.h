/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "FileDescriptorRecord.h"
#include "AbstractFileSystem.h"
#include "LowLevelAccess.h"
#include "SplitFAT/utils/Mutex.h"

///Unit-test classes forward declaration
#if !defined(MCPE_PUBLISH)
class TransactionUnitTest_RestoreFromTransaction_Test;
#endif //!defined(MCPE_PUBLISH)

namespace SFAT {

	class VolumeManager;

	enum class TransactionEventType : uint32_t {
		FAT_BLOCK_CHANGED,
		DIRECTORY_CLUSTER_CHANGED,
		FILE_CLUSTER_CHANGED,
		BLOCK_VIRTUALIZATION_TABLE_CHANGED,
	};

	struct TransactionEvent {
		TransactionEventType mEventType;
		union {
			ClusterIndexType mClusterIndex;
			uint32_t mBlockIndex;
			uint32_t mActiveDescriptorIndex;
		};
		uint32_t mCRC; // CRC before the any of the changes
	};

	class TransactionEventsLog {
#if !defined(MCPE_PUBLISH)
		friend class TransactionUnitTest_RestoreFromTransaction_Test;
#endif //!defined(MCPE_PUBLISH)

	public:
		TransactionEventsLog(VolumeManager& volumeManager);
		ErrorCode logFATCellChange(ClusterIndexType cellIndex, const FATBlockTableType& buffer);
		ErrorCode logFileDescriptorChange(ClusterIndexType descriptorClusterIndex, const FileDescriptorRecord& oldRecord, const FileDescriptorRecord& newRecord);
		ErrorCode logBlockVirtualizationChange();
		ErrorCode logFileClusterChange(ClusterIndexType clusterIndex/*,  const void* oldClusterData, const void* newLusterData*/);

		ErrorCode start();
		ErrorCode commit();
		ErrorCode tryRestoreFromTransactionFile();
		bool isInTransaction() const;

	private:
		ErrorCode _writeIntoTransactionFile(const TransactionEvent& transactionEvent, const void* pBuffer);
		ErrorCode _restoreFromTransactionFile();
		ErrorCode _finalizeTransacion();

	private:
		VolumeManager& mVolumeManager;
		std::unordered_map<uint32_t, TransactionEvent> mFATBlockChanges;
		std::unordered_map<ClusterIndexType, TransactionEvent> mFileClusterChanges;
		std::unordered_map<ClusterIndexType, TransactionEvent> mDirectoryClusterChanges;
		bool mIsInTransaction;
		std::vector<uint8_t> mClusterDataBuffer;
	};

} // namespace SFAT

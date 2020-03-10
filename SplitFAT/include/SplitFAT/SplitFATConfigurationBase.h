/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <stdint.h>
#include <memory>
#include "Common.h"

namespace SFAT {

	class FileStorageBase;
	class FileHandle;
	class VolumeManager;
	class VirtualFileSystem;
	class DataPlacementStrategyBase;

	/**
	*	Access to the lower level file storage for both FAT-data and cluster-data.
	*/
	class SplitFATConfigurationBase {
	public:
		SplitFATConfigurationBase() = default;
		virtual ~SplitFATConfigurationBase() = default;

		virtual ErrorCode shutdown() = 0;

		virtual ErrorCode create() = 0;
		virtual ErrorCode open() = 0;
		virtual ErrorCode close() = 0;
		virtual FileHandle getClusterDataFile(int accessMode) const = 0;
		virtual FileHandle getFATDataFile(int accessMode) const = 0;
		virtual ErrorCode remove() = 0;
		virtual ErrorCode flushFATDataFile() = 0;
		virtual ErrorCode flushClusterDataFile() = 0;
		virtual ErrorCode allocateDataBlock(VolumeManager& volumeManager, uint32_t blockIndex) = 0;
		// Called before the actual transaction is finalized, to allow block optimization processes to be performed.
		virtual ErrorCode defragmentationOnTransactionEnd() {
			return ErrorCode::RESULT_OK;
		}

		virtual bool clusterDataFileExists() const = 0;
		virtual bool fatDataFileExists() const = 0;
		inline bool isReady() const { return mIsReady; }

		virtual ErrorCode createDataPlacementStrategy(std::shared_ptr<DataPlacementStrategyBase>& dataPlacementStrategy,
			VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem) = 0;

		////////////////////////////////////////////////////////////////////////////////////////////////////
		// Transaction
		////////////////////////////////////////////////////////////////////////////////////////////////////
		virtual bool isTransactionSupported() const { return false; }
		virtual ErrorCode createTempTransactionFile() {
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		virtual ErrorCode tryOpenFinalTransactionFile(FileHandle& fileHandle) {
			(void)fileHandle;
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		virtual ErrorCode cleanupTransactionFinalFile() {
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		virtual ErrorCode cleanupTransactionTempFile() {
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		virtual ErrorCode finalizeTransactionFile() {
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		virtual ErrorCode closeReadOnlyTransactionFile() {
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		virtual void getTempTransactionFile(FileHandle& fileHandle) const {
			(void)fileHandle;
		}

	protected:

		volatile bool mIsReady = false; //TODO: Consider for thread-safe implementation. It is not currently!
	};

} // namespace SFAT
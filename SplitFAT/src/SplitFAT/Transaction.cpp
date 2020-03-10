/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/Transaction.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/CRC.h"

namespace SFAT {

	TransactionEventsLog::TransactionEventsLog(VolumeManager& volumeManager)
		: mVolumeManager(volumeManager)
		, mIsInTransaction(false) {
	}

	ErrorCode TransactionEventsLog::logFATCellChange(ClusterIndexType cellIndex, const FATBlockTableType& buffer) {
		uint32_t blockIndex = mVolumeManager.getBlockIndex(cellIndex);
		ErrorCode err = ErrorCode::RESULT_OK;
		TransactionEvent transactionEvent = { TransactionEventType::FAT_BLOCK_CHANGED, { blockIndex }, 0 /*Ignore the CRC for now*/ };

		// Try inserting the element
		auto result = mFATBlockChanges.insert(std::pair<uint32_t, TransactionEvent>(blockIndex, transactionEvent));
		if (result.second) {
			// The element was just inserted.
			//TODO: Calculate and update the CRC.
			err = _writeIntoTransactionFile(transactionEvent, buffer.data());
		}

		return err;
	}

	ErrorCode TransactionEventsLog::logFileDescriptorChange(ClusterIndexType descriptorClusterIndex, const FileDescriptorRecord& oldRecord, const FileDescriptorRecord& newRecord) {
		(void)oldRecord; // Not used parameter
		(void)newRecord; // Not used parameter

		ErrorCode err = ErrorCode::RESULT_OK;
		TransactionEvent transactionEvent = { TransactionEventType::DIRECTORY_CLUSTER_CHANGED, { descriptorClusterIndex }, 0 /*Ignore the CRC for now*/ };

		// Try inserting the element
		auto result = mDirectoryClusterChanges.insert(std::pair<ClusterIndexType, TransactionEvent>(descriptorClusterIndex, transactionEvent));
		if (result.second) {
			// The element was just inserted.
			// Copy the corresponding cluster before it is changed.
			err = mVolumeManager.readCluster(mClusterDataBuffer, descriptorClusterIndex);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			err = _writeIntoTransactionFile(transactionEvent, mClusterDataBuffer.data());
		}

		return err;
	}

	ErrorCode TransactionEventsLog::logBlockVirtualizationChange() {
		const uint32_t crc = mVolumeManager.getBlockVirtualization().getActiveDescriptorCRC();
		const uint32_t activeDescriptorIndex = mVolumeManager.getBlockVirtualization().getActiveDescriptorIndex();
		TransactionEvent transactionEvent = { TransactionEventType::BLOCK_VIRTUALIZATION_TABLE_CHANGED, { activeDescriptorIndex }, crc };

		const VolumeDescriptorExtraParameters& extraParameters = mVolumeManager.getVolumeDescriptorExtraParameters();

		return _writeIntoTransactionFile(transactionEvent, &extraParameters);
	}

	ErrorCode TransactionEventsLog::logFileClusterChange(ClusterIndexType clusterIndex/*, const void* oldClusterData, const void* newLusterData*/) {
		ErrorCode err = ErrorCode::RESULT_OK;
		TransactionEvent transactionEvent = { TransactionEventType::FILE_CLUSTER_CHANGED, { clusterIndex }, 0 /*Ignore the CRC for now*/ };

		// Try inserting the element
		auto result = mFileClusterChanges.insert(std::pair<uint32_t, TransactionEvent>(clusterIndex, transactionEvent));
		if (result.second) {
			//The element was just inserted.
			//TODO: Calculate and update the CRC.
		}

		return err;
	}

	ErrorCode TransactionEventsLog::start() {
		mFATBlockChanges.clear();
		mFileClusterChanges.clear();
		mDirectoryClusterChanges.clear();

		ErrorCode err = mVolumeManager.flush();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		err = mVolumeManager.getLowLevelFileAccess().createTempTransactionFile();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		mIsInTransaction = true;

		return ErrorCode::RESULT_OK;
	}

	ErrorCode TransactionEventsLog::_finalizeTransacion() {
		SFAT_ASSERT(mIsInTransaction, "Should be called only in transaction!");

		ErrorCode err = logBlockVirtualizationChange();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Close the transaction file.
		err = mVolumeManager.getLowLevelFileAccess().finalizeTransactionFile();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_TRANSACTION, "Wasn't able to finalize the transaction file!");
			// Will not commit the changes!
			return err;
		}

		//
		// At this point we have the transaction file finalized, so we can revert in case something fails.

		// Write down all cached changes in FAT and the directory-data clusters.
		err = mVolumeManager.immediateFlush();
		mIsInTransaction = false;

		return err;
	}

	ErrorCode TransactionEventsLog::commit() {
		if (!mIsInTransaction) {
			return ErrorCode::ERROR_NO_TRANSACTION_HAS_BEEN_STARTED;
		}

		ErrorCode err = _finalizeTransacion();

		// If there was an error during the transaction closing, restore from the transaction file.
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_TRANSACTION, "Wasn't able to finalize the transaction! Reverting the transaction!");
			err = _restoreFromTransactionFile();
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_TRANSACTION, "Wasn't able to restore after failed transaction!");
				return err;
			}
			SFAT_LOGI(LogArea::LA_TRANSACTION, "The transaction was reverted correctly!");
		}

		// Either the transaction was finalized correctly, or it was reverted correctly.
		// Delete the transaction file as a final step.
		err = mVolumeManager.getLowLevelFileAccess().cleanupTransactionFinalFile();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_TRANSACTION, "Wasn't able to clean up the transaction file!");
		}

		return err;
	}

	bool TransactionEventsLog::isInTransaction() const {
		return mIsInTransaction;
	}

	ErrorCode TransactionEventsLog::_writeIntoTransactionFile(const TransactionEvent& transactionEvent, const void* pBuffer) {
		FileHandle fileHandle;
		mVolumeManager.getLowLevelFileAccess().getTempTransactionFile(fileHandle);
		if (!fileHandle.isOpen()) {
			return ErrorCode::ERROR_NO_TRANSACTION_HAS_BEEN_STARTED;
		}

		size_t countBytesToWrite = 0;
		switch (transactionEvent.mEventType) {
			case TransactionEventType::FAT_BLOCK_CHANGED: {
				countBytesToWrite = static_cast<size_t>(mVolumeManager.getVolumeDescriptor().getByteSizeOfFATBlock());
			} break;
			case TransactionEventType::DIRECTORY_CLUSTER_CHANGED: {
				countBytesToWrite = static_cast<size_t>(mVolumeManager.getClusterSize());
			} break;
			case TransactionEventType::BLOCK_VIRTUALIZATION_TABLE_CHANGED: {
				countBytesToWrite = sizeof(VolumeDescriptorExtraParameters);
			} break;
			default: {
				return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
			}
		}

		FilePositionType position;
		ErrorCode err = fileHandle.getPosition(position);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Write the event descriptor data
		size_t bytesWritten = 0;
		err = fileHandle.writeAtPosition(&transactionEvent, sizeof(TransactionEvent), position, bytesWritten);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		SFAT_ASSERT(sizeof(TransactionEvent) == bytesWritten, "The size of the written should match!");
		position += sizeof(TransactionEvent);

		// Write the data from the buffer
		err = fileHandle.writeAtPosition(pBuffer, countBytesToWrite, position, bytesWritten);
		SFAT_ASSERT(countBytesToWrite == bytesWritten, "The size of the written should match!");

		return err;
	}

	ErrorCode TransactionEventsLog::_restoreFromTransactionFile() {
		FileHandle fileHandle;
		mVolumeManager.getLowLevelFileAccess().tryOpenFinalTransactionFile(fileHandle);
		//mVolumeManager.getLowLevelFileAccess().getTempTransactionFile(fileHandle);
		if (!fileHandle.isOpen()) {
			return ErrorCode::ERROR_NO_TRANSACTION_FILE_FOUND;
		}

		TransactionEvent transactionEvent;

		FilePositionType position = 0;
		ErrorCode err = ErrorCode::RESULT_OK;

		size_t bytesRead;
		do {
			// Read the transaction-event descriptor data
			bytesRead = 0;
			err = fileHandle.readAtPosition(&transactionEvent, sizeof(TransactionEvent), position, bytesRead);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
			if (bytesRead != sizeof(TransactionEvent)) {
				break;
			}
			position += sizeof(TransactionEvent);

			// Read the specific data and restore
			size_t countBytesToRead = 0;
			switch (transactionEvent.mEventType) {
				case TransactionEventType::FAT_BLOCK_CHANGED: {
					countBytesToRead = static_cast<size_t>(mVolumeManager.getVolumeDescriptor().getByteSizeOfFATBlock());
					uint32_t blockIndex = transactionEvent.mBlockIndex;
					err = mVolumeManager.executeOnFATBlock(blockIndex, [&fileHandle, &position, &countBytesToRead](uint32_t blockIndex, FATBlockTableType& table, bool& wasChanged)->ErrorCode{
						(void)blockIndex; // Not used parameter

						wasChanged = true;
						size_t bytesRead = 0;
						ErrorCode err = fileHandle.readAtPosition(table.data(), countBytesToRead, position, bytesRead);
						if (err != ErrorCode::RESULT_OK) {
							return err;
						}
						if (countBytesToRead != bytesRead) {
							SFAT_LOGE(LogArea::LA_TRANSACTION, "The size of the FAT block read from the transaction file is incorrect!");
							return ErrorCode::ERROR_VOLUME_RESTORE_FROM_TRANSACTION_ERROR;
						}
						return ErrorCode::RESULT_OK;
					});

					if (err != ErrorCode::RESULT_OK) {
						return err;
					}

					position += countBytesToRead;
				} break;
				case TransactionEventType::DIRECTORY_CLUSTER_CHANGED: {
					countBytesToRead = static_cast<size_t>(mVolumeManager.getClusterSize());
					// Read the cluster data from the transaction file
					if (mClusterDataBuffer.size() < countBytesToRead) {
						mClusterDataBuffer.resize(countBytesToRead);
					}
					err = fileHandle.readAtPosition(mClusterDataBuffer.data(), countBytesToRead, position, bytesRead);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}
					if (countBytesToRead != bytesRead) {
						SFAT_LOGE(LogArea::LA_TRANSACTION, "The size of the cluster data read from the transaction file is incorrect!");
						return ErrorCode::ERROR_VOLUME_RESTORE_FROM_TRANSACTION_ERROR;
					}

					err = mVolumeManager.writeCluster(mClusterDataBuffer, transactionEvent.mClusterIndex);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}

					position += countBytesToRead;
				} break;
				case TransactionEventType::BLOCK_VIRTUALIZATION_TABLE_CHANGED: {
					countBytesToRead = sizeof(VolumeDescriptorExtraParameters);
					VolumeDescriptorExtraParameters extraParameters;
					err = fileHandle.readAtPosition(&extraParameters, countBytesToRead, position, bytesRead);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}
					if (countBytesToRead != bytesRead) {
						SFAT_LOGE(LogArea::LA_TRANSACTION, "The size of the data read from the transaction file is incorrect!");
						return ErrorCode::ERROR_VOLUME_RESTORE_FROM_TRANSACTION_ERROR;
					}

					err = mVolumeManager.getBlockVirtualization().setBlockVirtualizationData(extraParameters);
					if (err != ErrorCode::RESULT_OK) {
						return err;
					}

					position += countBytesToRead;
				} break;
				case TransactionEventType::FILE_CLUSTER_CHANGED: {
					SFAT_LOGW(LogArea::LA_TRANSACTION, "File cluster changes are not processed.");
				}
			}
		} while (bytesRead > 0);

		err = fileHandle.close();
		SFAT_ASSERT(err == ErrorCode::RESULT_OK, "It will be impossible to delete the transaction file if it is not closed!");

		return mVolumeManager.flush();
	}

	ErrorCode TransactionEventsLog::tryRestoreFromTransactionFile() {
		ErrorCode err = _restoreFromTransactionFile();
		if (err == ErrorCode::ERROR_NO_TRANSACTION_FILE_FOUND) {
			// There is no transaction file, so nothing to be restored.
			return ErrorCode::RESULT_OK;
		}
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_TRANSACTION, "Failed to restore from transaction file!");
			return err;
		}

		err = mVolumeManager.getLowLevelFileAccess().cleanupTransactionFinalFile();
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_TRANSACTION, "Wasn't able to clean up the transaction file!");
		}

		SFAT_LOGI(LogArea::LA_TRANSACTION, "Restored from transaction file!");

		return err;
	}
} // namespace SFAT

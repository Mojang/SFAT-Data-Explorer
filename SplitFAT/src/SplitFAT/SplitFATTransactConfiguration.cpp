/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/SplitFATTransactConfiguration.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/PathString.h"

namespace SFAT {
	
	//
	// Transaction
	//

	bool SplitFATTransactConfiguration::isTransactionSupported() const {
		return true;
	}

	ErrorCode SplitFATTransactConfiguration::createTempTransactionFile() {
		if (mTempTransactionFile.isOpen()) {
			return ErrorCode::ERROR_TRANSACTION_IS_ALREADY_STARTED;
		}
		ErrorCode err = mTransactionFileStorage->openFile(mTempTransactionFile, _getTransactionTempFilePath(), "wb");
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't create the transaction file.");
		}

		return err;
	}

	ErrorCode SplitFATTransactConfiguration::tryOpenFinalTransactionFile(FileHandle& fileHandle) {
		ErrorCode err = ErrorCode::RESULT_OK;
		if (mTransactionFileStorage->fileExists(_getTransactionFinalFilePath())) {
			err = mTransactionFileStorage->openFile(mTransactionFile, _getTransactionFinalFilePath(), "rb");
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't open the transaction file.");
			}
		}

		fileHandle = mTransactionFile;

		return err;
	}

	ErrorCode SplitFATTransactConfiguration::cleanupTransactionFinalFile() {
		if (mTransactionFile.isOpen()) {
			// Try closing it!
			ErrorCode err = mTransactionFile.close();
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't close the transaction file in order to delete it.");
			}
		}
		return mTransactionFileStorage->deleteFile(_getTransactionFinalFilePath());
	}

	ErrorCode SplitFATTransactConfiguration::cleanupTransactionTempFile() {
		return mTransactionFileStorage->deleteFile(_getTransactionTempFilePath());
	}

	ErrorCode SplitFATTransactConfiguration::finalizeTransactionFile() {
		if (!mTempTransactionFile.isOpen() ||
			!mTempTransactionFile.checkAccessMode(AccessMode::AM_WRITE)) {
			return ErrorCode::ERROR_VOLUME_TRANSACTION_ERROR;
		}
		// Flush the temp transaction file
		ErrorCode err = mTempTransactionFile.flush();
		SFAT_ASSERT(err == ErrorCode::RESULT_OK, "The flush operation failed for the transaction file!");
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		// Close the temp transaction file
		err = mTempTransactionFile.close();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Rename it to the name of the final transaction file
		return mTransactionFileStorage->renameFile(_getTransactionTempFilePath(), _getTransactionFinalFilePath());
	}

	ErrorCode SplitFATTransactConfiguration::closeReadOnlyTransactionFile() {
		if (mTransactionFile.isOpen()) {
			SFAT_ASSERT(mTransactionFile.checkAccessMode(AccessMode::AM_READ), "Read mode is required!");
			SFAT_ASSERT(!mTransactionFile.checkAccessMode(AccessMode::AM_WRITE), "Write mode should not be used!");
			return mTransactionFile.close();
		}
		return ErrorCode::RESULT_OK;
	}

	void SplitFATTransactConfiguration::getTempTransactionFile(FileHandle& fileHandle) const {
		SFAT_ASSERT(!mTempTransactionFile.checkAccessMode(AccessMode::AM_READ) &&
			mTempTransactionFile.checkAccessMode(AccessMode::AM_WRITE), "This function should only be used with the temp transaction file!");
		fileHandle = mTempTransactionFile;
	}

	void SplitFATTransactConfiguration::_transactionSetup(std::shared_ptr<FileStorageBase> transactionFileStorage) {
		mTransactionFileStorage = std::move(transactionFileStorage);
	}

	void SplitFATTransactConfiguration::_transactionShutdown() {
		mTransactionFile.reset();
		mTempTransactionFile.reset();
	}

} // namespace SFAT

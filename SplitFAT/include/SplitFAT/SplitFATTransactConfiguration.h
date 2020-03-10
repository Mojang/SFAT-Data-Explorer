/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFATConfigurationBase.h"
#include "AbstractFileSystem.h"
#include <memory>

namespace SFAT {

	/**
	*	Access to the lower level file storage for both FAT-data and cluster-data.
	*/
	class SplitFATTransactConfiguration : public SplitFATConfigurationBase {
	public:

		// Transaction
		virtual bool isTransactionSupported() const override;
		virtual ErrorCode createTempTransactionFile() override;
		virtual ErrorCode tryOpenFinalTransactionFile(FileHandle& fileHandle) override;
		virtual ErrorCode cleanupTransactionFinalFile() override;
		virtual ErrorCode cleanupTransactionTempFile() override;
		virtual ErrorCode finalizeTransactionFile() override;
		virtual ErrorCode closeReadOnlyTransactionFile() override;
		virtual void getTempTransactionFile(FileHandle& fileHandle) const override;

	protected:
		virtual const char* _getTransactionFinalFilePath() const = 0;
		virtual const char* _getTransactionTempFilePath() const = 0;

		void _transactionSetup(std::shared_ptr<FileStorageBase> transactionFileStorage);
		void _transactionShutdown();

	private:

		// Transaction
		FileHandle	mTempTransactionFile;
		FileHandle	mTransactionFile;
		std::shared_ptr<FileStorageBase>	mTransactionFileStorage;
	};

} // namespace SFAT
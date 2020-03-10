/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include <string>
#include <memory>

#include "SplitFAT/SplitFATTransactConfiguration.h"

namespace SFAT {

	/**
	*	Access to the lower level file storage for both FAT-data and cluster-data.
	*/
	class WindowsSplitFATConfiguration : public SplitFATTransactConfiguration {
	public:
		WindowsSplitFATConfiguration();
		virtual ~WindowsSplitFATConfiguration() override;

		ErrorCode setup(const std::string& fatDataFilePath,	const std::string& clusterDataFilePath, const std::string& transactionFilePath);

		virtual ErrorCode shutdown() override;

		virtual ErrorCode create() override;
		virtual ErrorCode open() override;
		virtual ErrorCode close() override;
		virtual FileHandle getClusterDataFile(int accessMode) const override;
		virtual FileHandle getFATDataFile(int accessMode) const override;
		virtual ErrorCode remove() override;
		virtual ErrorCode flushFATDataFile() override;
		virtual ErrorCode flushClusterDataFile() override;
		virtual ErrorCode allocateDataBlock(VolumeManager& volumeManager, uint32_t blockIndex) override;

		virtual bool clusterDataFileExists() const override;
		virtual bool fatDataFileExists() const override;

		virtual ErrorCode createDataPlacementStrategy(std::shared_ptr<DataPlacementStrategyBase>& dataPlacementStrategy,
			VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem) override;

	protected:
		// Transaction
		virtual const char* _getTransactionFinalFilePath() const override;
		virtual const char* _getTransactionTempFilePath() const override;

	private:
		const char* _getVolumeControlDataFilePath() const;
		const char* _getClusterDataFilePath() const;

	private:
		FileHandle mFATDataFile;
		FileHandle mClusterDataFile;
		std::shared_ptr<FileStorageBase>	mFATAndClusterDataStorage; // One storage for all - Volume control data, FAT and Cluster data
		std::string mFATDataFilePath;
		std::string mClusterDataFilePath;

		// Transaction
		std::string mTransactionTempFilePath;
		std::string mTransactionFinalFilePath;
		std::string mTransactionPath;
	};

} // namespace SFAT
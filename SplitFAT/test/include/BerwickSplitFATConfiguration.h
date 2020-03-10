/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#if defined(MCPE_PLATFORM_ORBIS)

#include "SplitFAT/SplitFATTransactConfiguration.h"

#include <string>
#include <memory>

#else

#include "SplitFAT/SplitFATTransactConfiguration.h"

#include <string>
#include <memory>

#endif

// SFAT forward declarations
namespace SFAT {
	class PathString;
}

namespace Core { namespace SFAT {

	class BerwickFileStorage;
	class BerwickCombinedFileStorage;
	class BerwickFileStorageLargeWrites;

	/**
	 *	Access to the lower level file storage for both FAT-data and cluster-data.
	 */
	class BerwickSplitFATConfiguration : public ::SFAT::SplitFATTransactConfiguration {
	public:
		BerwickSplitFATConfiguration();
		virtual ~BerwickSplitFATConfiguration() override;

		::SFAT::ErrorCode setup(const ::SFAT::PathString& storagePath);

		virtual ::SFAT::ErrorCode shutdown() override;

		virtual ::SFAT::ErrorCode create() override;
		virtual ::SFAT::ErrorCode open() override;
		virtual ::SFAT::ErrorCode close() override;
		virtual ::SFAT::FileHandle getClusterDataFile(int accessMode) const override;
		virtual ::SFAT::FileHandle getFATDataFile(int accessMode) const override;
		virtual ::SFAT::ErrorCode remove() override;
		virtual ::SFAT::ErrorCode flushFATDataFile() override;
		virtual ::SFAT::ErrorCode flushClusterDataFile() override;
		virtual ::SFAT::ErrorCode allocateDataBlock(::SFAT::VolumeManager& volumeManager, uint32_t blockIndex) override;
		virtual ::SFAT::ErrorCode defragmentationOnTransactionEnd() override;

		virtual bool clusterDataFileExists() const override;
		virtual bool fatDataFileExists() const override;

		virtual ::SFAT::ErrorCode createDataPlacementStrategy(std::shared_ptr<::SFAT::DataPlacementStrategyBase>& dataPlacementStrategy,
			::SFAT::VolumeManager& volumeManager, ::SFAT::VirtualFileSystem& virtualFileSystem) override;

		// Testing and debugging
		::SFAT::ErrorCode testBlockCaching();

	protected:
		// Transaction
		const char* _getTransactionFinalFilePath() const override;
		const char* _getTransactionTempFilePath() const override;

	private:
		const char* _getVolumeControlDataFilePath() const;
		const char* _getClusterDataFilePath() const;

	private:
		::SFAT::FileHandle mFATDataFile;
		::SFAT::FileHandle mClusterDataFile;
		std::shared_ptr<BerwickFileStorage> mDownloadStorage0; // Volume control data and FAT
		std::shared_ptr<BerwickFileStorageLargeWrites> mDownloadStorage1; // Directory data and file data
		std::shared_ptr<BerwickCombinedFileStorage> mCombinedStorage; // Storage on both /download0, /download1 and system memory buffer
		std::string mFATDataFilePath;
		std::string mClusterDataFilePath;
		std::string mDirectoryDataFilePath;

		// Transaction
		std::string mTransactionTempFilePath;
		std::string mTransactionFinalFilePath;
		std::string mTransactionPath;
	};

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

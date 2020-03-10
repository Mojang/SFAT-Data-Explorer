/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#if defined(MCPE_PLATFORM_ORBIS)

#include "Core/Platform/orbis/file/sfat/BerwickSplitFATConfiguration.h"
#include "Core/Platform/orbis/file/sfat/BerwickFileSystem.h"
#include "Core/Platform/orbis/file/sfat/BerwickCombinedFileSystem.h"
#include "Core/Platform/orbis/file/sfat/BerwickDataPlacementStrategy.h"
#include "Core/Debug/DebugUtils.h"
#include "Core/Debug/Log.h"
#include "SplitFAT/DataBlockManager.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/VolumeManager.h"

#include <algorithm>

#define SFAT_FAT_DATA_FILE_PATH "/download0/fatData.bin";
#define SFAT_CLUSTER_DATA_FILE_PATH "/download1/clusterData.bin"
#define SFAT_DIRECTORY_DATA_FILE_PATH "/download0/dirData.bin"
#define SFAT_TRANSACTION_TEMP_FILE_PATH "/download0/_sfat_trans_temp.bin"
#define SFAT_TRANSACTION_FINAL_FILE_PATH "/download0/_sfat_trans.bin"

#else

#include "BerwickSplitFATConfiguration.h"
#include "BerwickFileSystem.h"
#include "BerwickCombinedFileSystem.h"
#include "BerwickDataPlacementStrategy.h"
#include "SplitFAT/DataBlockManager.h"
#include "SplitFAT/FAT.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/PathString.h"

#include <algorithm>

//#define SFAT_DATA_FOLDER	"E:\\BerwickTest\\CUSA00744\\"
//#define SFAT_DATA_FOLDER	"E:\\BerwickTest\\"
//#define SFAT_DATA_FOLDER	"D:\\PS4\\REPRO 1\\"

#define SFAT_DOWNLOAD0_MOUNT_PATH	"/download0"
#define SFAT_DOWNLOAD1_MOUNT_PATH	"/download1"
#define SFAT_FAT_DATA_FILE_PATH		"/download0/fatData.bin"
#define SFAT_CLUSTER_DATA_FILE_PATH		"/download1/clusterData.bin"
#define SFAT_DIRECTORY_DATA_FILE_PATH	"/download0/dirData.bin"
#define SFAT_TRANSACTION_TEMP_FILE_PATH		"/download0/_sfat_trans_temp.bin"
#define SFAT_TRANSACTION_FINAL_FILE_PATH	"/download0/_sfat_trans.bin"

#endif

namespace Core { namespace SFAT {

	/**************************************************************************
	*	BerwickSplitFATConfiguration implementation
	**************************************************************************/
	
	BerwickSplitFATConfiguration::BerwickSplitFATConfiguration() {
	}

	BerwickSplitFATConfiguration::~BerwickSplitFATConfiguration() {
		shutdown();
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::setup(const ::SFAT::PathString& storagePath) {

		if (!mIsReady) {
			mFATDataFilePath = ::SFAT::PathString::combinePath(storagePath, SFAT_FAT_DATA_FILE_PATH).getString();
			mClusterDataFilePath = ::SFAT::PathString::combinePath(storagePath, SFAT_CLUSTER_DATA_FILE_PATH).getString();
			mDirectoryDataFilePath = ::SFAT::PathString::combinePath(storagePath, SFAT_DIRECTORY_DATA_FILE_PATH).getString();
			mTransactionTempFilePath = ::SFAT::PathString::combinePath(storagePath, SFAT_TRANSACTION_TEMP_FILE_PATH).getString();
			mTransactionFinalFilePath = ::SFAT::PathString::combinePath(storagePath, SFAT_TRANSACTION_FINAL_FILE_PATH).getString();

			std::string download0MountPath(::SFAT::PathString::combinePath(storagePath, SFAT_DOWNLOAD0_MOUNT_PATH).getString());
			std::string download1MountPath(::SFAT::PathString::combinePath(storagePath, SFAT_DOWNLOAD1_MOUNT_PATH).getString());
			mDownloadStorage0 = std::make_shared<BerwickFileStorage>(download0MountPath.c_str());
			mDownloadStorage1 = std::make_shared<BerwickFileStorageLargeWrites>(mDownloadStorage0, download1MountPath.c_str());
			mCombinedStorage = std::make_shared<BerwickCombinedFileStorage>(mDownloadStorage0, mDownloadStorage1, mDirectoryDataFilePath);

			DEBUG_ASSERT(mDownloadStorage0 != nullptr, "Cluster data storage should exist!");
			DEBUG_ASSERT(mDownloadStorage1 != nullptr, "FAT data storage should exist!");
			DEBUG_ASSERT(mCombinedStorage != nullptr, "The combined data storage should exist!");
			
			_transactionSetup(mDownloadStorage0);

			mIsReady = true;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::BerwickSplitFATConfiguration::shutdown() {
		if (mIsReady) {
			DEBUG_ASSERT(mDownloadStorage0 != nullptr, "FAT data storage should exist!");
			DEBUG_ASSERT(mDownloadStorage1 != nullptr, "Cluster data storage should exist!");

			// Close all files
			mClusterDataFile.reset();
			mFATDataFile.reset();

			_transactionShutdown();

			mIsReady = false;
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::open() {
		::SFAT::ErrorCode err = mDownloadStorage0->openFile(mFATDataFile, _getVolumeControlDataFilePath(), "r+b");
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Can't open the FAT control data file.");
			return err;
		}

		err = mCombinedStorage->openFile(mClusterDataFile, _getClusterDataFilePath(), "r+b");
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Can't open the cluster data file.");
		}

		return err;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::create() {
		::SFAT::ErrorCode err = mDownloadStorage0->openFile(mFATDataFile, _getVolumeControlDataFilePath(), "w+b");
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Can't create the initial FAT and volume-control data file.");
			return err;
		}

		err = mCombinedStorage->openFile(mClusterDataFile, _getClusterDataFilePath(), "w+b");
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Can't create the initial cluster data file.");
		}

		return err;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::close() {
		::SFAT::ErrorCode err0 = ::SFAT::ErrorCode::RESULT_OK;
		::SFAT::ErrorCode err1 = ::SFAT::ErrorCode::RESULT_OK;
		if (mFATDataFile.isOpen()) {
			err0 = mFATDataFile.close();
		}

		if (mClusterDataFile.isOpen()) {
			err1 = mClusterDataFile.close();
		}

		if (err0 != ::SFAT::ErrorCode::RESULT_OK) {
			return err0;
		}
		return err1;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::remove() {
		DEBUG_ASSERT(mDownloadStorage0 != nullptr, "FAT data storage should exist!");
		DEBUG_ASSERT(mDownloadStorage1 != nullptr, "Cluster data storage should exist!");

		close();

		if (fatDataFileExists()) {
			::SFAT::ErrorCode err = mDownloadStorage0->deleteFile(_getVolumeControlDataFilePath());
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_FILE, "Can't delete the FAT control data file.");
				return err;
			}
		}

		if (clusterDataFileExists()) {
			::SFAT::ErrorCode err = mDownloadStorage1->deleteFile(_getClusterDataFilePath());
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_FILE, "Can't delete the cluster data file.");
			}
			return err;
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::flushFATDataFile() {
		::SFAT::ErrorCode err = mFATDataFile.flush();
		return err;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::flushClusterDataFile() {
		::SFAT::ErrorCode err = mClusterDataFile.flush();
		return err;
	}

	::SFAT::FileHandle BerwickSplitFATConfiguration::getClusterDataFile(int accessMode) const {
		UNUSED_PARAMETER(accessMode); // To be used for statistics and optimization
		return mClusterDataFile;
	}

	::SFAT::FileHandle BerwickSplitFATConfiguration::getFATDataFile(int accessMode) const {
		UNUSED_PARAMETER(accessMode); // To be used for statistics and optimization
		return mFATDataFile;
	}

	const char* BerwickSplitFATConfiguration::_getVolumeControlDataFilePath() const {
		return mFATDataFilePath.c_str();
	}

	const char* BerwickSplitFATConfiguration::_getClusterDataFilePath() const {
		return mClusterDataFilePath.c_str();
	}

	const char* BerwickSplitFATConfiguration::_getTransactionFinalFilePath() const {
		return mTransactionFinalFilePath.c_str();
	}

	const char* BerwickSplitFATConfiguration::_getTransactionTempFilePath() const {
		return mTransactionTempFilePath.c_str();
	}

	bool BerwickSplitFATConfiguration::fatDataFileExists() const {
		DEBUG_ASSERT(mDownloadStorage0 != nullptr, "FAT data file should exist!");
		return mDownloadStorage0->fileExists(_getVolumeControlDataFilePath());
	}

	bool BerwickSplitFATConfiguration::clusterDataFileExists() const {
		DEBUG_ASSERT(mDownloadStorage1 != nullptr, "Cluster data file should exist!");
		return mDownloadStorage1->fileExists(_getClusterDataFilePath());
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::testBlockCaching() {
		::SFAT::FileBase* file = mClusterDataFile.getImplementation().get();
		BerwickCombinedFile* fileLW = static_cast<BerwickCombinedFile*>(file);
		::SFAT::ErrorCode err = fileLW->copyCacheToBlock(0);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}
		err = fileLW->copyBlockToCache(0);

		return err;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::allocateDataBlock(::SFAT::VolumeManager& volumeManager, uint32_t blockIndex) {
		uint32_t currentBlocksCount = volumeManager.getCountAllocatedDataBlocks();
		if (currentBlocksCount >= volumeManager.getMaxPossibleBlocksCount()) {
			return ::SFAT::ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		::SFAT::FileBase* file = mClusterDataFile.getImplementation().get();
		BerwickCombinedFile* fileLW = static_cast<BerwickCombinedFile*>(file);
		SFAT_ASSERT(fileLW != nullptr, "It is expected to be the correct type!");

		::SFAT::ErrorCode err = fileLW->blockAllocation(blockIndex);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::createDataPlacementStrategy(std::shared_ptr<::SFAT::DataPlacementStrategyBase>& dataPlacementStrategy,
		::SFAT::VolumeManager& volumeManager, ::SFAT::VirtualFileSystem& virtualFileSystem) {
		dataPlacementStrategy = std::make_shared<BerwickDataPlacementStrategy>(volumeManager, virtualFileSystem);
		mCombinedStorage->setDataPlacementStrategy(dataPlacementStrategy);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickSplitFATConfiguration::defragmentationOnTransactionEnd() {
#if (SPLITFAT_ENABLE_DEFRAGMENTATION == 1)
		// Optimize the clusters placement
		::SFAT::FileBase* file = mClusterDataFile.getImplementation().get();
		BerwickCombinedFile* fileLW = static_cast<BerwickCombinedFile*>(file);
		::SFAT::ErrorCode err = fileLW->optimizeCachedBlockContent();
		return err;
#else
		return ::SFAT::ErrorCode::RESULT_OK;
#endif //SPLITFAT_ENABLE_DEFRAGMENTATION
	}

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

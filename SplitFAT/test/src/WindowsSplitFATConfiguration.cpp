/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "WindowsSplitFATConfiguration.h"
#include "WindowsDataPlacementStrategy.h"
#include "SplitFAT/VolumeManager.h"
#include "WindowsFileSystem.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/PathString.h"
#include <algorithm>

namespace SFAT {

	namespace {
		const char* kTransactionFileName = "_sfat_trans.dat";
		const char* kTransactionTempFileName = "_sfat_trans_temp.dat";
	}

	/**************************************************************************
	*	WindowsSplitFATConfiguration implementation
	**************************************************************************/

	WindowsSplitFATConfiguration::WindowsSplitFATConfiguration() {
	}

	WindowsSplitFATConfiguration::~WindowsSplitFATConfiguration() {
		shutdown();
	}

	ErrorCode WindowsSplitFATConfiguration::setup(
		const std::string& fatDataFilePath,
		const std::string& clusterDataFilePath,
		const std::string& transactionPath) {
		(void)transactionPath; // Not used parameter


		if (!mIsReady) {
			mFATDataFilePath = fatDataFilePath;
			mClusterDataFilePath = clusterDataFilePath;
			mTransactionPath = std::move(PathString(fatDataFilePath).getParentPath().getString());
			mTransactionTempFilePath = std::move(PathString::combinePath(mTransactionPath, kTransactionTempFileName).getString());
			mTransactionFinalFilePath = std::move(PathString::combinePath(mTransactionPath, kTransactionFileName).getString());

			mFATAndClusterDataStorage = std::make_shared<WindowsFileStorage>(); // Uses the same storage for FAT, volume control dara and cluster data

			_transactionSetup(mFATAndClusterDataStorage);

			mIsReady = true;
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsSplitFATConfiguration::WindowsSplitFATConfiguration::shutdown() {
		if (mIsReady) {
			SFAT_ASSERT(mFATAndClusterDataStorage != nullptr, "The combine FAT and Cluster data storage should exist!");

			// Close all files
			mClusterDataFile.reset();
			mFATDataFile.reset();

			_transactionShutdown();

			mIsReady = false;
		}

		return ErrorCode::RESULT_OK;
	}


	ErrorCode WindowsSplitFATConfiguration::open() {
		ErrorCode err = mFATAndClusterDataStorage->openFile(mFATDataFile, _getVolumeControlDataFilePath(), "r+b");
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't open the FAT control data file.");
			return err;
		}

		err = mFATAndClusterDataStorage->openFile(mClusterDataFile, _getClusterDataFilePath(), "r+b");
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't open the cluster data file.");
		}
		return err;
	}

	ErrorCode WindowsSplitFATConfiguration::create() {
		ErrorCode err = mFATAndClusterDataStorage->openFile(mFATDataFile, _getVolumeControlDataFilePath(), "w+b");
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't create the initial volume - FAT control data.");
			return err;
		}

		err = mFATAndClusterDataStorage->openFile(mClusterDataFile, _getClusterDataFilePath(), "w+b");
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't create the initial volume - cluster data.");
		}

		return err;
	}

	ErrorCode WindowsSplitFATConfiguration::close() {
		SFAT_ASSERT(mFATAndClusterDataStorage != nullptr, "The combine FAT and Cluster data storage should exist!");

		ErrorCode err0 = ErrorCode::RESULT_OK;
		ErrorCode err1 = ErrorCode::RESULT_OK;
		if (mFATDataFile.isOpen()) {
			err0 = mFATDataFile.close();
		}

		if (mClusterDataFile.isOpen()) {
			err1 = mClusterDataFile.close();
		}

		return (err0 != ErrorCode::RESULT_OK) ? err0 : err1;
	}

	ErrorCode WindowsSplitFATConfiguration::remove() {
		SFAT_ASSERT(mFATAndClusterDataStorage != nullptr, "The combine FAT and Cluster data storage should exist!");

		close();

		if (fatDataFileExists()) {
			ErrorCode err = mFATAndClusterDataStorage->deleteFile(_getVolumeControlDataFilePath());
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't delete the FAT control data file.");
				return err;
			}
		}

		if (clusterDataFileExists()) {
			ErrorCode err = mFATAndClusterDataStorage->deleteFile(_getClusterDataFilePath());
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't delete the cluster data file.");
			}
			return err;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsSplitFATConfiguration::flushFATDataFile() {
		ErrorCode err = mFATDataFile.flush();
		return err;
	}

	ErrorCode WindowsSplitFATConfiguration::flushClusterDataFile() {
		ErrorCode err = mClusterDataFile.flush();
		return err;
	}

	FileHandle WindowsSplitFATConfiguration::getClusterDataFile(int accessMode) const {
		(void)accessMode; // To be used for statistics and optimization
		return mClusterDataFile;
	}

	FileHandle WindowsSplitFATConfiguration::getFATDataFile(int accessMode) const {
		(void)accessMode; // To be used for statistics and optimization
		return mFATDataFile;
	}

	const char* WindowsSplitFATConfiguration::_getVolumeControlDataFilePath() const {
		return mFATDataFilePath.c_str();
	}

	const char* WindowsSplitFATConfiguration::_getClusterDataFilePath() const {
		return mClusterDataFilePath.c_str();
	}

	bool WindowsSplitFATConfiguration::clusterDataFileExists() const {
		SFAT_ASSERT(mFATAndClusterDataStorage != nullptr, "The combine FAT and Cluster data storage should exist!");
		return mFATAndClusterDataStorage->fileExists(_getClusterDataFilePath());
	}

	bool WindowsSplitFATConfiguration::fatDataFileExists() const {
		SFAT_ASSERT(mFATAndClusterDataStorage != nullptr, "The combine FAT and Cluster data storage should exist!");
		return mFATAndClusterDataStorage->fileExists(_getVolumeControlDataFilePath());
	}

	ErrorCode WindowsSplitFATConfiguration::allocateDataBlock(VolumeManager& volumeManager, uint32_t blockIndex) {
		uint32_t currentBlocksCount = volumeManager.getCountAllocatedDataBlocks();
		if (currentBlocksCount >= volumeManager.getMaxPossibleBlocksCount()) {
			return ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND;
		}

		FileHandle file = getClusterDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The cluster data file should be open!");

		FilePositionType position = volumeManager.getDataBlockStartPosition(blockIndex);

		std::vector<uint8_t> buffer;
		//size_t bufferSize = std::min(static_cast<size_t>(1 << 20), mDataBlockSize);
		size_t bufferSize = std::min(static_cast<size_t>(volumeManager.getClusterSize()), static_cast<size_t>(volumeManager.getDataBlockSize()));
		buffer.resize(bufferSize, 0);

		FileSizeType bytesRemainingToWrite = volumeManager.getDataBlockSize();
		while (bytesRemainingToWrite > 0) {
			size_t bytesToWrite = std::min(static_cast<size_t>(bytesRemainingToWrite), bufferSize);
			size_t bytesWritten = 0;
			ErrorCode err = file.writeAtPosition(buffer.data(), bytesToWrite, position, bytesWritten);
			if (err != ErrorCode::RESULT_OK) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X during data block allocation!", err);
				return err;
			}
			if (bytesWritten != bytesToWrite) {
				return ErrorCode::ERROR_EXPANDING_DATA_BLOCK;
			}
			position += bytesToWrite;
			bytesRemainingToWrite -= bytesWritten;
		}

		return file.flush();
	}

	//
	// Transaction
	//

	const char* WindowsSplitFATConfiguration::_getTransactionFinalFilePath() const {
		return mTransactionFinalFilePath.c_str();
	}

	const char* WindowsSplitFATConfiguration::_getTransactionTempFilePath() const {
		return mTransactionTempFilePath.c_str();
	}

	//
	// DataPlacementStrategy
	//

	ErrorCode WindowsSplitFATConfiguration::createDataPlacementStrategy(std::shared_ptr<DataPlacementStrategyBase>& dataPlacementStrategy
		, VolumeManager& volumeManager, VirtualFileSystem& virtualFileSystem) {
		dataPlacementStrategy = std::make_unique<WindowsDataPlacementStrategy>(volumeManager, virtualFileSystem);
		return ErrorCode::RESULT_OK;
	}

} // namespace SFAT
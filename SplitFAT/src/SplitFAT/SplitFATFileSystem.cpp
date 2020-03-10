/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/SplitFATFileSystem.h"
#include "SplitFAT/Common.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/PathString.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/VirtualFileSystem.h"
#include "SplitFAT/FileManipulator.h"
#include "SplitFAT/utils/PathString.h"
#include <stdio.h>

#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
#	include <chrono>
#	include <atomic>
#endif

namespace SFAT {

#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
	struct SplitFATPerformanceCounters {
		std::chrono::time_point<std::chrono::high_resolution_clock> mTransactionStartTime;
		std::chrono::time_point<std::chrono::high_resolution_clock> mTransactionEndTime;
		std::atomic<uint64_t> mReadingDuration; //An integer to keep time in microseconds (1e-6)
		std::atomic<uint64_t> mWritingDuration; //An integer to keep time in microseconds (1e-6)
		std::atomic<size_t> mTotalBytesWritten;
		std::atomic<size_t> mTotalBytesRead;

		SplitFATPerformanceCounters() {
			cleanup();
		}

		void LogPerfCounters() {
			std::chrono::duration<double> diff = mTransactionEndTime - mTransactionStartTime;
			printf("SplitFAT Transaction Counters: %3.3f, Read %5.1fMB, Written %5.1fMB\n", diff.count(), mTotalBytesRead.load() / (float)(1 << 20), mTotalBytesWritten.load() / (float)(1 << 20));
			printf("SplitFAT reading time: %3.3f\n", mReadingDuration.load() / 1000000.0f);
			printf("SplitFAT writing time: %3.3f\n", mWritingDuration.load() / 1000000.0f);
		}

		void cleanup() {
			mReadingDuration = 0;
			mWritingDuration = 0;
			mTotalBytesRead = 0;
			mTotalBytesWritten = 0;
		}
	};
#endif

	SplitFATFile::SplitFATFile(SplitFATFileStorage& fileStorage)
		: FileBase(fileStorage) {
	}

	SplitFATFile::~SplitFATFile() {
		if (isOpen()) {
			close();
		}
	}

	bool SplitFATFile::isOpen() const {
		return (mFileManipulator != nullptr) && mFileManipulator->isValid();
	}

	ErrorCode SplitFATFile::open(const char *szFilePath, uint32_t accessMode) {
		// Close any previously opened file.
		SFAT_ASSERT(!isOpen(), "File reopen is not supported!");
		SFAT_ASSERT(mFileManipulator == nullptr, "Must be nullptr at this point!");

		mAccessMode = accessMode;
		if ((mAccessMode & (AM_READ | AM_WRITE)) == 0) {
			// Should have at least one of these - AM_READ or AM_WRITE
			return ErrorCode::ERROR_FILE_ACCESS_MODE_UNSPECIFIED;
		}

		PathString filePath(szFilePath);
		FileManipulator fileFM;
		ErrorCode err = getVirtualFileSystem().createGenericFileManipulatorForFilePath(filePath, fileFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Assign the original access-mode to the faileManipulator
		fileFM.mAccessMode = accessMode;

		if (!fileFM.isValid()) {
			if ((mAccessMode & AM_CREATE_IF_DOES_NOT_EXIST) != 0) {
				// Create new file
				err = getVirtualFileSystem().createFile(filePath, mAccessMode, ((mAccessMode & AccessMode::AM_BINARY) != 0), fileFM);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}

				SFAT_ASSERT(fileFM.isValid(), "The file should exist here.");
				mFileManipulator = std::make_unique<FileManipulator>(std::move(fileFM));
				return ErrorCode::RESULT_OK;
			}

			return ErrorCode::ERROR_OPENING_FILE_NOT_FOUND;
		}

		if (mAccessMode & AM_TRUNCATE) {
			//Open with truncating the file
			err = getVirtualFileSystem().truncateFile(fileFM, 0);
			if (err != ErrorCode::RESULT_OK) {
				return err;
			}
		}
		
		// Update the access mode
		if (mAccessMode & AM_UPDATE) {
			mAccessMode |= AM_WRITE | AM_READ;
		}

		if (mAccessMode & AM_APPEND) {
			err = getVirtualFileSystem().seek(fileFM, 0, SeekMode::SM_END);
			if (err == ErrorCode::RESULT_OK) {
				return err;
			}
		}

		mFileManipulator = std::make_unique<FileManipulator>(std::move(fileFM));
		return ErrorCode::RESULT_OK;
	}

	ErrorCode SplitFATFile::close() {
		if (!isOpen()) {
			return ErrorCode::RESULT_OK;
		}

		ErrorCode err = flush();
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		mFileManipulator = nullptr;

		return ErrorCode::RESULT_OK;
	}

	ErrorCode SplitFATFile::read(void* buffer, size_t sizeInBytes, size_t& sizeRead) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		std::chrono::time_point<std::chrono::high_resolution_clock> readingStartTime = std::chrono::high_resolution_clock::now();
#endif
		ErrorCode err = getVirtualFileSystem().read(*mFileManipulator, buffer, sizeInBytes, sizeRead);
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		std::chrono::time_point<std::chrono::high_resolution_clock> readingEndTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = readingEndTime - readingStartTime;
		SplitFATPerformanceCounters& perfCounters = getSplitFATFileStorage().getPerformanceCounters();
		perfCounters.mReadingDuration.fetch_add(static_cast<uint32_t>(diff.count() * 1000000.0f), std::memory_order_relaxed);

		if (err == ErrorCode::RESULT_OK) {
			perfCounters.mTotalBytesRead.fetch_add(sizeRead, std::memory_order_relaxed);
		}
#endif
		return err;
	}

	ErrorCode SplitFATFile::write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		std::chrono::time_point<std::chrono::high_resolution_clock> writingStartTime = std::chrono::high_resolution_clock::now();
#endif
		ErrorCode err = getVirtualFileSystem().write(*mFileManipulator, buffer, sizeInBytes, sizeWritten);
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		std::chrono::time_point<std::chrono::high_resolution_clock> writingEndTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = writingEndTime - writingStartTime;
		SplitFATPerformanceCounters& perfCounters = getSplitFATFileStorage().getPerformanceCounters();
		auto time = perfCounters.mWritingDuration.fetch_add(static_cast<uint64_t>(diff.count() * 1000000.0f), std::memory_order_relaxed);

		if (err == ErrorCode::RESULT_OK) {
			perfCounters.mTotalBytesWritten.fetch_add(sizeWritten, std::memory_order_relaxed);
		}
#endif
		SFAT_ASSERT(err == ErrorCode::RESULT_OK, "Low level error?");
		return err;
	}

	ErrorCode SplitFATFile::seek(FilePositionType offset, SeekMode mode) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

		return getVirtualFileSystem().seek(*mFileManipulator, offset, mode);
	}

	ErrorCode SplitFATFile::flush() {
		if (!isOpen()) {
			return ErrorCode::RESULT_OK;
		}

		return getVirtualFileSystem().flush(*mFileManipulator);
	}

	SplitFATFileStorage& SplitFATFile::getSplitFATFileStorage() const {
		return static_cast<SplitFATFileStorage&>(mFileStorage);
	}

	VirtualFileSystem& SplitFATFile::getVirtualFileSystem() const {
		return getSplitFATFileStorage().getVirtualFileSystem();
	}

	ErrorCode SplitFATFile::getPosition(FilePositionType& position) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

		position = mFileManipulator->getPosition();
		return ErrorCode::RESULT_OK;
	}

	ErrorCode SplitFATFile::getSize(FileSizeType& size) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

		size = mFileManipulator->getFileSize();
		return ErrorCode::RESULT_OK;
	}

	/*************************************************************************************
		SplitFATFileStorage implementation
	*************************************************************************************/

	SplitFATFileStorage::SplitFATFileStorage() {
		mVirtualFileSystem = std::make_unique<VirtualFileSystem>();
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		mPerformanceCounters = std::make_unique<SplitFATPerformanceCounters>();
#endif
	}

	SplitFATFileStorage::~SplitFATFileStorage() {
	}

	bool SplitFATFileStorage::fileExists(const char *szFilePath) {
		PathString path(szFilePath);
		return getVirtualFileSystem().fileExists(path);
	}

	bool SplitFATFileStorage::directoryExists(const char *szDirectoryPath) {
		PathString path(szDirectoryPath);
		return getVirtualFileSystem().directoryExists(path);
	}

	bool SplitFATFileStorage::fileOrDirectoryExists(const char *szPath) {
		PathString path(szPath);
		return getVirtualFileSystem().fileOrDirectoryExists(path);
	}

	ErrorCode SplitFATFileStorage::deleteFile(const char *szFilePath) {
		return getVirtualFileSystem().deleteFile(szFilePath);
	}

	ErrorCode SplitFATFileStorage::removeDirectory(const char *szDirectoryPath) {
		return getVirtualFileSystem().removeDirectory(szDirectoryPath);
	}

	ErrorCode SplitFATFileStorage::createFileImpl(std::shared_ptr<FileBase>& fileImpl) {
		fileImpl = std::make_shared<SplitFATFile>(*this);
		return ErrorCode::RESULT_OK;
	}

	VirtualFileSystem& SplitFATFileStorage::getVirtualFileSystem() const {
		return *mVirtualFileSystem;
	}

	// Note that the cleanUp() function removes all disk stored content, but leaves the current file-storage in not workable state.
	// The reason for this is that the control structores and cached buffers are not reinitialized.
	// After the use of the function, we are supposed to destroy the file-storage object, and create a new one if we need so.
	ErrorCode SplitFATFileStorage::cleanUp() {
		SFAT_ASSERT(mVirtualFileSystem != nullptr, "The VirtualFileSystem should be initialized by now!");
		return mVirtualFileSystem->removeVolume();
	}

	ErrorCode SplitFATFileStorage::createDirectory(const char *szDirectoryPath) {
		SFAT_ASSERT(mVirtualFileSystem != nullptr, "The VirtualFileSystem should be initialized by now!");
		FileManipulator directoryFM;
		return mVirtualFileSystem->createDirectory(szDirectoryPath, directoryFM);
	}

	ErrorCode SplitFATFileStorage::setup(std::shared_ptr<SplitFATConfigurationBase> lowLevelFileAccess) {
		return mVirtualFileSystem->setup(std::move(lowLevelFileAccess));
	}

	ErrorCode SplitFATFileStorage::getFileSize(const char *szFilePath, FileSizeType& fileSize) {
		fileSize = 0;
		FileManipulator entityFM;
		ErrorCode err = mVirtualFileSystem->createGenericFileManipulatorForExistingEntity(szFilePath, entityFM);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		if (!entityFM.isValid()) {
			return ErrorCode::ERROR_FILE_COULD_NOT_BE_FOUND;
		}
		if (entityFM.getFileDescriptorRecord().isDirectory()) {
			return ErrorCode::ERROR_CAN_NOT_GET_FILE_SIZE_OF_DIRECTORY;
		}
		fileSize = entityFM.getFileDescriptorRecord().mFileSize;
		return ErrorCode::RESULT_OK;
	}

	bool SplitFATFileStorage::isFile(const char *szEntityPath) {
		FileManipulator entityFM;
		ErrorCode err = mVirtualFileSystem->createGenericFileManipulatorForExistingEntity(szEntityPath, entityFM);
		if (err != ErrorCode::RESULT_OK) {
			return false;
		}
		if (!entityFM.isValid()) {
			return false;
		}
		return entityFM.getFileDescriptorRecord().isFile();
	}

	bool SplitFATFileStorage::isDirectory(const char *szEntityPath) {
		FileManipulator entityFM;
		ErrorCode err = mVirtualFileSystem->createGenericFileManipulatorForExistingEntity(szEntityPath, entityFM);
		if (err != ErrorCode::RESULT_OK) {
			return false;
		}
		if (!entityFM.isValid()) {
			return false;
		}
		return entityFM.getFileDescriptorRecord().isDirectory();
	}

	ErrorCode SplitFATFileStorage::iterateThroughDirectory(const char *szDirectoryPath, uint32_t flags, DirectoryIterationCallback callback) {
		return mVirtualFileSystem->iterateThroughDirectory(szDirectoryPath, flags, callback);
	}

	ErrorCode SplitFATFileStorage::renameFile(const char *szFilePath, const char *szNewName) {
		return mVirtualFileSystem->renameFile(szFilePath, szNewName);
	}

	ErrorCode SplitFATFileStorage::renameDirectory(const char *szDirectoryPath, const char *szNewName) {
		return mVirtualFileSystem->renameDirectory(szDirectoryPath, szNewName);
	}

	//
	// Transaction control functions
	//
	bool SplitFATFileStorage::isInTransaction() const {
		return mVirtualFileSystem->isInTransaction();
	}

	ErrorCode SplitFATFileStorage::tryStartTransaction(bool &started) {
		started = false;
		mTransactionMutex.lock();
		if (mTransactionMutex.getLockCount() > 1) {
			mTransactionMutex.unlock();
			return ErrorCode::RESULT_OK;
		}
		ErrorCode err = mVirtualFileSystem->startTransaction();
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		mPerformanceCounters->cleanup();
		mPerformanceCounters->mTransactionStartTime = std::chrono::high_resolution_clock::now();
#endif
		started = true;
		return err;
	}

	ErrorCode SplitFATFileStorage::endTransaction() {
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		mPerformanceCounters->mTransactionEndTime = std::chrono::high_resolution_clock::now();
		mPerformanceCounters->LogPerfCounters();
#endif
		ErrorCode err = ErrorCode::RESULT_OK;
		if (mVirtualFileSystem->isInTransaction()) {
			ErrorCode errTransEnd = mVirtualFileSystem->endTransaction();
			if (errTransEnd != ErrorCode::RESULT_OK) {
				SFAT_LOGW(LogArea::LA_TRANSACTION, "Transaction end with an error: 0x%4x!", errTransEnd);
			}

			mTransactionMutex.unlock();
		}
		else {
			SFAT_LOGW(LogArea::LA_TRANSACTION, "endTransaction() called without startTransaction()");
		}
		return err;
	}

	ErrorCode SplitFATFileStorage::tryRestoreFromTransactionFile() {
		return mVirtualFileSystem->tryRestoreFromTransactionFile();
	}

	ErrorCode SplitFATFileStorage::getFreeSpace(FileSizeType& countFreeBytes) {
		return mVirtualFileSystem->getFreeSpace(countFreeBytes);
	}

#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
	SplitFATPerformanceCounters& SplitFATFileStorage::getPerformanceCounters() const {
		return *(mPerformanceCounters.get());
	}
#endif

	ErrorCode SplitFATFileStorage::executeDebugCommand(const std::string& path, const std::string& command) {
#if !defined(MCPE_PUBLISH)
		if (command == "transactionMutexLock") {
			mTransactionMutex.lock();
			return ErrorCode::RESULT_OK;
		}
		else if (command == "transactionMutexUnlock") {
			mTransactionMutex.unlock();
			return ErrorCode::RESULT_OK;
		}

		return mVirtualFileSystem->executeDebugCommand(path, command);
#else
		(void)command; // Not used parameter
		return ErrorCode::RESULT_OK;
#endif
	}

} // namespace SFAT

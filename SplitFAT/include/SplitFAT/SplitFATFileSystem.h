/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/utils/Mutex.h"

//Define to 1 to enable the performance counters and print the result in the output console.
#define SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS	0

// Unit-test classes forward declaration
#if !defined(MCPE_PUBLISH)
class TransactionUnitTest_RestoreFromTransaction_Test;
class SfatFunctionalTests;
#endif //!defined(MCPE_PUBLISH)

namespace SFAT {

	class VirtualFileSystem;
	class SplitFATFileStorage;
	class FileManipulator;
	class SplitFATConfigurationBase;
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
	struct SplitFATPerformanceCounters;
#endif

	class SplitFATFile : public FileBase {
	public:
		SplitFATFile(SplitFATFileStorage& fileStorage);
		virtual ~SplitFATFile() override;
		virtual bool isOpen() const override;
		virtual ErrorCode close() override;
		virtual ErrorCode read(void* buffer, size_t sizeInBytes, size_t& sizeRead) override;
		virtual ErrorCode write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) override;
		virtual ErrorCode seek(FilePositionType offset, SeekMode mode) override;
		virtual ErrorCode getPosition(FilePositionType& position) override;
		virtual ErrorCode getSize(FileSizeType& size) override;
		virtual ErrorCode flush() override;
		virtual ErrorCode open(const char *szFilePath, uint32_t accessMode) override;

	private:
		SplitFATFileStorage& getSplitFATFileStorage() const;
		VirtualFileSystem& getVirtualFileSystem() const;
		std::unique_ptr<FileManipulator> mFileManipulator;
	};

	class SplitFATFileStorage : public FileStorageBase {
#if !defined(MCPE_PUBLISH)
		friend class TransactionUnitTest_RestoreFromTransaction_Test;
		friend class SfatFunctionalTests;
#endif //!defined(MCPE_PUBLISH)


	public:
		SplitFATFileStorage();
		virtual ~SplitFATFileStorage() override;

		ErrorCode setup(std::shared_ptr<SplitFATConfigurationBase> lowLevelFileAccess);

		virtual bool fileExists(const char *szFilePath) override;
		virtual bool directoryExists(const char *szDirectoryPath) override;
		virtual bool fileOrDirectoryExists(const char *szPath) override;
		virtual ErrorCode deleteFile(const char *szFilePath) override;
		virtual ErrorCode removeDirectory(const char *szDirectoryPath) override;
		virtual ErrorCode createDirectory(const char *szDirectoryPath) override;
		virtual ErrorCode renameFile(const char *szFilePath, const char *szNewName) override;
		virtual ErrorCode renameDirectory(const char *szDirectoryPath, const char *szNewName) override;
		virtual ErrorCode getFileSize(const char *szFilePath, FileSizeType& fileSize) override;
		virtual bool isFile(const char *szEntityPath) override;
		virtual bool isDirectory(const char *szEntityPath) override;
		
		//TODO: Define the meaning of the flags and implement their use in the SplitFATFileSystem!
		virtual ErrorCode iterateThroughDirectory(const char *szDirectoryPath, uint32_t flags, DirectoryIterationCallback callback) override;
		virtual ErrorCode getFreeSpace(FileSizeType& countFreeBytes) override;

		VirtualFileSystem& getVirtualFileSystem() const;

		ErrorCode cleanUp();

		// Transaction control functions
		bool isInTransaction() const;
		ErrorCode tryStartTransaction(bool &started);
		ErrorCode endTransaction();
		ErrorCode tryRestoreFromTransactionFile();

		ErrorCode executeDebugCommand(const std::string& path, const std::string& command);


#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		SplitFATPerformanceCounters& getPerformanceCounters() const;
#endif
	protected:
		virtual ErrorCode createFileImpl(std::shared_ptr<FileBase>& fileImpl) override;

	private:
		std::unique_ptr<VirtualFileSystem> mVirtualFileSystem;
#if (SPLIT_FAT_ENABLE_PERFORMANCE_COUNTERS == 1)
		std::unique_ptr<SplitFATPerformanceCounters> mPerformanceCounters;
#endif
		SFATRecursiveMutex mTransactionMutex;
	};

} // namespace SFAT

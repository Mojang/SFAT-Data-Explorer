/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/utils/Mutex.h"

// Setting SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC to 1 activates synchronization
// that serialized the read/write access of the threads to a particular file (WindowsFile).
// This is currently not necessary as the SplitFAT used its one synchronization.
// So it should remain set to 0 (disabled). It is kept only for functionality testing purposes.
#define SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC	0

namespace SFAT {

	class WindowsFileStorage;

	class WindowsFile : public FileBase {
	public:
		WindowsFile(WindowsFileStorage& fileStorage);
		virtual ~WindowsFile();
		virtual bool isOpen() const override;
		virtual ErrorCode open(const char *szFilePath, uint32_t accessMode) override;
		virtual ErrorCode open(const char *szFilePath, const char *szMode) override;
		virtual ErrorCode close() override;
		virtual ErrorCode read(void* buffer, size_t sizeInBytes, size_t& sizeRead) override;
		virtual ErrorCode write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) override;
		virtual ErrorCode seek(FilePositionType offset, SeekMode mode) override;
		virtual ErrorCode getPosition(FilePositionType& position) override;
		virtual ErrorCode getSize(FileSizeType& size) override;
		virtual ErrorCode flush() override;

	private:
		FILE* mFile;
		std::string mFilePath;
#if (SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC == 1)
		SFATMutex	mReadWriteMutex;
#endif
	};

	class WindowsFileStorage : public FileStorageBase {
	public:
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
		virtual ErrorCode iterateThroughDirectory(const char *szDirectoryPath, uint32_t flags, DirectoryIterationCallback callback) override {
			(void)szDirectoryPath; // Not used parameter
			(void)flags; // Not used parameter
			(void)callback; // Not used parameter

			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		ErrorCode getFreeSpace(FileSizeType& countFreeBytes) override {
			(void)countFreeBytes; // Not used parameter
			return ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}

	protected:
		virtual ErrorCode createFileImpl(std::shared_ptr<FileBase>& fileImpl) override;
	};

} // namespace SFAT

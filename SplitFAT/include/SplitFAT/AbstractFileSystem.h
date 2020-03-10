/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include "SplitFAT/FileSystemConstants.h"
#include <stdio.h>
#include <string>
#include <memory>
#include <functional>

namespace SFAT {

	class FileStorageBase;
	struct FileDescriptorRecord;
	struct DescriptorLocation;
	using DirectoryIterationCallback = std::function<ErrorCode(bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)>;
	using DirectoryIterationCallbackInternal = std::function<ErrorCode(bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)>;

	class FileBase {
		friend class FileStorageBase;
	public:
		FileBase(FileStorageBase& fileStorage);
		virtual ~FileBase();
		virtual bool isOpen() const = 0;
		virtual ErrorCode close() = 0;
		virtual ErrorCode read(void* buffer, size_t sizeInBytes, size_t& sizeRead) = 0;
		virtual ErrorCode write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) = 0;
		virtual ErrorCode readAtPosition(void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeRead);
		virtual ErrorCode writeAtPosition(const void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeWritten);
		virtual ErrorCode seek(FilePositionType offset, SeekMode mode) = 0;
		virtual ErrorCode getPosition(FilePositionType& position) = 0;
		virtual ErrorCode getSize(FileSizeType& size) = 0;
		virtual ErrorCode flush() = 0;
		bool checkAccessMode(uint32_t accessModeMask) const;

		static uint32_t fileAccessStringToFlags(const char *szAccessMode);

	private:
		virtual ErrorCode open(const char *szFilePath, uint32_t accessMode) = 0;
		virtual ErrorCode open(const char *szFilePath, const char *szMode);

	protected:
		FileStorageBase& mFileStorage;
		uint32_t mAccessMode;
	};

	class FileHandle final {
		friend class FileStorageBase;
	public:
		FileHandle() = default;
		~FileHandle() = default;

		bool isValid() const;
		bool isOpen() const;
		ErrorCode close();
		ErrorCode read(void* buffer, size_t sizeInBytes, size_t& sizeRead);
		ErrorCode write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten);
		ErrorCode readAtPosition(void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeRead);
		ErrorCode writeAtPosition(const void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeWritten);
		ErrorCode seek(FilePositionType offset, SeekMode mode);
		ErrorCode getPosition(FilePositionType& position);
		ErrorCode flush();
		bool checkAccessMode(uint32_t accessModeMask) const;
		ErrorCode reset();

		std::shared_ptr<FileBase> getImplementation() { return mFileImpl; }
	private:
		std::shared_ptr<FileBase> mFileImpl;
	};

	class FileStorageBase {
		friend class FileHandle; // FileHandle should be able to call createFileImpl(std::shared_ptr<FileBase>& fileImpl), which is a factory for concrete FileBase implementation
	public:
		FileStorageBase() = default;
		virtual ~FileStorageBase() = default;
		virtual bool fileExists(const char *szFilePath) = 0;
		virtual bool directoryExists(const char *szDirectoryPath) = 0;
		virtual bool fileOrDirectoryExists(const char *szPath) = 0;
		virtual ErrorCode deleteFile(const char *szFilePath) = 0;
		virtual ErrorCode removeDirectory(const char *szDirectoryPath) = 0;
		virtual ErrorCode createDirectory(const char *szDirectoryPath) = 0;
		virtual ErrorCode renameFile(const char *szFilePath, const char *szNewName) = 0;
		virtual ErrorCode renameDirectory(const char *szDirectoryPath, const char *szNewName) = 0;
		virtual ErrorCode getFileSize(const char *szFilePath, FileSizeType& fileSize) = 0;
		virtual bool isFile(const char *szEntityPath) = 0;
		virtual bool isDirectory(const char *szEntityPath) = 0;
		virtual ErrorCode iterateThroughDirectory(const char *szDirectoryPath, uint32_t flags, DirectoryIterationCallback callback) = 0;
		virtual ErrorCode getFreeSpace(FileSizeType& countFreeBytes) = 0;

		ErrorCode openFile(FileHandle& fileHandle, const char *szFilePath, uint32_t accessMode);
		ErrorCode openFile(FileHandle& fileHandle, const char *szFilePath, const char *szMode);

	protected:
		virtual ErrorCode createFileImpl(std::shared_ptr<FileBase>& fileImpl) = 0;
	};

} // namespace SFAT
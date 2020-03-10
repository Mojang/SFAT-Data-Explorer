/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#if defined(MCPE_PLATFORM_ORBIS)

#include "SplitFAT/Common.h"
#include "SplitFAT/AbstractFileSystem.h"

#include "Platform/Threading/Mutex.h"
#include <unistd.h>

#else

#include "BerwickToWindowsPort.h"

#include "SplitFAT/Common.h"
#include "SplitFAT/AbstractFileSystem.h"

#include <memory>

#endif

struct SceAppContentMountPoint;

namespace Core { namespace SFAT {

	class BerwickFileStorage;

	class BerwickFile : public ::SFAT::FileBase {
	public:
		BerwickFile(BerwickFileStorage& fileStorage);
		virtual ~BerwickFile();
		virtual bool isOpen() const override;
		virtual ::SFAT::ErrorCode open(const char *szFilePath, uint32_t accessMode) override;
		virtual ::SFAT::ErrorCode close() override;
		
		// Disabling the use of the function because the getPosition() function is may not be reliable otherwise
		// Note that the SplitFAT file system will rely entirely on readAtPosition()
		virtual ::SFAT::ErrorCode read(void*, size_t, size_t&) override final {
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		// Disabling the use of the function because the getPosition() function may not be reliable otherwise
		// Note that the SplitFAT file system will rely entirely on writeAtPosition()
		virtual ::SFAT::ErrorCode write(const void*, size_t, size_t&) override final {
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}

		virtual ::SFAT::ErrorCode readAtPosition(void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeRead) override;
		virtual ::SFAT::ErrorCode writeAtPosition(const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) override;
		virtual ::SFAT::ErrorCode seek(::SFAT::FilePositionType offset, ::SFAT::SeekMode mode) override;
		virtual ::SFAT::ErrorCode getPosition(::SFAT::FilePositionType& position) override;
		virtual ::SFAT::ErrorCode getSize(::SFAT::FileSizeType& size) override;
		virtual ::SFAT::ErrorCode flush() override;

		int getFileDescriptor() const { return mFD; }

	protected:
		int mFD;
		std::string mFilePath;
		::SFAT::FilePositionType mPosition;
		Bedrock::Threading::RecursiveMutex mReadWriteMutex;
	};

	class BerwickFileStorage : public ::SFAT::FileStorageBase {
	public:
		BerwickFileStorage(const std::string mountPath);
		virtual ~BerwickFileStorage() override;
		virtual bool fileExists(const char *szFilePath) override;
		virtual bool directoryExists(const char *szDirectoryPath) override;
		virtual bool fileOrDirectoryExists(const char *szPath) override;
		virtual ::SFAT::ErrorCode deleteFile(const char *szFilePath) override;
		virtual ::SFAT::ErrorCode removeDirectory(const char *szDirectoryPath) override;
		virtual ::SFAT::ErrorCode createDirectory(const char *szDirectoryPath) override;
		virtual ::SFAT::ErrorCode renameFile(const char *szFilePath, const char *szNewName) override;
		virtual ::SFAT::ErrorCode renameDirectory(const char *szDirectoryPath, const char *szNewName) override;
		virtual ::SFAT::ErrorCode getFileSize(const char *szFilePath, ::SFAT::FileSizeType& fileSize) override;
		virtual bool isFile(const char *szEntityPath) override;
		virtual bool isDirectory(const char *szEntityPath) override;

		virtual ::SFAT::ErrorCode iterateThroughDirectory(const char *szDirectoryPath, uint32_t flags, ::SFAT::DirectoryIterationCallback callback) override {
			UNUSED_PARAMETER(szDirectoryPath);
			UNUSED_PARAMETER(flags);
			UNUSED_PARAMETER(callback);
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}

		::SFAT::ErrorCode getFreeSpace(::SFAT::FileSizeType& countFreeBytes) override {
			UNUSED_PARAMETER(countFreeBytes);
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}

		const std::string getMountPath() const;

	protected:
		virtual ::SFAT::ErrorCode createFileImpl(std::shared_ptr<::SFAT::FileBase>& fileImpl) override;
		std::unique_ptr<SceAppContentMountPoint> mMountPoint;

	private:
		int initialize();
	
	private:
		std::string mDownload0MountPath;
	};

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

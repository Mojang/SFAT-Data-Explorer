/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#if defined(MCPE_PLATFORM_ORBIS)

#include "SplitFAT/Common.h"
#include "Core/Platform/orbis/file/sfat/BerwickFileSystem.h"
#include <unistd.h>
#include <vector>

#else

#include "SplitFAT/Common.h"
#include "BerwickFileSystem.h"
#include <vector>

#endif

struct SceAppContentMountPoint;

namespace Core { namespace SFAT {

	class BerwickFileStorageLargeWrites;

	class BerwickFileLargeWrites : public ::SFAT::FileBase {
	public:
		BerwickFileLargeWrites(BerwickFileStorageLargeWrites& fileStorage);
		virtual ~BerwickFileLargeWrites() override;
		virtual bool isOpen() const override;
		virtual ::SFAT::ErrorCode open(const char *szFilePath, uint32_t accessMode) override;
		virtual ::SFAT::ErrorCode close() override;

		// Disabling the use of the function because the getPosition() function is may not be reliable otherwise
		// Note that the SplitFAT file system will rely entirely on readAtPosition()
		virtual ::SFAT::ErrorCode read(void* buffer, size_t sizeInBytes, size_t& sizeRead) override final {
			UNUSED_PARAMETER(buffer);
			UNUSED_PARAMETER(sizeInBytes);
			UNUSED_PARAMETER(sizeRead);
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		// Disabling the use of the function because the getPosition() function may not be reliable otherwise
		// Note that the SplitFAT file system will rely entirely on writeAtPosition()
		virtual ::SFAT::ErrorCode write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) override final {
			UNUSED_PARAMETER(buffer);
			UNUSED_PARAMETER(sizeInBytes);
			UNUSED_PARAMETER(sizeWritten);
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}

		virtual ::SFAT::ErrorCode readAtPosition(void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeRead) override;
		virtual ::SFAT::ErrorCode writeAtPosition(const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) override;
		virtual ::SFAT::ErrorCode seek(::SFAT::FilePositionType offset, ::SFAT::SeekMode mode) override;
		virtual ::SFAT::ErrorCode getPosition(::SFAT::FilePositionType& position) override;
		virtual ::SFAT::ErrorCode getSize(::SFAT::FileSizeType& size) override;
		virtual ::SFAT::ErrorCode flush() override;

		::SFAT::ErrorCode blockAllocation(uint32_t blockIndex);

	private:
		::SFAT::ErrorCode _initialBlockAllocation(const char* szFilePath);

	private:
		const uint32_t mBlockSize = 256 * (1 << 20); // 256MB
		const size_t mChunkSize = 256 * (1 << 10); // 256KB - the read/write access should be aligned and on portions of this.
		const size_t mTotalBlocksCount = 24; // Synchronize with mMaxBlocksCount in VolumeDescriptor::initializeWithDefaults()
		BerwickFile mReadFile; // /download1 data area
		BerwickFile mWriteFile; // /download1 data area
		uint32_t mOriginalAccessMode;
	};

	class BerwickFileStorageLargeWrites : public BerwickFileStorage {
		friend class BerwickFileLargeWrites; //Giving access to mBerwickFileStorage
	public:
		BerwickFileStorageLargeWrites(std::shared_ptr<BerwickFileStorage> berwickFileStorage, std::string download1MountPath);
		~BerwickFileStorageLargeWrites();

		// Returns an error stating that it is not supported for the Large Writes file storage
		virtual ::SFAT::ErrorCode removeDirectory(const char *szDirectoryPath) override;
		// Returns an error stating that it is not supported for the Large Writes file storage
		virtual ::SFAT::ErrorCode createDirectory(const char *szDirectoryPath) override;

	private:
		::SFAT::ErrorCode _initialize();
		bool isAvailable();

	protected:
		virtual ::SFAT::ErrorCode createFileImpl(std::shared_ptr<::SFAT::FileBase>& fileImpl) override;

	private:
		std::shared_ptr<BerwickFileStorage> mBerwickFileStorage;
		std::string mDownload1MountPath;
	};

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

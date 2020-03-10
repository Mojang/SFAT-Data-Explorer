/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#if defined(MCPE_PLATFORM_ORBIS)

#include "Core/Platform/orbis/file/sfat/BerwickFileSystemLargeWrites.h"
#include "Core/Debug/DebugUtils.h"
#include "Core/Debug/Log.h"
#include <stdio.h>

#include <kernel.h>
#include <app_content.h>
#include <libsysmodule.h>
#include <vector>

#define SFAT_MOUNT_POINT_PATH "/download1"

#else

#include "BerwickFileSystemLargeWrites.h"
#include <stdio.h>

#include <vector>
#include <algorithm>

#endif

#define FILE_UNIT_SIZE (0x10000000)	//256MiB

// Defining SFAT_ENABLE_BLOCK_INITIALIZATION to 1 will enable the block initialization at block allocation time.
// The initialization is a slow process - about 5 seconds per block.
// Currently the initialization is not necessary for the correct work of the file system.
// ENABLE this only for debug purposes!
#define SFAT_ENABLE_BLOCK_INITIALIZATION	1

namespace Core { namespace SFAT {

	/*************************************************************************************
		BerwickFileLargeWrites implementation
	*************************************************************************************/

	BerwickFileLargeWrites::BerwickFileLargeWrites(BerwickFileStorageLargeWrites& fileStorage)
		: ::SFAT::FileBase(fileStorage)
		, mReadFile(fileStorage)
		, mWriteFile(fileStorage)
		, mOriginalAccessMode(::SFAT::AM_UNSPECIFIED) {
	}

	BerwickFileLargeWrites::~BerwickFileLargeWrites() {
	}

	bool BerwickFileLargeWrites::isOpen() const {
		return mReadFile.isOpen() || mWriteFile.isOpen();
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::open(const char *szFilePath, uint32_t accessMode) {
		SceKernelStat status;
		memset(&status, 0, sizeof(SceKernelStat));
		int res = sceKernelStat(szFilePath, &status);
		if ((res < 0) && (res != SCE_KERNEL_ERROR_ENOENT)){
			ALOGE(LOG_AREA_PLATFORM, "Can't get the file status for \"%s\"! Error code #%8X", szFilePath, res);
			return ::SFAT::ErrorCode::ERROR_GETTING_FILE_STATUS;
		}

		if ((res == SCE_KERNEL_ERROR_ENOENT) || (status.st_size == 0)) {
			// We need to allocate initial block
			::SFAT::ErrorCode err = _initialBlockAllocation(szFilePath);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}

			// Verify
			res = sceKernelStat(szFilePath, &status);
			if ((res < 0) && (res != SCE_KERNEL_ERROR_ENOENT)) {
				ALOGE(LOG_AREA_PLATFORM, "Can't get the file status for \"%s\"! Error code #%8X", szFilePath, res);
				return ::SFAT::ErrorCode::ERROR_GETTING_FILE_STATUS;
			}
		}

		// Save the original accessMode
		mOriginalAccessMode = mAccessMode = accessMode;
		if ((accessMode & ::SFAT::AM_READ) != 0) {
			// Open the file for reading only. On the current platform, the combined READ_WRITE access is not allowed.
			::SFAT::ErrorCode err = mReadFile.open(szFilePath, accessMode & ~(::SFAT::AM_WRITE | ::SFAT::AM_CREATE_IF_DOES_NOT_EXIST | ::SFAT::AM_TRUNCATE));
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_PLATFORM, "Can't open the file \"%s\" for read!", szFilePath);
				return err;
			}
		}

		if ((accessMode & ::SFAT::AM_WRITE) != 0) {
			if (!mWriteFile.isOpen()) {
				// Open for write only. We don't want to create it (should be already created) or truncate it.
				::SFAT::ErrorCode err = mWriteFile.open(szFilePath, accessMode & ~(::SFAT::AM_READ | ::SFAT::AM_CREATE_IF_DOES_NOT_EXIST | ::SFAT::AM_TRUNCATE));
				if (err == ::SFAT::ErrorCode::RESULT_OK) {
					int ret = sceKernelLwfsSetAttribute(mWriteFile.getFileDescriptor(), SCE_KERNEL_LWFS_ENABLE);
					if (ret < 0) {
						ALOGE(LOG_AREA_PLATFORM, "sceKernelLwfsSetAttribute() [%d] error ret = [%x]\n", SCE_KERNEL_LWFS_ENABLE, ret);
						return ::SFAT::ErrorCode::ERROR_OPENING_FILE_LOW_LEVEL;
					}
					else {
						ALOGI(LOG_AREA_PLATFORM, "Large writes enabled!");
					}
				}
				else {
					ALOGE(LOG_AREA_PLATFORM, "Can't open the file \"%s\" for write!", szFilePath);
					return err;
				}
			}
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::close() {
		::SFAT::ErrorCode errWriteFile = mWriteFile.close();
		::SFAT::ErrorCode errReadFile = mReadFile.close();

		// The write error code has higher priority!
		if (errWriteFile != ::SFAT::ErrorCode::RESULT_OK) {
			return errWriteFile;
		}

		return errReadFile;
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::readAtPosition(void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeRead) {
		DEBUG_ASSERT((position % mChunkSize == 0), "The position should be multiple of 256KB!");
		DEBUG_ASSERT((sizeInBytes % mChunkSize == 0), "The write-size should be multiple of 256KB!");

		return mReadFile.readAtPosition(buffer, sizeInBytes, position, sizeRead);
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::writeAtPosition(const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) {
		sizeWritten = 0;

		DEBUG_ASSERT((position % mChunkSize == 0), "The position should be multiple of 256KB!");
		DEBUG_ASSERT((sizeInBytes % mChunkSize == 0), "The write-size should be multiple of 256KB!");
		int fileDescriptor = mWriteFile.getFileDescriptor();
		DEBUG_ASSERT(fileDescriptor >= 0, "The file descriptor should not be negative!");

		off_t res = sceKernelLwfsLseek(fileDescriptor, static_cast<off_t>(position), SCE_KERNEL_SEEK_SET);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't set the read/write position! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_POSITIONING_IN_FILE_LOW_LEVEL;
		}

		ssize_t writeResult = sceKernelLwfsWrite(fileDescriptor, buffer, sizeInBytes);
		if (writeResult < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't write to LW file! Error code #%8X", writeResult);
			return ::SFAT::ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}

		sizeWritten = static_cast<size_t>(writeResult);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::seek(::SFAT::FilePositionType offset, ::SFAT::SeekMode mode) {
		UNUSED_PARAMETER(mode);
		DEBUG_ASSERT((offset % mChunkSize == 0), "The position should be multiple of 256KB!");
		int fileDescriptor = mWriteFile.getFileDescriptor();
		DEBUG_ASSERT(fileDescriptor >= 0, "The file descriptor should not be negative!");

		off_t res = sceKernelLwfsLseek(fileDescriptor, static_cast<off_t>(offset), SCE_KERNEL_SEEK_SET);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't set the read/write position! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_POSITIONING_IN_FILE_LOW_LEVEL;
		}

		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::getPosition(::SFAT::FilePositionType& position) {
		UNUSED_PARAMETER(position);
		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::getSize(::SFAT::FileSizeType& size) {
		UNUSED_PARAMETER(size);
		return ::SFAT::ErrorCode::NOT_IMPLEMENTED;
	}


	::SFAT::ErrorCode BerwickFileLargeWrites::flush() {
		return mWriteFile.flush();
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::_initialBlockAllocation(const char* szFilePath) {
		if (mWriteFile.isOpen()) {
			mWriteFile.close();
		}
		if (mReadFile.isOpen()) {
			mReadFile.close();
		}
		int ret = sceKernelUnlink(szFilePath);

		DEBUG_ASSERT(!mWriteFile.isOpen(), "Should not be currently open!");
		DEBUG_ASSERT(!isOpen(), "Should not be currently open!");

		::SFAT::ErrorCode err = mWriteFile.open(szFilePath, ::SFAT::AM_WRITE | ::SFAT::AM_BINARY | ::SFAT::AM_CREATE_IF_DOES_NOT_EXIST);
		if (err == ::SFAT::ErrorCode::RESULT_OK) {
			ret = sceKernelLwfsSetAttribute(mWriteFile.getFileDescriptor(), SCE_KERNEL_LWFS_ENABLE);
			if (ret < 0) {
				ALOGE(LOG_AREA_PLATFORM, "sceKernelLwfsSetAttribute() [%d] error ret = [%x]\n", SCE_KERNEL_LWFS_ENABLE, ret);
				return ::SFAT::ErrorCode::ERROR_OPENING_FILE_LOW_LEVEL;
			}
			else {
				ALOGI(LOG_AREA_PLATFORM, "Large writes enabled!");
			}
		}
		else {
			ALOGE(LOG_AREA_PLATFORM, "Can't open the file \"%s\" for write!", szFilePath);
			return err;
		}

		ret = sceKernelLwfsAllocateBlock(mWriteFile.getFileDescriptor(), FILE_UNIT_SIZE * mTotalBlocksCount);
		if (ret < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't allocate block! Error #%8X", ret);
			err = ::SFAT::ErrorCode::ERROR_OPENING_FILE_LOW_LEVEL;
			::SFAT::ErrorCode closingError = mWriteFile.close();
			(void)closingError; //Ignore the closing error.
		}
		else {
			ALOGI(LOG_AREA_PLATFORM, "Data block allocated!");
			err = mWriteFile.close();
		}

		return err;
	}

	::SFAT::ErrorCode BerwickFileLargeWrites::blockAllocation(uint32_t blockIndex) {
#if (SFAT_ENABLE_BLOCK_INITIALIZATION == 1)
		::SFAT::FilePositionType position = blockIndex * mBlockSize;
		std::vector<uint8_t> buffer;
		size_t bufferSize = std::min(static_cast<size_t>(16 * (1 << 20)), static_cast<size_t>(mBlockSize));
#if !defined(MCPE_PUBLISH)
		buffer.resize(bufferSize, 0x80 + (blockIndex & 0x3f));
#else
		buffer.resize(bufferSize, 0);
#endif
		auto startTime = std::chrono::high_resolution_clock::now();

		size_t bytesRemainingToWrite = mBlockSize;
		while (bytesRemainingToWrite > 0) {
			size_t bytesToWrite = std::min(bytesRemainingToWrite, bufferSize);
			size_t bytesWritten = 0;

			::SFAT::ErrorCode err = ::SFAT::ErrorCode::RESULT_OK;
			{
				off_t seekRes = sceKernelLwfsLseek(mWriteFile.getFileDescriptor(), static_cast<off_t>(position), SCE_KERNEL_SEEK_SET);
				if (seekRes < 0) {
					ALOGE(LOG_AREA_PLATFORM, "Can't set the read/write position! Error code #%8X", seekRes);
					return ::SFAT::ErrorCode::ERROR_POSITIONING_IN_FILE_LOW_LEVEL;
				}

				ssize_t res = sceKernelLwfsWrite(mWriteFile.getFileDescriptor(), buffer.data(), bytesToWrite);
				if (res < 0) {
					ALOGE(LOG_AREA_PLATFORM, "Can't write to file! Error code #%8X", res);
					return ::SFAT::ErrorCode::ERROR_WRITING_LOW_LEVEL;
				}

				bytesWritten = static_cast<size_t>(res);
			}
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_PLATFORM, "Error #%08X during data block allocation!", err);
				return err;
			}
			if (bytesWritten != bytesToWrite) {
				return ::SFAT::ErrorCode::ERROR_EXPANDING_DATA_BLOCK;
			}
			position += bytesToWrite;
			bytesRemainingToWrite -= bytesWritten;
		}

		::SFAT::ErrorCode err = mWriteFile.flush();

		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> duration = endTime - startTime;

		ALOGI(LOG_AREA_PLATFORM, "Block #%u allocation: %3.3fsecs\n", blockIndex, duration.count());

		return err;
#else 
		UNUSED1(blockIndex);
		return ::SFAT::ErrorCode::RESULT_OK;
#endif 
	}


	/*************************************************************************************
		BerwickFileStorageLargeWrites implementation
	*************************************************************************************/

	BerwickFileStorageLargeWrites::BerwickFileStorageLargeWrites(std::shared_ptr<BerwickFileStorage> berwickFileStorage, std::string download1MountPath)
		: BerwickFileStorage(berwickFileStorage->getMountPath())
		, mBerwickFileStorage(std::move(berwickFileStorage))
		, mDownload1MountPath(std::move(download1MountPath)) {
		_initialize();
	}

	BerwickFileStorageLargeWrites::~BerwickFileStorageLargeWrites() {
	}

	::SFAT::ErrorCode BerwickFileStorageLargeWrites::_initialize() {
		mMountPoint = std::make_unique<SceAppContentMountPoint>();
		strncpy(mMountPoint->data, mDownload1MountPath.c_str(), SCE_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE);

		//Check if download1 is available.
		SceKernelStat stat;
		int ret = sceKernelStat(mMountPoint->data, &stat);
		if (ret != SCE_OK) {
			//Please make sure to configure 'Storage setting(1)' in 'param.sfo' with 'orbis-pub-sfo.exe'
			//or 'param.sfo' is in the correct position. 
			ALOGE(LOG_AREA_PLATFORM, "The /download1 is not available. Error #%8X", ret);
			return ::SFAT::ErrorCode::ERROR_CLUSTER_DATA_STORAGE_NOT_AVAILABLE;
		}
		else {
			ALOGI(LOG_AREA_PLATFORM, "The /download1 is available.");
		}

		//Get download data available space.
		size_t availableSpaceKb;
		ret = sceAppContentDownloadDataGetAvailableSpaceKb(mMountPoint.get(), &availableSpaceKb);
		if (ret != SCE_OK) {
			ALOGE(LOG_AREA_PLATFORM, "Can not determine the available space for /download1 storage. Error #%8X", ret);
			return ::SFAT::ErrorCode::ERROR_CAN_NOT_GET_AVAILABLE_STORAGE_SPACE;
		}
		else {
			ALOGE(LOG_AREA_PLATFORM, "The /download1 is available space is %ld kb.", availableSpaceKb);
		}
	
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorageLargeWrites::createFileImpl(std::shared_ptr<::SFAT::FileBase>& fileImpl) {
		fileImpl = std::make_shared<BerwickFileLargeWrites>(*this);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	bool BerwickFileStorageLargeWrites::isAvailable() {
		SceAppContentMountPoint	mountPoint;
		strncpy(mountPoint.data, mDownload1MountPath.c_str(), SCE_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE);

		//Check if /download1 is available.
		SceKernelStat stat;
		int ret = sceKernelStat(mountPoint.data, &stat);
		if (ret != SCE_OK) {
			//Please make sure to configure 'Storage setting(1)' in 'param.sfo' with 'orbis-pub-sfo.exe'
			//or 'param.sfo' is in the correct position. 
			ALOGE(LOG_AREA_PLATFORM, "/download1 is not available.");
			return false;
		}
		else {
			ALOGI(LOG_AREA_PLATFORM, "/download1 is available.");
		}
		return true;
	}

	::SFAT::ErrorCode BerwickFileStorageLargeWrites::removeDirectory(const char *szDirectoryPath) {
		UNUSED_PARAMETER(szDirectoryPath);
		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickFileStorageLargeWrites::createDirectory(const char *szDirectoryPath) {
		UNUSED_PARAMETER(szDirectoryPath);
		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

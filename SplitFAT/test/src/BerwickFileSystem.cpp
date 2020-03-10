/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#if defined(MCPE_PLATFORM_ORBIS)

#include "Core/Platform/orbis/file/sfat/BerwickFileSystem.h"
#include "Core/Debug/DebugUtils.h"
#include "Core/Debug/Log.h"
#include <stdio.h>

#include <kernel.h>
#include <app_content.h>
#include <libsysmodule.h>
#include <fios2.h>

#define SFAT_MOUNT_POINT_DOWNLOAD0_PATH "/download0"

#else

#include "BerwickFileSystem.h"
#include <stdio.h>

#endif


#define FILE_UNIT_SIZE (0x10000000)	//256MiB

namespace Core { namespace SFAT {

	/*************************************************************************************
		BerwickFile implementation
	*************************************************************************************/

	BerwickFile::BerwickFile(BerwickFileStorage& fileStorage)
		: FileBase(fileStorage)
		, mFD(-1)
		, mPosition(0) {
	}

	BerwickFile::~BerwickFile() {
		if (isOpen()) {
			close();
		}
	}

	bool BerwickFile::isOpen() const {
		return (mFD >= 0);
	}

	::SFAT::ErrorCode BerwickFile::open(const char *szFilePath, uint32_t accessMode) {
		DEBUG_ASSERT(!isOpen(), "File reopen is not supported!");

		mAccessMode = accessMode;

		// Analyze and use the szMode when opening the file below.
		int flags = 0;
		if (checkAccessMode(::SFAT::AM_APPEND)) {
			flags |= SCE_KERNEL_O_APPEND;
		}
		if (checkAccessMode(::SFAT::AM_TRUNCATE)) {
			flags |= SCE_KERNEL_O_TRUNC;
		}
		if (checkAccessMode(::SFAT::AM_CREATE_IF_DOES_NOT_EXIST)) {
			flags |= SCE_KERNEL_O_CREAT;
		}
		else {
			flags |= SCE_KERNEL_O_EXCL;
		}
		if (checkAccessMode(::SFAT::AM_READ | ::SFAT::AM_WRITE)) {
			flags |= SCE_KERNEL_O_RDWR;
		}
		else if (checkAccessMode(::SFAT::AM_WRITE)) {
			flags |= SCE_KERNEL_O_WRONLY;
		}
		else {
			SFAT_ASSERT(checkAccessMode(::SFAT::AM_READ), "The accessMode should be AM_READ at this point!");
			flags |= SCE_KERNEL_O_RDONLY;
		}

		// Use cache as little as possible.
		flags |= SCE_KERNEL_O_DIRECT; //Both, for read and write the same level of caching.
		// Perform synchronized writing.
		if (checkAccessMode(::SFAT::AM_WRITE)) {
			flags |= SCE_KERNEL_O_FSYNC;
		}

		mFilePath = szFilePath;

		int res = sceKernelOpen(szFilePath, flags, SCE_KERNEL_S_IRWU);
		if (res < 0) {
			ALOGI(LOG_AREA_PLATFORM, "Can't open file! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_OPENING_FILE_LOW_LEVEL;
		}

		mFD = res;

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFile::close() {
		::SFAT::ErrorCode err = ::SFAT::ErrorCode::RESULT_OK;
		if (mFD >= 0) {
			err = flush();
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_PLATFORM, "Can't flush cached data before file close!");
			}
			int res = sceKernelClose(mFD);
			mFD = -1;
			if (res < 0) {
				ALOGE(LOG_AREA_PLATFORM, "Can't close file! Error code #%8X", res);
				err = ::SFAT::ErrorCode::ERROR_CLOSING_FILE_LOW_LEVEL;
			}
		}
		else {
			ALOGW(LOG_AREA_PLATFORM, "Trying to close a file that is not opened!");
		}

		return err;
	}

	::SFAT::ErrorCode BerwickFile::readAtPosition(void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeRead) {
		Bedrock::Threading::LockGuard<decltype(mReadWriteMutex)> lock(mReadWriteMutex);
		sizeRead = 0;

		::SFAT::ErrorCode err = seek(position, ::SFAT::SeekMode::SM_SET);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		ssize_t res = sceKernelRead(mFD, buffer, sizeInBytes);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't read from file! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_READING_LOW_LEVEL;
		}

		sizeRead = static_cast<size_t>(res);
		mPosition += sizeRead;
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFile::writeAtPosition(const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) {
		Bedrock::Threading::LockGuard<decltype(mReadWriteMutex)> lock(mReadWriteMutex);
		sizeWritten = 0;

		::SFAT::ErrorCode err = seek(position, ::SFAT::SeekMode::SM_SET);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		ssize_t res = sceKernelWrite(mFD, buffer, sizeInBytes);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't write to file! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}

		sizeWritten = static_cast<size_t>(res);
		mPosition += sizeWritten;
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFile::seek(::SFAT::FilePositionType offset, ::SFAT::SeekMode mode) {
		Bedrock::Threading::LockGuard<decltype(mReadWriteMutex)> lock(mReadWriteMutex);

		int seekMode = SCE_KERNEL_SEEK_SET;
		if (mode == ::SFAT::SeekMode::SM_CURRENT) {
			seekMode = SCE_KERNEL_SEEK_CUR;
			mPosition += offset;
		}
		else if (mode == ::SFAT::SeekMode::SM_END) {
			seekMode = SCE_KERNEL_SEEK_END;
			::SFAT::FileSizeType size = 0;
			::SFAT::ErrorCode err = getSize(size);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}
			mPosition = ::SFAT::sizeToPosition(size) + offset;
		}
		else {
			mPosition = offset;
		}

		off_t res = sceKernelLseek(mFD, static_cast<off_t>(offset), seekMode);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't set the read/write position! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_POSITIONING_IN_FILE_LOW_LEVEL;
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFile::getPosition(::SFAT::FilePositionType& position) {
		if (mFD < 0) {
			return ::SFAT::ErrorCode::ERROR_FILE_NOT_OPENED;
		}
		position = mPosition;
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFile::flush() {
		Bedrock::Threading::LockGuard<decltype(mReadWriteMutex)> lock(mReadWriteMutex);

		if (!isOpen()) {
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		int res = sceKernelFsync(mFD);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't flush the data! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_FLUSH_LOW_LEVEL;
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFile::getSize(::SFAT::FileSizeType& size) {
		size = 0;

		if (!isOpen()) {
			return ::SFAT::ErrorCode::ERROR_FILE_NOT_OPENED_LOW_LEVEL;
		}

		SceKernelStat status;
		int res = sceKernelFstat(mFD, &status);
		if (res < 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't get the file size! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_GETTING_FILE_SIZE;
		}

		size = status.st_size;
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	/*************************************************************************************
		BerwickFileStorage implementation
	*************************************************************************************/

	BerwickFileStorage::BerwickFileStorage(const std::string mountPath)
		: mMountPoint(nullptr)
		, mDownload0MountPath(std::move(mountPath)) {
		initialize();
	}

	BerwickFileStorage::~BerwickFileStorage() {
	}

	bool BerwickFileStorage::fileExists(const char *szFilePath) {
		return sceFiosFileExistsSync(nullptr, szFilePath);
	}

	bool BerwickFileStorage::directoryExists(const char *szDirectoryPath) {
		return sceFiosDirectoryExistsSync(nullptr, szDirectoryPath);
	}

	bool BerwickFileStorage::fileOrDirectoryExists(const char *szPath) {
		return sceFiosExistsSync(nullptr, szPath);
	}

	::SFAT::ErrorCode BerwickFileStorage::deleteFile(const char *szFilePath) {
		int res = sceKernelUnlink(szFilePath);
		if (res != 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't delete file! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_DELETING_FILE_LOW_LEVEL;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorage::removeDirectory(const char *szDirectoryPath) {
		int res = sceKernelRmdir(szDirectoryPath);
		if (res != 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't remove \"%s\" directory! Error code #%8X", szDirectoryPath, res);
			return ::SFAT::ErrorCode::ERROR_REMOVING_DIRECTORY_LOW_LEVEL;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorage::createFileImpl(std::shared_ptr<::SFAT::FileBase>& fileImpl) {
		fileImpl = std::make_shared<BerwickFile>(*this);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorage::createDirectory(const char *szDirectoryPath) {
		int res = sceKernelMkdir(szDirectoryPath, SCE_KERNEL_S_IRWU);
		if (res != 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't create \"%s\" directory! Error code #%8X", szDirectoryPath, res);
			return ::SFAT::ErrorCode::ERROR_REMOVING_DIRECTORY_LOW_LEVEL;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorage::renameFile(const char *szFilePath, const char *szNewName) {
		int res = sceKernelRename(szFilePath, szNewName);
		if (res != 0) {
			ALOGE(LOG_AREA_PLATFORM, "Can't rename a file! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_RENAMING_FILE_LOW_LEVEL;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorage::renameDirectory(const char *szDirectoryPath, const char *szNewName) {
		int res = sceKernelRename(szDirectoryPath, szNewName);
		if (res != 0) {
			ALOGE(LOG_AREA_FILE, "Can't rename a directory! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_RENAMING_DIRECTORY_LOW_LEVEL;
		}
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickFileStorage::getFileSize(const char *szFilePath, ::SFAT::FileSizeType& fileSize) {
		SceFiosStat stat;
		int res = sceFiosStatSync(nullptr, szFilePath, &stat);
		if (res != SCE_FIOS_OK) {
			return ::SFAT::ErrorCode::ERROR_FILE_COULD_NOT_BE_FOUND;
		}
		if ((stat.statFlags & SCE_FIOS_STATUS_DIRECTORY) != 0) {
			return ::SFAT::ErrorCode::ERROR_FILE_COULD_NOT_BE_FOUND;
		}
		fileSize = static_cast<::SFAT::FileSizeType>(stat.fileSize);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	bool BerwickFileStorage::isFile(const char *szEntityPath) {
		SceFiosStat stat;
		int res = sceFiosStatSync(nullptr, szEntityPath, &stat);
		if (res != SCE_FIOS_OK) {
			return false;
		}
		return ((stat.statFlags & SCE_FIOS_STATUS_DIRECTORY) == 0);
	}

	bool BerwickFileStorage::isDirectory(const char *szEntityPath) {
		SceFiosStat stat;
		int res = sceFiosStatSync(nullptr, szEntityPath, &stat);
		if (res != SCE_FIOS_OK) {
			return false;
		}
		return ((stat.statFlags & SCE_FIOS_STATUS_DIRECTORY) != 0);
	}

	int BerwickFileStorage::initialize() {
		mMountPoint = std::make_unique<SceAppContentMountPoint>();
		strncpy(mMountPoint->data, mDownload0MountPath.c_str(), SCE_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE);

		//Check if download1 is available.
		SceKernelStat stat;
		int ret = sceKernelStat(mMountPoint->data, &stat);
		if (ret != SCE_OK) {
			//Please make sure to configure 'Storage setting(1)' in 'param.sfo' with 'orbis-pub-sfo.exe'
			//or 'param.sfo' is in the correct position.
			ALOGE(LOG_AREA_PLATFORM, "download0 is not available.");
		}
		else {
			ALOGI(LOG_AREA_PLATFORM, "download0 is available.");
		}

		//Get download data available space.
		size_t availableSpaceKb;
		ret = sceAppContentDownloadDataGetAvailableSpaceKb(mMountPoint.get(), &availableSpaceKb);
		if (ret != SCE_OK) {
			ALOGI(LOG_AREA_PLATFORM, "sceAppContentDownloadDataGetAvailableSpaceKb() error ret = [%x]\n", ret);
			return ret;
		}
		else {
			ALOGI(LOG_AREA_PLATFORM, "sceAppContentDownloadDataGetAvailableSpaceKb() available space kb = [%ld]\n", availableSpaceKb);
		}

		return 0;
	}

	const std::string BerwickFileStorage::getMountPath() const {
		return mDownload0MountPath;
	}

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

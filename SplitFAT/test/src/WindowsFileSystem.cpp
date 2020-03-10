/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "WindowsFileSystem.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include <stdio.h>
#include <errno.h>
#ifdef _WIN32
#	include <Shlwapi.h>
#	pragma comment(lib, "Shlwapi.lib") //Used for PathFileExistsA()
#endif

namespace SFAT {

	/*************************************************************************************
		WindowsFile implementation
	*************************************************************************************/

	WindowsFile::WindowsFile(WindowsFileStorage& fileStorage)
		: FileBase(fileStorage)
		, mFile(nullptr) {
	}

	WindowsFile::~WindowsFile() {
		if (isOpen()) {
			close();
		}
	}

	bool WindowsFile::isOpen() const {
		return (mFile != nullptr);
	}

	ErrorCode WindowsFile::open(const char *szFilePath, uint32_t accessMode) {
		(void)szFilePath; // Not used parameter
		(void)accessMode; // Not used parameter

		return ErrorCode::NOT_IMPLEMENTED; // This particular implementation is currently lower priority.
	}

	ErrorCode WindowsFile::open(const char *szFilePath, const char *szMode) {
		SFAT_ASSERT(!isOpen(), "File reopen is not supported!");

		mAccessMode = fileAccessStringToFlags(szMode);

		mFilePath = szFilePath;

		errno_t err = fopen_s(&mFile, szFilePath, szMode);
		if ((nullptr == mFile) || (err != 0)) {
			char errorMessage[50];
			strerror_s(errorMessage, err);
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Can't open file! Error message: %s", errorMessage);

			if (err == ENOENT) {
				return ErrorCode::ERROR_OPENING_FILE_NOT_FOUND;
			}

			return ErrorCode::ERROR_OPENING_FILE_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFile::close() {
		ErrorCode err = ErrorCode::RESULT_OK;
		if (mFile != nullptr) {
			int res = fclose(mFile);
			mFile = nullptr;
			if (res != 0) {
				err = ErrorCode::ERROR_CLOSING_FILE_LOW_LEVEL;
			}
		}
		else {
			SFAT_LOGW(LogArea::LA_PHYSICAL_DISK, "Trying to close a file that is not opened!");
		}

		return err;
	}

	ErrorCode WindowsFile::read(void* buffer, size_t sizeInBytes, size_t& sizeRead) {
#if (SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC == 1)
		SFATLockGuard guard(mReadWriteMutex);
#endif		
		sizeRead = fread(buffer, 1, sizeInBytes, mFile);
		int err = ferror(mFile);
		if (err != 0) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while reading!", err);
			return ErrorCode::ERROR_READING_LOW_LEVEL;
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFile::write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) {
#if (SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC == 1)
		SFATLockGuard guard(mReadWriteMutex);
#endif		
		sizeWritten = fwrite(buffer, 1, sizeInBytes, mFile);
		int err = ferror(mFile);
		if (err != 0) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X while writing!", err);
			return ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFile::seek(FilePositionType offset, SeekMode mode) {
#if (SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC == 1)
		SFATLockGuard guard(mReadWriteMutex);
#endif		
		int seekMode = SEEK_SET;
		if (mode == SeekMode::SM_CURRENT) {
			seekMode = SEEK_CUR;
		}
		else if (mode == SeekMode::SM_END) {
			seekMode = SEEK_END;
		}

		int res = _fseeki64(mFile, static_cast<__int64>(offset), seekMode);
		if (res != 0) {
			return ErrorCode::ERROR_POSITIONING_IN_FILE_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFile::flush() {
		if (!isOpen()) {
			return ErrorCode::RESULT_OK;
		}

#if (SPLITFAT_ENABLE_WINDOWS_READWRITE_SYNC == 1)
		SFATLockGuard guard(mReadWriteMutex);
#endif		

		int res = fflush(mFile);
		if (res != 0) {
			return ErrorCode::ERROR_FLUSH_LOW_LEVEL;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFile::getPosition(FilePositionType& position) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED_LOW_LEVEL;
		}

		fpos_t pos = 0;
		int res = fgetpos(mFile, &pos);
		if (res != 0) {
			return ErrorCode::ERROR_CAN_NOT_GET_FILE_POSITION;
		}

		position = static_cast<FilePositionType>(pos);
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFile::getSize(FileSizeType& size) {
		if (!isOpen()) {
			return ErrorCode::ERROR_FILE_NOT_OPENED;
		}

		// Read current file position
		FilePositionType currentPosition = 0;
		ErrorCode err = getPosition(currentPosition);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Move to the end of the file
		err = seek(0, SeekMode::SM_END);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// Read the file-end position
		FilePositionType fileEndPosition = 0;
		err = getPosition(fileEndPosition);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
		SFAT_ASSERT(fileEndPosition >= 0, "The position can't be negative!");
		size = static_cast<FileSizeType>(fileEndPosition);

		// Move to the original file position
		err = seek(currentPosition, SeekMode::SM_SET);

		return err;
	}

	/*************************************************************************************
		WindowsFileStorage implementation
	*************************************************************************************/

	bool WindowsFileStorage::fileExists(const char *szFilePath) {
		BOOL res = PathFileExistsA(szFilePath);
		return (res == TRUE);
	}

	bool WindowsFileStorage::directoryExists(const char *szDirectoryPath) {
		DWORD attribs = ::GetFileAttributesA(szDirectoryPath);
		if (attribs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}
		return (attribs & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool WindowsFileStorage::fileOrDirectoryExists(const char *szPath) {
		DWORD attribs = ::GetFileAttributesA(szPath);
		if (attribs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}
		return (attribs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE));
	}

	ErrorCode WindowsFileStorage::deleteFile(const char *szFilePath) {
		int res = remove(szFilePath);
		if (res != 0) {
			char buf[64];
			if (strerror_s(buf, sizeof(buf), errno) != 0)
			{
				snprintf(buf, sizeof(buf), "Unknown error:%u", errno);
			}
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "File remove error: %s", buf);
			return ErrorCode::ERROR_DELETING_FILE_LOW_LEVEL;
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFileStorage::removeDirectory(const char *szDirectoryPath) {
		(void)szDirectoryPath; // Not used parameter

		SFAT_ASSERT(false, "Implement!");
		return ErrorCode::NOT_IMPLEMENTED;
	}

	ErrorCode WindowsFileStorage::createFileImpl(std::shared_ptr<FileBase>& fileImpl) {
		fileImpl = std::make_shared<WindowsFile>(*this);
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFileStorage::createDirectory(const char *szDirectoryPath) {
		(void)szDirectoryPath; // Not used parameter

		SFAT_ASSERT(false, "Implement!");
		return ErrorCode::NOT_IMPLEMENTED;
	}

	ErrorCode WindowsFileStorage::getFileSize(const char *szFilePath, FileSizeType& fileSize) {
		fileSize = 0;
		struct stat stat_buf;
		int res = stat(szFilePath, &stat_buf);
		if (res != 0) {
			return ErrorCode::ERROR_GETTING_FILE_SIZE;
		}
		fileSize = static_cast<FileSizeType>(stat_buf.st_size);
		return ErrorCode::RESULT_OK;
	}

	bool WindowsFileStorage::isFile(const char *szEntityPath) {
		BOOL res = PathIsDirectoryA(szEntityPath);
		return (res == FALSE);
	}

	bool WindowsFileStorage::isDirectory(const char *szEntityPath) {
		BOOL res = PathIsDirectoryA(szEntityPath);
		return (res == TRUE);
	}

	ErrorCode WindowsFileStorage::renameFile(const char *szFilePath, const char *szNewName) {
		int res = rename(szFilePath, szNewName);
		if (res != 0) {
			return ErrorCode::ERROR_RENAMING_FILE_LOW_LEVEL;
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode WindowsFileStorage::renameDirectory(const char *szDirectoryPath, const char *szNewName) {
		int res = rename(szDirectoryPath, szNewName);
		if (res != 0) {
			return ErrorCode::ERROR_RENAMING_DIRECTORY_LOW_LEVEL;
		}
		return ErrorCode::RESULT_OK;
	}

} // namespace SFAT

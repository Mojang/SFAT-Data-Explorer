/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include <stdio.h>

namespace SFAT {

	/*************************************************************************************
		FileBase implementation
	*************************************************************************************/

	FileBase::FileBase(FileStorageBase& fileStorage)
		: mFileStorage(fileStorage)
		, mAccessMode(AccessMode::AM_UNSPECIFIED) {
	}

	FileBase::~FileBase() {
	}

	bool FileBase::isOpen() const {
		SFAT_ASSERT(false, "This code should not be reachable!");
		return false;
	}

	ErrorCode FileBase::open(const char *szFilePath, const char *szMode) {
		uint32_t accessMode = fileAccessStringToFlags(szMode);
		return open(szFilePath, accessMode);
	}
	
	ErrorCode FileBase::close() {
		SFAT_ASSERT(false, "This code should not be reachable!");
		return ErrorCode::ERROR_CLOSING_NOT_OPENED_LOW_LEVEL;
	}

	bool FileBase::checkAccessMode(uint32_t accessModeMask) const {
		return (mAccessMode & accessModeMask) == accessModeMask;
	}

	//static
	uint32_t FileBase::fileAccessStringToFlags(const char *szAccessMode) {
		if ((szAccessMode == nullptr) || (szAccessMode[0] == 0)) {
			return AM_UNSPECIFIED;
		}

		uint32_t mask = AM_UNSPECIFIED;

		// The mode string should start with one of 'r', 'w' or 'a'
		char ch = szAccessMode[0];
		if ((ch == 'r') || (ch == 'R')) {
			mask = AM_READ;
		} 
		else if ((ch == 'w') || (ch == 'W')) {
			mask = AM_WRITE | AM_TRUNCATE | AM_CREATE_IF_DOES_NOT_EXIST;
		}
		else if ((ch == 'a') || (ch == 'A')) {
			mask = AM_WRITE | AM_APPEND | AM_CREATE_IF_DOES_NOT_EXIST;
		}

		// Now read the modifiers
		for (int i = 1; i < 4; ++i) {
			ch = szAccessMode[i];
			if (ch == 0) {
				break;
			}
			else if (ch == '+') {
				mask |= AM_UPDATE;
			}
			else if ((ch == 'b') || (ch == 'B')) {
				mask |= AM_BINARY;
			}
			else if ((ch == 't') || (ch == 'T')) {
				mask |= AM_TEXT;
			}
			else if ((ch == 'x') || (ch == 'X')) {
				mask &= ~AM_CREATE_IF_DOES_NOT_EXIST;
			}
		}

		// If both flags present - 't' and 'b', the text mode is ignored.
		if (mask & AM_BINARY) {
			mask &= ~AM_TEXT;
		}

		if (mask & AM_UPDATE) {
			mask |= AM_WRITE | AM_READ;
		}

		return mask;
	}

	ErrorCode FileBase::readAtPosition(void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeRead) {
		ErrorCode err = seek(position, SeekMode::SM_SET);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return read(buffer, sizeInBytes, sizeRead);
	}

	ErrorCode FileBase::writeAtPosition(const void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeWritten) {
		ErrorCode err = seek(position, SeekMode::SM_SET);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		return write(buffer, sizeInBytes, sizeWritten);
	}


	/*************************************************************************************
		FileStorageBase implementation
	*************************************************************************************/

	ErrorCode FileStorageBase::openFile(FileHandle& fileHandle, const char *szFilePath, uint32_t accessMode) {
		ErrorCode err = createFileImpl(fileHandle.mFileImpl);

		if ((err != ErrorCode::RESULT_OK) || (fileHandle.mFileImpl == nullptr)) {
			SFAT_ASSERT(false, "The file-implementation object is not created!");
			return err;
		}

		return fileHandle.mFileImpl->open(szFilePath, accessMode);
	}

	ErrorCode FileStorageBase::openFile(FileHandle& fileHandle, const char *szFilePath, const char *szMode) {
		ErrorCode err = createFileImpl(fileHandle.mFileImpl);

		if ((err != ErrorCode::RESULT_OK) || (fileHandle.mFileImpl == nullptr)) {
			SFAT_ASSERT(false, "The file-implementation object is not created!");
			return err;
		}

		return fileHandle.mFileImpl->open(szFilePath, szMode);
	}


	/*************************************************************************************
		FileHandle implementation
	*************************************************************************************/

	bool FileHandle::isValid() const {
		return (mFileImpl != nullptr);
	}

	bool FileHandle::isOpen() const {
		if (!isValid()) {
			return false;
		}
		return mFileImpl->isOpen();
	}

	ErrorCode FileHandle::close() {
		if (isOpen()) {
			return mFileImpl->close();
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode FileHandle::read(void* buffer, size_t sizeInBytes, size_t& sizeRead) {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->read(buffer, sizeInBytes, sizeRead);
	}

	ErrorCode FileHandle::write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->write(buffer, sizeInBytes, sizeWritten);
	}

	ErrorCode FileHandle::readAtPosition(void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeRead) {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->readAtPosition(buffer, sizeInBytes, position, sizeRead);
	}

	ErrorCode FileHandle::writeAtPosition(const void* buffer, size_t sizeInBytes, FilePositionType position, size_t& sizeWritten) {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->writeAtPosition(buffer, sizeInBytes, position, sizeWritten);
	}

	ErrorCode FileHandle::seek(FilePositionType offset, SeekMode mode) {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->seek(offset, mode);
	}

	ErrorCode FileHandle::flush() {
		if (isOpen()) {
			return mFileImpl->flush();
		}
		return ErrorCode::RESULT_OK;
	}

	ErrorCode FileHandle::getPosition(FilePositionType& position) {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->getPosition(position);
	}

	bool FileHandle::checkAccessMode(uint32_t accessModeMask) const {
		SFAT_ASSERT(isValid(), "The file-handle is invalid!");
		return mFileImpl->checkAccessMode(accessModeMask);
	}

	ErrorCode FileHandle::reset() {
		if (mFileImpl != nullptr) {
			ErrorCode err = mFileImpl->close();
			mFileImpl = nullptr;
			return err;
		}
		return ErrorCode::RESULT_OK;
	}

} // namespace SFAT
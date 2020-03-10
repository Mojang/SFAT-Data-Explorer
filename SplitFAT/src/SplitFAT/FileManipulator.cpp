/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/FileManipulator.h"
#include "SplitFAT/AbstractFileSystem.h" //For the AccessMode enum

#include <string.h>

namespace SFAT {

	FileManipulator::FileManipulator()
		: mAccessMode(AccessMode::AM_UNSPECIFIED)
		, mPosition(0)
		, mPositionClusterIndex(ClusterValues::INVALID_VALUE)
		, mNextPosition(0)
		, mIsValid(false) {
		memset(&mFileDescriptorRecord, 0, sizeof(FileDescriptorRecord));
		mLocation.mDirectoryStartClusterIndex = ClusterValues::INVALID_VALUE;
		mLocation.mDescriptorClusterIndex = ClusterValues::INVALID_VALUE;
		mLocation.mRecordIndex = 0;
	}

	FileManipulator::FileManipulator(FileManipulator&& fm) {
		*this = std::move(fm);
	}

	FileManipulator& FileManipulator::operator=(FileManipulator&& fm) {
		mFileDescriptorRecord = std::move(fm.mFileDescriptorRecord);
		mLocation = std::move(fm.mLocation);

		mAccessMode = fm.mAccessMode;
		mPosition = fm.mPosition;
		mPositionClusterIndex = fm.mPositionClusterIndex;
		mNextPosition = fm.mNextPosition;

		mBuffer = std::move(fm.mBuffer);
		mFullPath = std::move(fm.mFullPath);

		mIsValid = fm.mIsValid;
		fm.mIsValid = false;

		return *this;
	}

	std::vector<uint8_t>& FileManipulator::getBuffer(size_t requiredMinSize) {
		if (mBuffer.size() < requiredMinSize) {
			mBuffer.resize(requiredMinSize);
		}
		return mBuffer;
	}

	bool FileManipulator::hasAccessMode(AccessMode mode) const {
		return (mAccessMode & static_cast<uint32_t>(mode)) != 0;
	}

	bool FileManipulator::isRootDirectory() const {
		return mFileDescriptorRecord.isDirectory() && (mLocation.mDescriptorClusterIndex == 0) && (mLocation.mRecordIndex == 0);
	}


} // namespace SFAT

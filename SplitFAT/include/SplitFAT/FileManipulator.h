/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/FileSystemConstants.h"
#include "SplitFAT/utils/PathString.h"
#include <vector>

namespace SFAT {

	class FileManipulator {
	public:
		FileManipulator();
		FileManipulator(const FileManipulator&) = delete;
		FileManipulator(FileManipulator&& fm);
		FileManipulator& operator=(const FileManipulator&) = delete;
		FileManipulator& operator=(FileManipulator&& fm);
		bool isValid() const { return mIsValid; }
		const FileDescriptorRecord& getFileDescriptorRecord() const { return mFileDescriptorRecord; }
		const DescriptorLocation& getDescriptorLocation() const { return mLocation; }
		const size_t getFileSize() const { return mFileDescriptorRecord.mFileSize; }
		const ClusterIndexType getStartCluster() const { return mFileDescriptorRecord.mStartCluster; }
		const ClusterIndexType getLastCluster() const { return mFileDescriptorRecord.mLastCluster; }
		std::vector<uint8_t>& getBuffer(size_t requiredMinSize);
		bool hasAccessMode(AccessMode mode) const;
		FilePositionType getPosition() const { return mNextPosition; }
		bool isRootDirectory() const;

	public:
		FileDescriptorRecord	mFileDescriptorRecord;		/// Descriptor of the file or directory (cached here)
		DescriptorLocation		mLocation;					/// Where in the parent directory the descriptor is located?
		PathString				mFullPath;					/// Full path to the file or directory

		// File access parameters
		uint32_t				mAccessMode;
		FilePositionType		mPosition;
		ClusterIndexType		mPositionClusterIndex;
		FilePositionType		mNextPosition;

		bool					mIsValid;
	private:
		std::vector<uint8_t>	mBuffer;
	};

} // namespace SFAT

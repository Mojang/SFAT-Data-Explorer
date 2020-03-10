/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include <string>
#include <time.h>

namespace SFAT {

	enum class FileDescriptorEnums : uint32_t {
		ENTITY_NAME_SIZE = 128,
	};

	enum class FileAttributes : uint32_t {
		FILE = 1,
		BINARY = 2,
		DELETED = 4,
		HIDDEN = 8,
	};

	struct DescriptorLocation {
		ClusterIndexType		mDirectoryStartClusterIndex;/// Parent-directory start cluster index
		ClusterIndexType		mDescriptorClusterIndex;	/// Cluster index from the parent-directory cluster chain where the descriptor is stored
		uint32_t				mRecordIndex;				/// Record index in the current directory
	};

	struct FileDescriptorRecord {
		bool isEmpty() const { return (mEntityName[0] == 0); }
		bool checkAttribute(FileAttributes attribute) const { return ((mAttributes & static_cast<uint32_t>(attribute)) != 0); }
		bool isDeleted() const { return checkAttribute(FileAttributes::DELETED); }
		bool isHidden() const { return checkAttribute(FileAttributes::HIDDEN); }
		bool isBinary() const { return checkAttribute(FileAttributes::BINARY); }
		bool isFile() const { return checkAttribute(FileAttributes::FILE); }
		bool isDirectory() const { return !isFile(); }
		bool isSameName(const std::string& name) const;

		char		mEntityName[static_cast<uint32_t>(FileDescriptorEnums::ENTITY_NAME_SIZE)];
		uint32_t	mAttributes; /// A combination of FileAttributes
		uint32_t	mUniqueID; // Unique entity ID. Not used yet.
		FileSizeType mFileSize;
		// The mStartCluster should contain ClusterValues::INVALID_VALUE if there is no cluster chain allocated for the file, the file-size is 0.
		// It should point to the first cluster of the cluster chain if the file-size is non zero.
		ClusterIndexType mStartCluster;
		uint32_t	mCRC;
		time_t		mTimeCreated;
		time_t		mTimeModified;

		// The intent is to use mLastCluster for optimization, integrity-test and recovery functionality.
		// The mLastCluster should contain ClusterValues::INVALID_VALUE if there is no cluster chain allocated for the file, the file-size is 0.
		// It should point to the last cluster of the cluster chain if the file-size is non zero.
		ClusterIndexType mLastCluster;

		// To be used for debugging. Note that this doesn't change the layout of the file-descriptor-record as it is still in the range of 256 bytes per record.
		ClusterIndexType mOldClusterTrace;
	};

} // namespace SFAT

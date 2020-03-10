/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include "SplitFAT/VirtualFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/FileManipulator.h"
#include "SplitFAT/utils/PathString.h"
#include "WindowsSplitFATConfiguration.h"
#include <memory>

namespace {
	const char* kVolumeControlAndFATDataFilePath = "SFATControl.dat";
	const char* kClusterDataFilePath = "data.dat";
	const char* kTransactionFilePath = "_SFATTransaction.dat";
}

using namespace SFAT;

class VirtualFileSystemTests : public testing::Test {
protected:  // You should make the members protected s.t. they can be
			// accessed from sub-classes.

	// virtual void SetUp() will be called before each test is run.  You
	// should define it if you need to initialize the variables.
	// Otherwise, this can be skipped.
	virtual void SetUp() override {
	}

	// virtual void TearDown() will be called after each test is run.
	// You should define it if there is cleanup work to do.  Otherwise,
	// you don't have to provide it.
	//
	virtual void TearDown() override {
	}

	void removeVolume() {
		{
			VolumeManager volumeManager;
			createVolume(volumeManager);
			volumeManager.removeVolume();

			EXPECT_FALSE(volumeManager.clusterDataFileExists());
			EXPECT_FALSE(volumeManager.fatDataFileExists());
		}
	}

	void createVolume(VolumeManager& volumeManager) {
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		ErrorCode err = lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		err = volumeManager.setup(lowLevelFileAccess);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	void createVirtualFileSystem(VirtualFileSystem& vfs) {
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		ErrorCode err = lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		err = vfs.setup(lowLevelFileAccess);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	void testIntegrityOfSequentialWrittenFile(FileManipulator &fileFM, VirtualFileSystem& vfs);
};

/// Tests the VirtualFileSystem instance creation. It should create an empty virtual disk.
TEST_F(VirtualFileSystemTests, Creation) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());

		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		FATCellValueType value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain());
		EXPECT_TRUE(value.isStartOfChain());
	}
}

/// Tests the appending of a new allocated cluster to an existing chain (the root directory, with currently only cluster 0 allocated).
TEST_F(VirtualFileSystemTests, AppendClusterToChain) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		// Append cluster to Root
		//----------------------------------------------------------------------------
		// Expect initialy the Root directory to occupy a single cluster with index 0 (ClusterValues::ROOT_START_CLUSTER_INDEX)
		FATCellValueType value = FATCellValueType::badCellValue();
		ErrorCode err = volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain());
		EXPECT_TRUE(value.isStartOfChain());
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
		uint32_t recordIndex = ~0UL;
		value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
		EXPECT_EQ(descriptorClusterIndex, 0); // For the root directory the descriptorClusterIndex should be 0
		EXPECT_EQ(recordIndex, 0); // For the root directory the recordIndex should be 0

		DescriptorLocation location;
		location.mDescriptorClusterIndex = 0;
		location.mDirectoryStartClusterIndex = 0;
		location.mRecordIndex = 0;
		ClusterIndexType newAllocatedCluster = ClusterValues::INVALID_VALUE;
		err = vfs._appendClusterToEndOfChain(location, ClusterValues::ROOT_START_CLUSTER_INDEX, newAllocatedCluster, false);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(newAllocatedCluster, 1);

		value = FATCellValueType::badCellValue();
		err = volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_EQ(value.getNext(), 1); // Should now point the the next cluster, where newAllocatedCluster = 1
		EXPECT_TRUE(value.isStartOfChain());
		EXPECT_FALSE(value.isEndOfChain());
		// Because it is start cluster, the cellValue should still have encoded the FileDescriptorRecord location
		value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
		EXPECT_EQ(descriptorClusterIndex, 0); // For the root directory the descriptorClusterIndex should be 0
		EXPECT_EQ(recordIndex, 0); // For the root directory the recordIndex should be 0

		value = FATCellValueType::badCellValue();
		err = volumeManager.getFATCell(newAllocatedCluster, value);
		EXPECT_EQ((value.getRawNext() & ClusterValues::CHAIN_START_END_MASK), ClusterValues::END_OF_CHAIN); // The new allocated cluster is now the end of the chain.
		EXPECT_EQ(value.getPrev(), 0);
		EXPECT_FALSE(value.isStartOfChain());
		EXPECT_TRUE(value.isEndOfChain());
		// The cellValue for the last cluster should have encoded the FileDescriptorRecord location as well
		value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
		EXPECT_EQ(descriptorClusterIndex, 0); // For the root directory the descriptorClusterIndex should be 0
		EXPECT_EQ(recordIndex, 0); // For the root directory the recordIndex should be 0

		vfs._printClusterChain(ClusterValues::ROOT_START_CLUSTER_INDEX);
	}
}

/// Tests the appending of a new allocated cluster to an existing chain (the root directory, with currently only cluster 0 allocated).
TEST_F(VirtualFileSystemTests, CreateANewClusterChain) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		// Append cluster to Root
		//----------------------------------------------------------------------------
		// Expect initialy the Root directory to occupy a single cluster with index 0 (ClusterValues::ROOT_START_CLUSTER_INDEX)
		FATCellValueType value = FATCellValueType::badCellValue();
		
		DescriptorLocation location;
		// We will use a fake location, but check if it will be encoded correctly in the first and last cluster cellValues.
		location.mDescriptorClusterIndex = 53;
		location.mDirectoryStartClusterIndex = 52;
		location.mRecordIndex = 5;
		
		// Add the first cluster. Expected position is clusterIndex = 1
		{
			ErrorCode err = volumeManager.getFATCell(1, value);
			EXPECT_TRUE(value.isFreeCluster());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			ClusterIndexType newAllocatedCluster = ClusterValues::INVALID_VALUE;
			err = vfs._appendClusterToEndOfChain(location, ClusterValues::INVALID_VALUE, newAllocatedCluster, false); ///Creates new cluster chain.
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newAllocatedCluster, 1);

			value = FATCellValueType::badCellValue();
			err = volumeManager.getFATCell(1, value);
			EXPECT_TRUE(value.isEndOfChain());
			EXPECT_TRUE(value.isStartOfChain());
			ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			uint32_t recordIndex = ~0UL;
			value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(descriptorClusterIndex, 53);
			EXPECT_EQ(recordIndex, 5);
		}

		// Add the second cluster. Expected position is clusterIndex = 2
		{
			ErrorCode err = volumeManager.getFATCell(2, value);
			EXPECT_TRUE(value.isFreeCluster());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			ClusterIndexType newAllocatedCluster = ClusterValues::INVALID_VALUE;
			err = vfs._appendClusterToEndOfChain(location, 1, newAllocatedCluster, false);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newAllocatedCluster, 2);

			value = FATCellValueType::badCellValue();
			err = volumeManager.getFATCell(1, value);
			EXPECT_EQ(value.getNext(), 2); // Should now point the next cluster, where newAllocatedCluster = 2
			EXPECT_TRUE(value.isStartOfChain()); // Should say that it is the start of the chain
			ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			uint32_t recordIndex = ~0UL;
			value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(descriptorClusterIndex, 53);
			EXPECT_EQ(recordIndex, 5);

			value = FATCellValueType::badCellValue();
			err = volumeManager.getFATCell(2, value);
			EXPECT_TRUE(value.isEndOfChain()); // Should be the end of chain
			EXPECT_EQ(value.getPrev(), 1); // Should now point the previous cluster - 1
			descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			recordIndex = ~0UL;
			value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(descriptorClusterIndex, 53);
			EXPECT_EQ(recordIndex, 5);
		}
	}

	/// Now close and reopen the volume and check everything again
	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		FATCellValueType value = FATCellValueType::badCellValue();
		ErrorCode err = volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain()); // This is the Root, and it should be End-of-chain
		EXPECT_TRUE(value.isStartOfChain()); // Should say that it is the start of the chain

		value = FATCellValueType::badCellValue();
		err = volumeManager.getFATCell(1, value);
		EXPECT_EQ(value.getNext(), 2); // Should now point the the next cluste, where newAllocatedCluster = 2
		EXPECT_TRUE(value.isStartOfChain()); // Should say that it is the start of the chain
		EXPECT_FALSE(value.isEndOfChain()); // It is NOT the End-of-chain
		ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
		uint32_t recordIndex = ~0UL;
		value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
		EXPECT_EQ(descriptorClusterIndex, 53);
		EXPECT_EQ(recordIndex, 5);

		value = FATCellValueType::badCellValue();
		err = volumeManager.getFATCell(2, value);
		EXPECT_FALSE(value.isStartOfChain()); // It is NOT the Start-of-chain
		EXPECT_TRUE(value.isEndOfChain()); // Should now point the the next cluste, where newAllocatedCluster = 2
		EXPECT_EQ(value.getPrev(), 1); // Should now point the previous cluster - 1
		descriptorClusterIndex = ClusterValues::INVALID_VALUE;
		recordIndex = ~0UL;
		value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
		EXPECT_EQ(descriptorClusterIndex, 53);
		EXPECT_EQ(recordIndex, 5);

		vfs._printClusterChain(1);
		vfs._printClusterChain(ClusterValues::ROOT_START_CLUSTER_INDEX);
	}

}

void printFileDescriptorRecord(const FileDescriptorRecord& record, uint32_t recordIndex) {
	if (!record.isEmpty()) {
		printf("#%3u Name: \"%s\" size:%u cluster:%u flags:%4X ", recordIndex, record.mEntityName, static_cast<uint32_t>(record.mFileSize), record.mStartCluster, record.mAttributes);
	}
	else {
		printf("#%3u Empty", recordIndex);
	}
	printf("\n");
}

TEST_F(VirtualFileSystemTests, WritingFileDescriptorRecord) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator fm;
		fm.mAccessMode = AM_BINARY | AM_READ | AM_WRITE;

		fm.mFileDescriptorRecord.mAttributes = static_cast<uint32_t>(FileAttributes::BINARY) | static_cast<uint32_t>(FileAttributes::FILE);
		strcpy_s(fm.mFileDescriptorRecord.mEntityName, sizeof(FileDescriptorRecord::mEntityName), "my_first_file.bin");
		fm.mFileDescriptorRecord.mFileSize = 123;
		fm.mFileDescriptorRecord.mStartCluster = 55;
		fm.mFileDescriptorRecord.mLastCluster = 44;
		fm.mFileDescriptorRecord.mUniqueID = 0;
		fm.mFileDescriptorRecord.mCRC = 0x12345678;
		time_t localTime = time(0);
		fm.mFileDescriptorRecord.mTimeCreated = localTime;
		fm.mFileDescriptorRecord.mTimeModified = localTime;

		fm.mPosition = 0; //Irrelevant position in the file //////////volumeManager.getFileDescriptorRecordStorageSize() * recordIndex;
		fm.mNextPosition = fm.mPosition; // No planed movement
		fm.mPositionClusterIndex = fm.mFileDescriptorRecord.mStartCluster; //Irrelevant

		fm.mLocation.mDescriptorClusterIndex = ClusterValues::ROOT_START_CLUSTER_INDEX;
		fm.mLocation.mDirectoryStartClusterIndex = ClusterValues::ROOT_START_CLUSTER_INDEX;
		fm.mLocation.mRecordIndex = 0;

		ErrorCode err = vfs._writeFileDescriptor(fm);


		//vfs._iterateThroughFileDescriptorRecords(0, 0, [](bool& doQuit, FileDescriptorRecord& record, uint32_t recordIndex)->ErrorCode {
		//	printFileDescriptorRecord(record, recordIndex);
		//	return ErrorCode::RESULT_OK;
		//});

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		vfs._createRootDirFileManipulator(rootFM);
		vfs._iterateThroughDirectory(rootFM, [&fm, &recordIndex](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			if (recordIndex == 0) {
				printFileDescriptorRecord(record, recordIndex);
				EXPECT_STRCASEEQ(record.mEntityName, fm.mFileDescriptorRecord.mEntityName);
				EXPECT_EQ(record.mAttributes, fm.mFileDescriptorRecord.mAttributes);
				EXPECT_EQ(record.mFileSize, fm.mFileDescriptorRecord.mFileSize);
				EXPECT_EQ(record.mStartCluster, fm.mFileDescriptorRecord.mStartCluster);
				EXPECT_EQ(record.mLastCluster, fm.mFileDescriptorRecord.mLastCluster);
				EXPECT_EQ(record.mUniqueID, fm.mFileDescriptorRecord.mUniqueID);
				EXPECT_EQ(record.mCRC, fm.mFileDescriptorRecord.mCRC);
			}
			else {
				if (recordIndex == 1) {
					printFileDescriptorRecord(record, recordIndex);
				}
				EXPECT_TRUE(record.isEmpty());
			}
			++recordIndex;

			return ErrorCode::RESULT_OK;
		});

		EXPECT_EQ(recordIndex, 32);
	}
}


TEST_F(VirtualFileSystemTests, CreateFile) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		vfs._createRootDirFileManipulator(rootFM);

		{
			FileManipulator fileFM;
			vfs.createFile("SecondFile.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
		}

		{
			FileManipulator fileFM;
			//vfs.createDirectory(rootFM, "MyFirstDirectory", 2, fileFM);
			vfs.createDirectory("MyFirstDirectory", fileFM);
		}

		vfs._iterateThroughDirectory(rootFM, [&recordIndex](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			if (recordIndex == 0) {
				printFileDescriptorRecord(record, recordIndex);
				EXPECT_STRCASEEQ(record.mEntityName, "secondfile.bin");
				EXPECT_EQ(record.mAttributes, static_cast<uint32_t>(FileAttributes::BINARY) | static_cast<uint32_t>(FileAttributes::FILE));
				EXPECT_EQ(record.mFileSize, 0);
				EXPECT_EQ(record.mStartCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
				EXPECT_EQ(record.mLastCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
				//EXPECT_EQ(record.mUniqueID, 2);
				//EXPECT_EQ(record.mCRC, 0);
			}
			else if (recordIndex == 1) {
				printFileDescriptorRecord(record, recordIndex);
				EXPECT_STRCASEEQ(record.mEntityName, "myfirstdirectory");
				EXPECT_EQ(record.mAttributes, static_cast<uint32_t>(FileAttributes::BINARY));
				EXPECT_EQ(record.mFileSize, 0);
				EXPECT_EQ(record.mStartCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
				EXPECT_EQ(record.mLastCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
				//EXPECT_EQ(record.mUniqueID, 2);
				//EXPECT_EQ(record.mCRC, 0);
			}
			else {
				if (recordIndex == 2) {
					printFileDescriptorRecord(record, recordIndex);
				}
				EXPECT_TRUE(record.isEmpty());
			}
			++recordIndex;

			return ErrorCode::RESULT_OK;
		});

		// Try creating a file using already existing name
		{
			FileManipulator fileFM;
			ErrorCode err = vfs.createFile("myFIRSTdirectory", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::ERROR_FILE_OR_DIRECTORY_WITH_SAME_NAME_ALREADY_EXISTS);
		}
	}
}

TEST_F(VirtualFileSystemTests, LastClusterUpdateCreatingSeveralClustersBigFile) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;
		const uint32_t recordsPerCluster = volumeManager.getClusterSize() / volumeManager.getFileDescriptorRecordStorageSize();

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		vfs._createRootDirFileManipulator(rootFM);

		FileManipulator dirFM;
		ErrorCode err = vfs.createDirectory("subdir", dirFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Create several empty files (for example 4 of them) to allocate some FileDescriptorRecords in the beginning of the directory.
		// Thus we will avoid having location 0 record for the file we will test further.
		const char* szFilePathFormat = "/subdir/file%04u.bin";
		char fileNameBuffer[50];
		for (int i = 0; i < 4; ++i) {
			FileManipulator localFileFM;
			snprintf(fileNameBuffer, sizeof(fileNameBuffer), szFilePathFormat, i);
			err = vfs.createFile(fileNameBuffer, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, localFileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		// Now create the test file
		FileManipulator fileFM;
		err = vfs.createFile("/subdir/the_test_file.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		const uint32_t countClustersToWrite = 3;
		const size_t bufferSize = countClustersToWrite * volumeManager.getClusterSize();
		std::vector<uint8_t> buffer(bufferSize, 0xA5);
		size_t bytesWritten = 0;
		err = vfs.write(fileFM, buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		ClusterIndexType startClusterIndex = fileFM.getStartCluster();
		uint32_t countClustersAllocated = 0;
		ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
		err = vfs._getCountClusters(startClusterIndex, countClustersAllocated, lastClusterIndex);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(countClustersAllocated, countClustersToWrite);
		EXPECT_EQ(startClusterIndex + countClustersAllocated - 1, lastClusterIndex);

		FATCellValueType firstCellValue;
		FATCellValueType lastCellValue;
		uint32_t counter = 0;
		err = vfs._iterateThroughClusterChain(startClusterIndex,
			[&counter, &fileFM, countClustersAllocated, startClusterIndex, lastClusterIndex, recordsPerCluster](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
			if (counter == 0) {
				EXPECT_EQ(startClusterIndex, currentCluster);
				ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
				uint32_t recordIndex = ~0UL;
				cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
				EXPECT_EQ(fileFM.getDescriptorLocation().mDescriptorClusterIndex, descriptorClusterIndex);
				EXPECT_EQ(fileFM.getDescriptorLocation().mRecordIndex % recordsPerCluster, recordIndex);
			}
			if (counter + 1 == countClustersAllocated) {
				EXPECT_EQ(lastClusterIndex, currentCluster);
				ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
				uint32_t recordIndex = ~0UL;
				cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
				EXPECT_EQ(fileFM.getDescriptorLocation().mDescriptorClusterIndex, descriptorClusterIndex);
				EXPECT_EQ(fileFM.getDescriptorLocation().mRecordIndex % recordsPerCluster, recordIndex);
			}
			++counter;
			return ErrorCode::RESULT_OK;
		}
		);
		EXPECT_EQ(counter, countClustersToWrite);
	}
}

TEST_F(VirtualFileSystemTests, CreateFileManipulatorForExistingFile) {
	
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		vfs._createRootDirFileManipulator(rootFM);


		{
			FileManipulator fileFM;
			ErrorCode err = vfs.createFile("/file_to_expand.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			uint32_t countClusters;
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 0);
			EXPECT_EQ(lastClusterIndex, ClusterValues::INVALID_VALUE);
			err = vfs._expandFile(fileFM, 123);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 1);
			// We have only one cluster, so the start is the same as the end.
			EXPECT_EQ(lastClusterIndex, fileFM.getStartCluster());
			// Check if the mLastCluster is properly updated
			EXPECT_EQ(fileFM.getFileDescriptorRecord().mLastCluster, lastClusterIndex);
		}

		{
			FileManipulator newFileFM;
			ErrorCode err = vfs.createGenericFileManipulatorForFilePath(PathString("file_to_expand.bin"), newFileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_TRUE(newFileFM.isValid());
			EXPECT_EQ(newFileFM.getFileSize(), 123);
			newFileFM.mAccessMode |= AccessMode::AM_WRITE; // Give manually a write-access

			uint32_t countClusters;
			err = vfs._expandFile(newFileFM, vfs._getClusterSize() - 1);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newFileFM.getFileSize(), vfs._getClusterSize() - 1);
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			vfs._getCountClusters(newFileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 1);
		}
	}
}

TEST_F(VirtualFileSystemTests, ExpandFile) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		vfs._createRootDirFileManipulator(rootFM);


		{
			FileManipulator fileFM;
			ErrorCode err = vfs.createFile("file_to_expand.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			vfs._expandFile(fileFM, 100);
		}

		ClusterIndexType firstFileDataCluster = volumeManager.getFirstFileDataClusterIndex();
		vfs._iterateThroughDirectory(rootFM, [firstFileDataCluster, &recordIndex](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			if (recordIndex == 0) {
				printFileDescriptorRecord(record, recordIndex);
				EXPECT_STRCASEEQ(record.mEntityName, "file_to_expand.bin");
				EXPECT_EQ(record.mAttributes, static_cast<uint32_t>(FileAttributes::BINARY) | static_cast<uint32_t>(FileAttributes::FILE));
				EXPECT_EQ(record.mFileSize, 100);
				EXPECT_EQ(record.mStartCluster, firstFileDataCluster); // Cluster index 1 is assigned
				EXPECT_EQ(record.mLastCluster, firstFileDataCluster); // Cluster index 1 is the last cluster as well
				EXPECT_EQ(record.mUniqueID, 0);
				//EXPECT_EQ(record.mCRC, 0);

				doQuit = true;
			}
			++recordIndex;

			return ErrorCode::RESULT_OK;
		});
	}
}

TEST_F(VirtualFileSystemTests, ExpandExistingFile) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		vfs._createRootDirFileManipulator(rootFM);

		{
			FileManipulator fileFM;
			ErrorCode err = vfs.createFile("file_to_expand.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			uint32_t countClusters;
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 0);
			EXPECT_EQ(lastClusterIndex, ClusterValues::INVALID_VALUE);
			err = vfs._expandFile(fileFM, 123);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 1);
			// We have only one cluster, so the start is the same as the end.
			EXPECT_EQ(lastClusterIndex, fileFM.getStartCluster());
			// Check if the mLastCluster is properly updated
			EXPECT_EQ(fileFM.getFileDescriptorRecord().mLastCluster, lastClusterIndex);
		}

		{
			FileManipulator newFileFM;
			uint32_t countClusters = 0;
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			ErrorCode err = vfs.createGenericFileManipulatorForFilePath(PathString("file_to_expand.bin"), newFileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_TRUE(newFileFM.isValid());
			EXPECT_EQ(newFileFM.getFileSize(), 123);
			newFileFM.mAccessMode |= AccessMode::AM_WRITE; // Give manually a write-access

			err = vfs._expandFile(newFileFM, vfs._getClusterSize());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newFileFM.getFileSize(), vfs._getClusterSize());
			vfs._getCountClusters(newFileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 1);

			err = vfs._expandFile(newFileFM, vfs._getClusterSize()+1);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newFileFM.getFileSize(), vfs._getClusterSize()+1);
			vfs._getCountClusters(newFileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 2);
			// Check if the mLastCluster is properly updated
			EXPECT_EQ(newFileFM.getFileDescriptorRecord().mLastCluster, lastClusterIndex);

			size_t newSize = 4 * (1 << 20); // To allocate 4M bytes
			uint32_t expectedClusterCount = vfs.getCountClustersForSize(newSize);
			err = vfs._expandFile(newFileFM, newSize);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newFileFM.getFileSize(), newSize);
			vfs._getCountClusters(newFileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, expectedClusterCount);
			EXPECT_EQ(newFileFM.getFileDescriptorRecord().mLastCluster, lastClusterIndex);

			newSize = 4 * (1 << 20) + 1; // To allocate 4M + 1 bytes
			expectedClusterCount = vfs.getCountClustersForSize(newSize);
			err = vfs._expandFile(newFileFM, newSize);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(newFileFM.getFileSize(), newSize);
			vfs._getCountClusters(newFileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, expectedClusterCount);
			EXPECT_EQ(newFileFM.getFileDescriptorRecord().mLastCluster, lastClusterIndex);
		}
	}
}


TEST_F(VirtualFileSystemTests, CreateSubdirectory) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		FileManipulator rootFM;
		uint32_t recordIndex = 0;
		ErrorCode err = vfs._createRootDirFileManipulator(rootFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Create sub-directory in root.
		FileManipulator subdirFM;
		err = vfs.createDirectory("subdirectory0", subdirFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		{
			FileManipulator secondSubdirFM;
			err = vfs.createDirectory("/subdirectory1", secondSubdirFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			vfs._iterateThroughDirectory(rootFM, [&recordIndex](bool& doQuit, const DescriptorLocation& location, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
				if (recordIndex == 0) {
					printFileDescriptorRecord(record, recordIndex);
					EXPECT_STRCASEEQ(record.mEntityName, "subdirectory0");
					EXPECT_EQ(record.mAttributes, static_cast<uint32_t>(FileAttributes::BINARY));
					EXPECT_EQ(record.mFileSize, 0);
					EXPECT_EQ(record.mStartCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
					EXPECT_EQ(record.mLastCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
					//EXPECT_EQ(record.mUniqueID, mUniqueID);
				}
				else if (recordIndex == 1) {
					printFileDescriptorRecord(record, recordIndex);
					EXPECT_STRCASEEQ(record.mEntityName, "subdirectory1");
					EXPECT_EQ(record.mAttributes, static_cast<uint32_t>(FileAttributes::BINARY));
					EXPECT_EQ(record.mFileSize, 0);
					EXPECT_EQ(record.mStartCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
					EXPECT_EQ(record.mLastCluster, ClusterValues::INVALID_VALUE); // No cluster assigned
					//EXPECT_EQ(record.mUniqueID, mUniqueID);
				}
				else {
					if (recordIndex == 2) {
						printFileDescriptorRecord(record, recordIndex);
					}
					EXPECT_TRUE(record.isEmpty());
				}
				++recordIndex;

				return ErrorCode::RESULT_OK;
			});
		}

		{
			PathString path("/subdirectory1/second_level_subdir");
			bool bRes = vfs.directoryExists(path);
			EXPECT_FALSE(bRes);

			FileManipulator secondLevelSubdirFM;
			err = vfs.createDirectory(path, secondLevelSubdirFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			bRes = vfs.directoryExists(path);
			EXPECT_TRUE(bRes);
		}
	}
}

TEST_F(VirtualFileSystemTests, FileExists) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		{
			FileManipulator secondLevelSubdirFM;
			ErrorCode err = vfs.createDirectory("/some_dir", secondLevelSubdirFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		bool bRes = vfs.directoryExists("/some_dir");
		EXPECT_TRUE(bRes);

		bRes = vfs.fileExists("/some_dir/a_file.bin");
		EXPECT_FALSE(bRes);

		{
			FileManipulator fileFM;
			ErrorCode err = vfs.createFile("/some_dir/a_file.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		bRes = vfs.fileExists("/some_dir/a_file.bin");
		EXPECT_TRUE(bRes);
	}
}

TEST_F(VirtualFileSystemTests, FileOrDirectoryExists) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		bool bRes = vfs.fileOrDirectoryExists("/some_dir");
		EXPECT_FALSE(bRes);

		{
			FileManipulator secondLevelSubdirFM;
			ErrorCode err = vfs.createDirectory("/some_dir", secondLevelSubdirFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		bRes = vfs.fileOrDirectoryExists("/some_dir");
		EXPECT_TRUE(bRes);

		bRes = vfs.fileOrDirectoryExists("/some_dir/a_file.bin");
		EXPECT_FALSE(bRes);

		{
			FileManipulator fileFM;
			ErrorCode err = vfs.createFile("/some_dir/a_file.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		bRes = vfs.fileOrDirectoryExists("/some_dir/a_file.bin");
		EXPECT_TRUE(bRes);
	}
}


TEST_F(VirtualFileSystemTests, isDirectoryEmpty) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		const char* szDirFullPath = "/some_dir";
		const char* szFileFullPath = "/some_dir/a_file.bin";

		bool bRes = vfs.directoryExists(szDirFullPath);
		EXPECT_FALSE(bRes);

		bRes = vfs.directoryExists(szDirFullPath);

		// Create a directory
		{
			FileManipulator directoryFM;
			ErrorCode err = vfs.createDirectory(szDirFullPath, directoryFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		// The directory should be empty
		{
			FileManipulator directoryFM;
			ErrorCode err = vfs._createFileManipulatorForDirectoryPath(szDirFullPath, directoryFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			bRes = false;
			err = vfs._isDirectoryEmpty(directoryFM, bRes);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_TRUE(bRes);
		}

		FileManipulator fileFM;
		ErrorCode err = vfs.createFile(szFileFullPath, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		bRes = vfs.fileExists(szFileFullPath);
		EXPECT_TRUE(bRes);

		// The directory should NOT be empty (has 1 element)
		{
			FileManipulator directoryFM;
			ErrorCode err = vfs._createFileManipulatorForDirectoryPath(szDirFullPath, directoryFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			bRes = false;
			err = vfs._isDirectoryEmpty(directoryFM, bRes);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_FALSE(bRes);
		}

		err = vfs.deleteFile(szFileFullPath);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// The directory should be empty again
		{
			FileManipulator directoryFM;
			ErrorCode err = vfs._createFileManipulatorForDirectoryPath(szDirFullPath, directoryFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			bRes = false;
			err = vfs._isDirectoryEmpty(directoryFM, bRes);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_TRUE(bRes);
		}
	}
}

TEST_F(VirtualFileSystemTests, FileDescriptorRecordFromFirstFileClusterIndex) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		const char* szFilePathFormat = "/file%04u.bin";

		VolumeManager &volumeManager = vfs.mVolumeManager;
		uint32_t recordsPerCluster = volumeManager.getClusterSize() / volumeManager.getFileDescriptorRecordStorageSize();
		uint32_t countFilesToCreate = 1 + recordsPerCluster;
		char filePath[50];
		for (uint32_t i = 0; i < recordsPerCluster; ++i) {
			FileManipulator fileFM;
			snprintf(filePath, sizeof(filePath), szFilePathFormat, i);
			ErrorCode err = vfs.createFile(filePath, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			uint32_t countClusters;
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 0);
			EXPECT_EQ(lastClusterIndex, ClusterValues::INVALID_VALUE);
			err = vfs._expandFile(fileFM, 123);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 1);
			EXPECT_EQ(lastClusterIndex, fileFM.getFileDescriptorRecord().mLastCluster);

			ClusterIndexType cellIndex = fileFM.getStartCluster();
			FATCellValueType cellValue = FATCellValueType::invalidCellValue();
			err = volumeManager.getFATCell(cellIndex, cellValue);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			uint32_t recordIndex = ~0UL;
			cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(descriptorClusterIndex, fileFM.getDescriptorLocation().mDescriptorClusterIndex);
			EXPECT_EQ(recordIndex, fileFM.getDescriptorLocation().mRecordIndex % recordsPerCluster);
		}

		for (uint32_t i = recordsPerCluster; i < countFilesToCreate; ++i) {
			FileManipulator fileFM;
			snprintf(filePath, sizeof(filePath), szFilePathFormat, i);
			ErrorCode err = vfs.createFile(filePath, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);

			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			uint32_t countClusters;
			ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 0);
			err = vfs._expandFile(fileFM, 123);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			vfs._getCountClusters(fileFM.getStartCluster(), countClusters, lastClusterIndex);
			EXPECT_EQ(countClusters, 1);

			ClusterIndexType cellIndex = fileFM.getStartCluster();
			FATCellValueType cellValue = FATCellValueType::invalidCellValue();
			err = volumeManager.getFATCell(cellIndex, cellValue);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			uint32_t recordIndex = ~0UL;
			cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(descriptorClusterIndex, fileFM.getDescriptorLocation().mDescriptorClusterIndex);
			EXPECT_EQ(recordIndex, fileFM.getDescriptorLocation().mRecordIndex % recordsPerCluster);
		}

	}
}


TEST_F(VirtualFileSystemTests, ForwardAndBackwardClusterChainPropagation) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);
		VolumeManager &volumeManager = vfs.mVolumeManager;
		ErrorCode err;
		const char* szFilePathFormat = "/file%04u.bin";

		// Create a buffer and fill it with data
		const size_t totalBufferSize = 16 << 20;
		std::vector<uint8_t> buffer;
		buffer.resize(totalBufferSize);
		for (size_t i = 0; i < totalBufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(rand());
		}

		uint32_t recordsPerCluster = volumeManager.getClusterSize() / volumeManager.getFileDescriptorRecordStorageSize();
		uint32_t countFilesToCreate = 1 + recordsPerCluster;
		char filePath[50];
		std::vector<FileManipulator> fileManipulators;
		fileManipulators.resize(countFilesToCreate);
		
		for (uint32_t i = 0; i < countFilesToCreate; ++i) {
			FileManipulator fileFM;
			snprintf(filePath, sizeof(filePath), szFilePathFormat, i);
			err = vfs.createFile(filePath, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileManipulators[i]);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		// Write into many files in mixed otder
		size_t bufferPos = 0;
		size_t sizeToWrite = 2 * volumeManager.getClusterSize();
		do {
			for (uint32_t i = 0; i < countFilesToCreate; ++i) {
				if (bufferPos + sizeToWrite > totalBufferSize) {
					break;
				}
				size_t sizeWritten;
				err = vfs.write(fileManipulators[i], &buffer[bufferPos], sizeToWrite, sizeWritten);
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				bufferPos += sizeWritten;
			}
		} while (bufferPos + sizeToWrite <= totalBufferSize);

		for (uint32_t i = 0; i < countFilesToCreate; ++i) {
			snprintf(filePath, sizeof(filePath), szFilePathFormat, i);
			err = vfs.flush(fileManipulators[i]);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		for (uint32_t i = 0; i < countFilesToCreate; ++i) {
			ClusterIndexType startClusterIndex = fileManipulators[i].getStartCluster();
			if (SFAT::isValidClusterIndex(startClusterIndex)) {
				FATCellValueType cellValue;
				err = volumeManager.getFATCell(startClusterIndex, cellValue);
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				EXPECT_TRUE(cellValue.isStartOfChain());
				ClusterIndexType descriptorClusterIndex;
				uint32_t recordIndex;
				cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
				EXPECT_EQ(descriptorClusterIndex, fileManipulators[i].getDescriptorLocation().mDescriptorClusterIndex);
				EXPECT_EQ(recordIndex, fileManipulators[i].getDescriptorLocation().mRecordIndex % recordsPerCluster);
				ClusterIndexType clusterIndex = startClusterIndex;
				while (!cellValue.isEndOfChain()) {
					FATCellValueType nextCellValue;
					err = volumeManager.getFATCell(cellValue.getNext(), nextCellValue);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					EXPECT_EQ(nextCellValue.getPrev(), clusterIndex);

					// Update
					clusterIndex = cellValue.getNext();
					cellValue = nextCellValue;
				}
			}
		}
	}
}

void VirtualFileSystemTests::testIntegrityOfSequentialWrittenFile(FileManipulator &fileFM, VirtualFileSystem& vfs) {
	VolumeManager &volumeManager = vfs.mVolumeManager;
	const uint32_t recordsPerCluster = volumeManager.getClusterSize() / volumeManager.getFileDescriptorRecordStorageSize();

	ClusterIndexType startClusterIndex = fileFM.getStartCluster();
	FileSizeType fileSize = fileFM.getFileSize();
	uint32_t clustersForSize = static_cast<uint32_t>((fileSize + volumeManager.getClusterSize() - 1) / volumeManager.getClusterSize());
	uint32_t countClustersAllocated = 0;
	ClusterIndexType lastClusterIndex = ClusterValues::INVALID_VALUE;
	ErrorCode err = vfs._getCountClusters(startClusterIndex, countClustersAllocated, lastClusterIndex);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(countClustersAllocated, clustersForSize);
	EXPECT_EQ(startClusterIndex + countClustersAllocated - 1, lastClusterIndex);

	FATCellValueType firstCellValue;
	FATCellValueType lastCellValue;
	uint32_t counter = 0;
	err = vfs._iterateThroughClusterChain(startClusterIndex,
		[&counter, &fileFM, countClustersAllocated, startClusterIndex, lastClusterIndex, recordsPerCluster](bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)->ErrorCode {
		if (counter == 0) {
			EXPECT_EQ(startClusterIndex, currentCluster);
			ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			uint32_t recordIndex = ~0UL;
			cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(fileFM.getDescriptorLocation().mDescriptorClusterIndex, descriptorClusterIndex);
			EXPECT_EQ(fileFM.getDescriptorLocation().mRecordIndex % recordsPerCluster, recordIndex);
		}
		if (counter + 1 == countClustersAllocated) {
			EXPECT_EQ(lastClusterIndex, currentCluster);
			ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
			uint32_t recordIndex = ~0UL;
			cellValue.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
			EXPECT_EQ(fileFM.getDescriptorLocation().mDescriptorClusterIndex, descriptorClusterIndex);
			EXPECT_EQ(fileFM.getDescriptorLocation().mRecordIndex % recordsPerCluster, recordIndex);
		}
		++counter;
		return ErrorCode::RESULT_OK;
	}
	);
	EXPECT_EQ(counter, clustersForSize);
}

TEST_F(VirtualFileSystemTests, TruncatingFile) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = vfs.mVolumeManager;
		const uint32_t recordsPerCluster = volumeManager.getClusterSize() / volumeManager.getFileDescriptorRecordStorageSize();

		EXPECT_TRUE(volumeManager.clusterDataFileExists());
		EXPECT_TRUE(volumeManager.fatDataFileExists());
		EXPECT_EQ(volumeManager.getState(), FileSystemState::FSS_READY);

		uint32_t countFATBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_GE(countFATBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());
		uint32_t countDataBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countDataBlocks, 1UL + volumeManager.getFirstFileDataBlockIndex());

		FileManipulator rootFM;
		uint32_t recordIndex = 0UL;
		vfs._createRootDirFileManipulator(rootFM);

		FileManipulator dirFM;
		ErrorCode err = vfs.createDirectory("subdir", dirFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Create several empty files (for example 4 of them) to allocate some FileDescriptorRecords in the beginning of the directory.
		// Thus we will avoid having location 0 record for the file we will test further.
		const char* szFilePathFormat = "/subdir/file%04u.bin";
		char fileNameBuffer[50];
		for (int i = 0; i < 4; ++i) {
			FileManipulator localFileFM;
			snprintf(fileNameBuffer, sizeof(fileNameBuffer), szFilePathFormat, i);
			err = vfs.createFile(fileNameBuffer, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, localFileFM);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}

		FileSizeType initialFreeSpace = 0U;
		err = volumeManager.getFreeSpace(initialFreeSpace);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Now create the test file
		FileManipulator fileFM;
		err = vfs.createFile("/subdir/the_test_file.bin", AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		const uint32_t countClustersToWrite = 15;
		const size_t bufferSize = 1234 + (countClustersToWrite - 1) * volumeManager.getClusterSize();
		std::vector<uint8_t> buffer(bufferSize, 0xA5);
		size_t bytesWritten = 0;
		err = vfs.write(fileFM, buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		FileSizeType updatedFreeSpace = 0U;
		err = volumeManager.getFreeSpace(updatedFreeSpace);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(updatedFreeSpace + countClustersToWrite * volumeManager.getClusterSize(), initialFreeSpace);

		testIntegrityOfSequentialWrittenFile(fileFM, vfs);

		// Remove one cluster from the file
		size_t newFileSize = 2345 + (countClustersToWrite - 2) * volumeManager.getClusterSize();
		err = vfs._trunc(fileFM, newFileSize, false);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(newFileSize, fileFM.getFileSize());

		testIntegrityOfSequentialWrittenFile(fileFM, vfs);

		FileSizeType freeSpaceAfterTrunc = 0U;
		err = volumeManager.getFreeSpace(freeSpaceAfterTrunc);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(freeSpaceAfterTrunc + (countClustersToWrite - 1) * volumeManager.getClusterSize(), initialFreeSpace);

		// Truncate to a single cluster file
		newFileSize = 345;
		err = vfs._trunc(fileFM, newFileSize, false);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(newFileSize, fileFM.getFileSize());

		testIntegrityOfSequentialWrittenFile(fileFM, vfs);

		freeSpaceAfterTrunc = 0U;
		err = volumeManager.getFreeSpace(freeSpaceAfterTrunc);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(freeSpaceAfterTrunc + volumeManager.getClusterSize(), initialFreeSpace);
	}

}

TEST_F(VirtualFileSystemTests, MoveClusterNoTransaction) {
	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);
		VolumeManager &volumeManager = vfs.mVolumeManager;
		const uint32_t recordsPerCluster = volumeManager.getClusterSize() / volumeManager.getFileDescriptorRecordStorageSize();
		ErrorCode err;
		const char* szFilePathFormat = "/file%04u.bin";
		const uint32_t countFilesToCreate = 5; // Test on files with 1 to 5 clusters - [start cluster], [start neighbour], [middle], [end neighbour] [last cluster]
		const ClusterIndexType clusterOffset = 1000;

		// Create a buffer and fill it with data
		const size_t totalBufferSize = countFilesToCreate * volumeManager.getClusterSize();
		std::vector<uint8_t> readBuffer;
		std::vector<uint8_t> writeBuffer;
		readBuffer.resize(totalBufferSize);
		writeBuffer.resize(totalBufferSize);

		char filePath[50];
		const unsigned int kSeed = 53;

		FileDescriptorRecord fileDescriptorArray[countFilesToCreate];
		DescriptorLocation descriptorLocationArray[countFilesToCreate];

		std::vector<uint8_t> clusterBuffer0(volumeManager.getClusterSize());
		std::vector<uint8_t> clusterBuffer1(volumeManager.getClusterSize());

		uint32_t testCounter = 0;
		srand(kSeed);
		for (uint32_t i = 0; i < countFilesToCreate; ++i) {
			for (uint32_t clusterToMove = 0; clusterToMove <= i; ++clusterToMove, ++testCounter) {

				snprintf(filePath, sizeof(filePath), szFilePathFormat, testCounter);
				printf("Test #%u.\tFile with %u cluster(s). Moving cluster %u\n", testCounter, i + 1, clusterToMove);

				//Create a file with (i + 1) clusters
				{
					// Initialize the buffer with unique enough numbers
					for (size_t j = 0; j < totalBufferSize; ++j) {
						writeBuffer[j] = static_cast<uint8_t>(rand());
					}

					// Create the file
					FileManipulator fileFM;
					err = vfs.createFile(filePath, AccessMode::AM_BINARY | AccessMode::AM_WRITE, true, fileFM);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);

					// Write enough data to allocate i + 1 clusters 
					size_t sizeToWrite = i * volumeManager.getClusterSize() + volumeManager.getClusterSize() / 2; // Should allocate i + 1 clusters
					size_t bytesWritten = 0;
					err = vfs.write(fileFM, writeBuffer.data(), sizeToWrite, bytesWritten);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					EXPECT_EQ(sizeToWrite, bytesWritten);

					// Copy the file descriptor and location
					fileDescriptorArray[i] = fileFM.getFileDescriptorRecord();
					descriptorLocationArray[i] = fileFM.getDescriptorLocation();

					// Closing the file
					err = vfs.flush(fileFM);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);

					// Initial test
					ClusterIndexType startClusterIndex = fileDescriptorArray[i].mStartCluster;
					EXPECT_TRUE(isValidClusterIndex(startClusterIndex));
					FATCellValueType value = FATCellValueType::badCellValue();
					ErrorCode err = volumeManager.getFATCell(startClusterIndex, value);
					EXPECT_TRUE(value.isStartOfChain()); // Should say that it is the start of the chain

					ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
					uint32_t recordIndex = ~0UL;
					value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
					// Should point to the location of the file-descriptor for this file
					EXPECT_EQ(descriptorLocationArray[i].mDescriptorClusterIndex, descriptorClusterIndex);
					EXPECT_EQ(descriptorLocationArray[i].mRecordIndex % recordsPerCluster, recordIndex);

					ClusterIndexType lastClusterIndex = fileDescriptorArray[i].mLastCluster;
					EXPECT_TRUE(isValidClusterIndex(lastClusterIndex));
					value = FATCellValueType::badCellValue();
					err = volumeManager.getFATCell(lastClusterIndex, value);
					EXPECT_TRUE(value.isEndOfChain()); // This is the Root, and it should be End-of-chain

					descriptorClusterIndex = ClusterValues::INVALID_VALUE;
					recordIndex = ~0UL;
					value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
					// Should point to the location of the file-descriptor for this file
					EXPECT_EQ(descriptorLocationArray[i].mDescriptorClusterIndex, descriptorClusterIndex);
					EXPECT_EQ(descriptorLocationArray[i].mRecordIndex % recordsPerCluster, recordIndex);

					if (i == 0) {
						EXPECT_EQ(startClusterIndex, lastClusterIndex);
					}
				}

				// Open the file again and test
				{
					ClusterChainVector clusterChain;
					err = vfs._loadClusterChain(fileDescriptorArray[i].mStartCluster, clusterChain);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					EXPECT_EQ(clusterChain.size(), i + 1);

					// Moving cluster(s) and testing
					ClusterIndexType originalClusterIndex = clusterChain[clusterToMove].mClusterIndex;
					ClusterIndexType newClusterIndex = originalClusterIndex + clusterOffset + countFilesToCreate * testCounter;
					err = vfs.moveCluster(originalClusterIndex, newClusterIndex);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);

					// Testing the file
					//////////////////////////////////////////////////////

					// Open the file for reading
					FileManipulator fileFM;
					err = vfs.createGenericFileManipulatorForFilePath(filePath, fileFM);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					fileFM.mAccessMode = AccessMode::AM_READ;

					//Clean up the read buffer
					memset(readBuffer.data(), 0xA5, readBuffer.size());

					// Read the data
					size_t sizeToRead = i * volumeManager.getClusterSize() + volumeManager.getClusterSize() / 2; // Should allocate i + 1 clusters
					size_t bytesRead = 0;
					err = vfs.read(fileFM, readBuffer.data(), sizeToRead, bytesRead);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					EXPECT_EQ(sizeToRead, bytesRead);
					// Compare the content
					uint32_t countMatches = 0;
					for (size_t j = 0; j < sizeToRead; ++j) {
						if (writeBuffer[j] == readBuffer[j]) {
							++countMatches;
						}
					}
					EXPECT_EQ(countMatches, sizeToRead);

					// Copy the file descriptor and location
					FileDescriptorRecord record = fileFM.getFileDescriptorRecord();
					DescriptorLocation location = fileFM.getDescriptorLocation();

					// Closing the file
					err = vfs.flush(fileFM);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);

					// Initial test
					ClusterIndexType startClusterIndex = record.mStartCluster;
					EXPECT_TRUE(isValidClusterIndex(startClusterIndex));
					FATCellValueType value = FATCellValueType::badCellValue();
					ErrorCode err = volumeManager.getFATCell(startClusterIndex, value);
					EXPECT_TRUE(value.isStartOfChain()); // Should say that it is the start of the chain

					ClusterIndexType descriptorClusterIndex = ClusterValues::INVALID_VALUE;
					uint32_t recordIndex = ~0UL;
					value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
					// Should point to the location of the file-descriptor for this file
					EXPECT_EQ(location.mDescriptorClusterIndex, descriptorClusterIndex);
					EXPECT_EQ(location.mRecordIndex % recordsPerCluster, recordIndex);

					ClusterIndexType lastClusterIndex = record.mLastCluster;
					EXPECT_TRUE(isValidClusterIndex(lastClusterIndex));
					value = FATCellValueType::badCellValue();
					err = volumeManager.getFATCell(lastClusterIndex, value);
					EXPECT_TRUE(value.isEndOfChain()); // For a single cluster file, this should be the last cluster as well.

					if (value.isEndOfChain()) {
						descriptorClusterIndex = ClusterValues::INVALID_VALUE;
						recordIndex = ~0UL;
						value.decodeFileDescriptorLocation(descriptorClusterIndex, recordIndex);
						// Should point to the location of the file-descriptor for this file
						EXPECT_EQ(location.mDescriptorClusterIndex, descriptorClusterIndex);
						EXPECT_EQ(location.mRecordIndex % recordsPerCluster, recordIndex);
					}

					if (i == 0) {
						EXPECT_EQ(startClusterIndex, lastClusterIndex);
					}
				}
			}
		}
	}

}

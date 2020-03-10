/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include "SplitFAT/SplitFATFileSystem.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "BerwickSplitFATConfiguration.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/utils/PathString.h"
#include <memory>
#include <random>
#include <chrono>

using namespace SFAT;
using namespace Core::SFAT;

class HighLevelUnitTest : public testing::Test {
protected:  // You should make the members protected s.t. they can be
			// accessed from sub-classes.

	// virtual void SetUp() will be called before each test is run.  You
	// should define it if you need to initialize the variables.
	// Otherwise, this can be skipped.
	virtual void SetUp() override {
		// Start with cleaning up
		{
			std::shared_ptr<BerwickSplitFATConfiguration> lowLevelFileAccess = std::make_shared<BerwickSplitFATConfiguration>();
			lowLevelFileAccess->setup("D:\\PS4\\REPRO 1\\");
			std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
			fileStorage->setup(lowLevelFileAccess);
			fileStorage->cleanUp();
		}
	}

	// virtual void TearDown() will be called after each test is run.
	// You should define it if there is cleanup work to do.  Otherwise,
	// you don't have to provide it.
	//
	virtual void TearDown() override {
	}

	void createSplitFATFileStorage(SplitFATFileStorage& fileStorage) {
		std::shared_ptr<BerwickSplitFATConfiguration> lowLevelFileAccess = std::make_shared<BerwickSplitFATConfiguration>();
		ErrorCode err = lowLevelFileAccess->setup("D:\\PS4\\REPRO 1\\");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		err = fileStorage.setup(lowLevelFileAccess);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
};

/// Tests the creation of the Split FAT file system.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_Create) {

	SplitFATFileStorage fileStorage;
	createSplitFATFileStorage(fileStorage);
}

/// Tests the creation of file in the root directory.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_CreateFile) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	ErrorCode err = fileStorage->openFile(file, "test.bin", "wb");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	FileSizeType size = 0;
	FileSizeType errorMargin = 8192 * 8;
	err = fileStorage->getFreeSpace(size);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_TRUE(size < (23 * (256ULL << 20)) + errorMargin);
	EXPECT_TRUE(size > (23 * (256ULL << 20)) - errorMargin);
}

/// Tests directory creation and a file in it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_CreateDirectory) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	ErrorCode err = fileStorage->createDirectory("/subdir");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	FileHandle file;

	err = fileStorage->openFile(file, "/subdir/test.bin", "wb");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	bool bRes = fileStorage->fileExists("/subdir/test.bin");
	EXPECT_TRUE(bRes);
}

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_WriteToNewFile) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	ErrorCode err = fileStorage->openFile(file, "test.bin", "wb+");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	size_t bufferSize = 123;
	std::vector<uint8_t> buffer(bufferSize);
	for (size_t i = 0; i < bufferSize; ++i) {
		buffer[i] = static_cast<uint8_t>(i);
	}

	size_t bytesWritten = 0;
	err = file.write(buffer.data(), bufferSize, bytesWritten);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(bytesWritten, bufferSize);

	std::vector<uint8_t> readBuffer(bufferSize);
	err = file.seek(0, SeekMode::SM_SET);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	size_t bytesRead = 0;
	err = file.read(readBuffer.data(), bufferSize, bytesRead);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(bytesRead, bufferSize);

	EXPECT_TRUE(buffer == readBuffer);
}

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_WriteAndReadWithoutReadAccessMode) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	ErrorCode err = fileStorage->openFile(file, "test.bin", "wb");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	size_t bufferSize = 123;
	std::vector<uint8_t> buffer(bufferSize);
	for (size_t i = 0; i < bufferSize; ++i) {
		buffer[i] = static_cast<uint8_t>(i);
	}

	size_t bytesWritten = 0;
	err = file.write(buffer.data(), bufferSize, bytesWritten);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(bytesWritten, bufferSize);

	std::vector<uint8_t> readBuffer(bufferSize);
	err = file.seek(0, SeekMode::SM_SET);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	size_t bytesRead = 0;
	err = file.read(readBuffer.data(), bufferSize, bytesRead);
	EXPECT_EQ(err, ErrorCode::ERROR_TRYING_TO_READ_FILE_WITHOUT_READ_ACCESS_MODE);
}

/// Tests writing and reading two files.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_WriteReadTwoFiles) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	size_t bufferSize0 = 123;
	std::vector<uint8_t> buffer0(bufferSize0);
	for (size_t i = 0; i < bufferSize0; ++i) {
		buffer0[i] = static_cast<uint8_t>(i);
	}

	size_t bufferSize1 = 432;
	std::vector<uint8_t> buffer1(bufferSize1);
	for (size_t i = 0; i < bufferSize1; ++i) {
		buffer1[i] = 255 - static_cast<uint8_t>(i);
	}

	// Create and write the first test file
	{
		ErrorCode err = fileStorage->openFile(file, "test.bin", "wb+");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bytesWritten = 0;
		err = file.write(buffer0.data(), bufferSize0, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize0);

		// Read it back
		std::vector<uint8_t> readBuffer(bufferSize0);
		err = file.seek(0, SeekMode::SM_SET);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bufferSize0, bytesRead);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bufferSize0);

		EXPECT_TRUE(buffer0 == readBuffer);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	// Create and write the second test file
	{
		ErrorCode err = fileStorage->createDirectory("subDir");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		err = fileStorage->createDirectory("subDir/sub-subDir");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		err = fileStorage->openFile(file, "subDir/sub-subDir/test1.bin", "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bytesWritten = 0;
		err = file.write(buffer1.data(), bufferSize1, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize1);
	}

	// Open and read the first file
	{
		ErrorCode err = fileStorage->openFile(file, "test.bin", "rb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::vector<uint8_t> readBuffer(bufferSize0 * 2);

		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bufferSize0 * 2, bytesRead); //Trying to read twice as much, but we should get the correct size.
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bufferSize0);

		readBuffer.resize(bytesRead);
		EXPECT_TRUE(buffer0 == readBuffer);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	// Open and read the second file
	{
		ErrorCode err = fileStorage->openFile(file, "subDir/sub-subDir/test1.bin", "rb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::vector<uint8_t> readBuffer(bufferSize1 * 2);

		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bufferSize1 * 2, bytesRead); //Trying to read twice as much, but we should get the correct size.
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bufferSize1);

		readBuffer.resize(bytesRead);
		EXPECT_TRUE(buffer1 == readBuffer);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
}

/// Tests writing/reading a bigger than one cluster file
TEST_F(HighLevelUnitTest, SplitFATFileSystem_WriteReadABiggerFile) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	size_t bufferSize1 = 8193;
	std::vector<uint8_t> buffer1(bufferSize1);
	for (size_t i = 0; i < bufferSize1; ++i) {
		buffer1[i] = 255 - static_cast<uint8_t>(i);
	}

	// Create and write the test file
	{
		ErrorCode err = fileStorage->openFile(file, "test1.bin", "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bytesWritten = 0;
		err = file.write(buffer1.data(), bufferSize1, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize1);
	}

	// Open and read the test file
	{
		ErrorCode err = fileStorage->openFile(file, "test1.bin", "rb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::vector<uint8_t> readBuffer(bufferSize1 * 2);

		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bufferSize1 * 2, bytesRead); //Trying to read twice as much, but we should get the correct size.
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bufferSize1);

		readBuffer.resize(bytesRead);
		EXPECT_TRUE(buffer1 == readBuffer);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
}

// Tests writing/reading a bigger than one cluster file
TEST_F(HighLevelUnitTest, SplitFATFileSystem_WriteRead2MBFile) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	size_t bufferSize1 = 2 * (1 << 20);
	std::vector<uint8_t> buffer1(bufferSize1);
	for (size_t i = 0; i < bufferSize1; ++i) {
		buffer1[i] = 255 - static_cast<uint8_t>(i % 101);
	}

	// Create and write the test file
	{
		ErrorCode err = fileStorage->openFile(file, "test1.bin", "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bytesWritten = 0;
		err = file.write(buffer1.data(), bufferSize1, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize1);
	}

	// Open and read the test file
	{
		ErrorCode err = fileStorage->openFile(file, "test1.bin", "rb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::vector<uint8_t> readBuffer(bufferSize1 * 2);

		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bufferSize1 * 2, bytesRead); //Trying to read twice as much, but we should get the correct size.
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bufferSize1);

		readBuffer.resize(bytesRead);
		EXPECT_TRUE(buffer1 == readBuffer);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
}

// Tests writing/reading a bigger than one cluster file
TEST_F(HighLevelUnitTest, SplitFATFileSystem_RandomSizeWrite2MBFile) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	FileHandle file;

	size_t bufferSize = 2 * (1 << 20);
	std::vector<uint8_t> buffer(bufferSize);
	for (size_t i = 0; i < bufferSize; ++i) {
		buffer[i] = static_cast<uint8_t>(rand());
	}

	// Create and write the test file
	{
		ErrorCode err = fileStorage->openFile(file, "test1.bin", "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t maxWriteSize = 1 << 18;

		size_t totalBytesWritten = 0;
		size_t bytesRemainingToWrite = bufferSize;
		while (bytesRemainingToWrite > 0) {
			size_t bytesToWrite = std::min(bytesRemainingToWrite, 1 + rand() % maxWriteSize);
			size_t bytesWritten = 0;
			err = file.write(&buffer[totalBytesWritten], bytesToWrite, bytesWritten);
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(bytesWritten, bytesToWrite);

			bytesRemainingToWrite -= bytesWritten;
			totalBytesWritten += bytesWritten;
		}
	}

	// Open and read the test file
	{
		ErrorCode err = fileStorage->openFile(file, "test1.bin", "rb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::vector<uint8_t> readBuffer(bufferSize * 2);

		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bufferSize * 2, bytesRead); //Trying to read twice as much, but we should get the correct size.
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bufferSize);

		readBuffer.resize(bytesRead);
		EXPECT_TRUE(buffer == readBuffer);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
}

// Large file parameters
namespace
{
	// Different options to test with
	//size_t kFileSizeTarget = 270 * (1 << 20); // 270MB
	//size_t kFileSizeTarget = (1024+512) * (1 << 20); // 1.5GB
	size_t kFileSizeTarget = 1 * (1ULL << 30); // 1GB
	const unsigned int kSeed = 53;
}

// Tests writing/reading a bigger than one cluster file
void testReadingLargeFile(std::shared_ptr<SplitFATFileStorage> fileStorage) {

	printf("Start large file reading test!\n\n");
	auto startTime = std::chrono::high_resolution_clock::now();

	FileHandle file;

	size_t bufferSize = 64 * (1 << 20);
	std::vector<uint8_t> buffer(bufferSize);
	srand(kSeed);

	size_t steps = (kFileSizeTarget + bufferSize - 1) / bufferSize;

	// Open and read the test file
	{
		ErrorCode err = fileStorage->openFile(file, "largeFile.bin", "rb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::vector<uint8_t> readBuffer(bufferSize);
		std::mt19937 mt_rand(kSeed);
		size_t totalBytesRead = 0;

		for (size_t i = 0; i < steps; ++i) {

			size_t bytesToRead = std::min(bufferSize, kFileSizeTarget - totalBytesRead);
			printf("Read %.2fMB\n", bytesToRead / float(1 << 20));
			// Fill the buffer with pseudo random numbers
			for (size_t i = 0; i < bufferSize; ++i) {
				buffer[i] = static_cast<uint8_t>(mt_rand());
			}

			SFAT_ASSERT(kFileSizeTarget >= totalBytesRead, "The bytes written shouldn't pass over the target size!");
			size_t bytesRead = 0;
			err = file.read(readBuffer.data(), bytesToRead, bytesRead); //Trying to read twice as much, but we should get the correct size.
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(bytesRead, bytesToRead);

			if (bytesRead == bufferSize) {
				if (buffer != readBuffer) {
					for (int i = 0; i < buffer.size(); ++i) {
						if (buffer[i] != readBuffer[i]) {
							printf("Element #%u is different", i);
						}
					}
				}
				EXPECT_TRUE(buffer == readBuffer);
			}
			else {
				readBuffer.resize(bytesRead);
				buffer.resize(bytesRead);
				EXPECT_TRUE(buffer == readBuffer);
				readBuffer.resize(bufferSize);
				buffer.resize(bufferSize);
			}

			totalBytesRead += bytesRead;
		}
		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> diff = endTime - startTime;
	float mbPerSec = static_cast<float>((kFileSizeTarget / diff.count()) / (1 << 20));
	printf("Time to read: %3.3f for %5.1fMB, %3.2fMB/s\n", diff.count(), kFileSizeTarget / static_cast<float>(1 << 20), mbPerSec);

	printf("Finished large file reading test!\n\n");
}

// Tests writing/reading a bigger than one cluster file
void testWritingReadingLargeFile(std::shared_ptr<SplitFATFileStorage> fileStorage) {

	printf("Start large file test!\n\n");
	auto startTime = std::chrono::high_resolution_clock::now();

	FileHandle file;

	size_t bufferSize = 64 * (1 << 20);
	std::vector<uint8_t> buffer(bufferSize);
	srand(kSeed);

	size_t steps = (kFileSizeTarget + bufferSize - 1) / bufferSize;

	// Create and write the test file
	{
		ErrorCode err = fileStorage->openFile(file, "largeFile.bin", "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::mt19937 mt_rand(kSeed);
		size_t totalBytesWritten = 0;

		for (size_t i = 0; i < steps; ++i) {

			size_t bytesToWrite = std::min(bufferSize, kFileSizeTarget - totalBytesWritten);
			printf("Write %.2fMB\n", bytesToWrite / float(1 << 20));
			// Fill the buffer with pseudo random numbers
			for (size_t i = 0; i < bufferSize; ++i) {
				buffer[i] = static_cast<uint8_t>(mt_rand());
			}
			SFAT_ASSERT(kFileSizeTarget >= totalBytesWritten, "The bytes written shouldn't pass over the target size!");
			size_t bytesWritten = 0;
			err = file.write(buffer.data(), bytesToWrite, bytesWritten); //Trying to read twice as much, but we should get the correct size.
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_EQ(bytesWritten, bytesToWrite);

			totalBytesWritten += bytesWritten;
		}
		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> diff = endTime - startTime;
	float mbPerSec = static_cast<float>((kFileSizeTarget / diff.count()) / (float)(1 << 20));
	printf("Time taken to write: %3.3f for %5.1fMB, %3.2fMB/s\n", diff.count(), kFileSizeTarget / static_cast<float>(1 << 20), mbPerSec);

	testReadingLargeFile(fileStorage);

	printf("Finished large file test!\n\n");
}

#ifndef _DEBUG //This is too slow for Debug

// Tests writing/reading a bigger than one cluster file
TEST_F(HighLevelUnitTest, SplitFATFileSystem_LargeFile257MB) {

	std::shared_ptr<SplitFATFileStorage> fileStorage = std::make_shared<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	testWritingReadingLargeFile(fileStorage);
}

#endif // _DEBUG

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_GetFileSize) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	size_t bufferSize = 1248;

	// Create a file
	{
		FileHandle file;

		ErrorCode err = fileStorage->openFile(file, "test1248.bin", "wb+");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		FileSizeType fileSize = 0xBCDE;
		err = fileStorage->getFileSize("test1248.bin", fileSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(fileSize, 0);

		std::vector<uint8_t> buffer(bufferSize);
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(i);
		}

		size_t bytesWritten = 0;
		err = file.write(buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		fileSize = 0xCDEF;
		err = fileStorage->getFileSize("test1248.bin", fileSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(fileSize, bufferSize);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	FileSizeType fileSize = 0;
	ErrorCode err = fileStorage->getFileSize("test1248.bin", fileSize);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(fileSize, bufferSize);

	fileSize = 0xABCD;
	err = fileStorage->getFileSize("there_is_no_such_file.bin", fileSize);
	EXPECT_EQ(err, ErrorCode::ERROR_FILE_COULD_NOT_BE_FOUND);
	EXPECT_EQ(fileSize, 0);

	err = fileStorage->createDirectory("someSubDirButNotFile");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	fileSize = 0xABCD;
	err = fileStorage->getFileSize("someSubDirButNotFile", fileSize);
	EXPECT_EQ(err, ErrorCode::ERROR_CAN_NOT_GET_FILE_SIZE_OF_DIRECTORY);
	EXPECT_EQ(fileSize, 0);
}

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_IsFileOrDirectory) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	ErrorCode err = fileStorage->createDirectory("a_directory");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	// Create a file
	{
		size_t bufferSize = 1030;
		FileHandle file;

		ErrorCode err = fileStorage->openFile(file, "a_directory/test1248.bin", "wb+");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		EXPECT_FALSE(fileStorage->isDirectory("a_directory/test1248.bin"));
		EXPECT_TRUE(fileStorage->isFile("a_directory/test1248.bin"));

		std::vector<uint8_t> buffer(bufferSize);
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(i);
		}

		size_t bytesWritten = 0;
		err = file.write(buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	EXPECT_FALSE(fileStorage->isDirectory("a_directory/test1248.bin"));
	EXPECT_TRUE(fileStorage->isFile("a_directory/test1248.bin"));

	EXPECT_TRUE(fileStorage->isDirectory("a_directory"));
	EXPECT_FALSE(fileStorage->isFile("a_directory"));

	err = fileStorage->createDirectory("a_directory/a_subDirectory");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	EXPECT_TRUE(fileStorage->isDirectory("a_directory/a_subDirectory"));
	EXPECT_FALSE(fileStorage->isFile("a_directory/a_subDirectory"));

	EXPECT_TRUE(fileStorage->isDirectory("")); // Root directory
	EXPECT_TRUE(fileStorage->isDirectory("/")); // Root directory
	EXPECT_TRUE(fileStorage->isDirectory("\\")); // Root directory
	EXPECT_FALSE(fileStorage->isFile(""));

	EXPECT_FALSE(fileStorage->isDirectory("non_existing_entity"));
	EXPECT_FALSE(fileStorage->isFile("non_existing_entity"));
}


/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_DeleteAnEmptyFile) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	// Create a file and leave it empty
	{
		FileHandle file;

		ErrorCode err = fileStorage->openFile(file, "emptyFile000.bin", "wb+");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		EXPECT_TRUE(fileStorage->fileExists("emptyFile000.bin"));

		FileSizeType fileSize = 0xBCDE;
		err = fileStorage->getFileSize("emptyFile000.bin", fileSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(fileSize, 0);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	EXPECT_TRUE(fileStorage->fileExists("emptyFile000.bin"));

	ErrorCode err = fileStorage->deleteFile("emptyFile000.bin");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	EXPECT_FALSE(fileStorage->fileExists("emptyFile000.bin"));
}

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_CreateDeleteAndCreateFileWithTheSameName) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	const char *szFilePath = "fileToDetete.bin";

	// Create a file and leave it empty
	{
		FileHandle file;

		ErrorCode err = fileStorage->openFile(file, szFilePath, "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		EXPECT_TRUE(fileStorage->fileExists(szFilePath));

		FileSizeType fileSize = 0xBCDE;
		err = fileStorage->getFileSize(szFilePath, fileSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(fileSize, 0);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	EXPECT_TRUE(fileStorage->fileExists(szFilePath));

	ErrorCode err = fileStorage->deleteFile(szFilePath);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	EXPECT_FALSE(fileStorage->fileExists(szFilePath));

	// Create a file with the same name again
	{
		FileHandle file;

		ErrorCode err = fileStorage->openFile(file, szFilePath, "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		EXPECT_TRUE(fileStorage->fileExists(szFilePath));

		FileSizeType fileSize = 0xBCDE;
		err = fileStorage->getFileSize(szFilePath, fileSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(fileSize, 0);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
}

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_FlatIterationThroughADirectory) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	struct FileSystemEntityDescription {
		std::string mEntityName;
		bool mIsFile;
		bool mToBeDeleted = false;
	};

	std::vector<FileSystemEntityDescription> directoryTree = {
		{"dir0", false},
		{"file0", true},
		{"dir0/level1Dir0", false},
		{"dir0/level1Dir0/file0", true},
		{"dir0/level1Dir0/file1", true},
		{"file1", true},
		{"dir1", false},
		{"file1", true},
		{"file2", true, true},
		{"file3", true}
	};
	const char *szFilePath = "fileToDetete.bin";

	// Create the directory tree
	for (auto&& entity : directoryTree) {
		if (entity.mIsFile) {
			FileHandle file;

			ErrorCode err = fileStorage->openFile(file, entity.mEntityName.c_str(), "wb");
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_TRUE(fileStorage->fileExists(entity.mEntityName.c_str()));
			err = file.close();
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}
		else {
			fileStorage->createDirectory(entity.mEntityName.c_str());
			EXPECT_TRUE(fileStorage->directoryExists(entity.mEntityName.c_str()));
		}
	}

	// Remove whatever is marked for removal
	for (auto&& entity : directoryTree) {
		if (!entity.mToBeDeleted) {
			continue;
		}

		if (entity.mIsFile) {
			ErrorCode err = fileStorage->deleteFile(entity.mEntityName.c_str());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}
		else {
			ErrorCode err = fileStorage->removeDirectory(entity.mEntityName.c_str());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}
	}

	// Set of entities in the root
	std::set<std::string> rootEntities;
	for (auto&& entity : directoryTree) {
		if (entity.mToBeDeleted) {
			continue;
		}
		if (entity.mEntityName.find("/") == std::string::npos) {
			rootEntities.insert(entity.mEntityName);
		}
	}

	std::set<std::string> entitiesFoundAlready;
	int recordIndex = 0;
	fileStorage->iterateThroughDirectory("/", DI_ALL, [&entitiesFoundAlready, &rootEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
		EXPECT_NE(record.mEntityName[0], 0);
		EXPECT_FALSE(record.isDeleted());
		EXPECT_TRUE(rootEntities.find(record.mEntityName) != rootEntities.end());
		EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
		entitiesFoundAlready.insert(record.mEntityName);
		std::string expectedFullPath = "/";
		expectedFullPath += record.mEntityName;
		EXPECT_STREQ(expectedFullPath.c_str(), fullPath.c_str());
		return ErrorCode::RESULT_OK;
	});

	EXPECT_TRUE(rootEntities == entitiesFoundAlready);
}

/// Tests creating a new file and writing to it.
TEST_F(HighLevelUnitTest, SplitFATFileSystem_RecursiveIterationThroughADirectory) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	struct FileSystemEntityDescription {
		std::string mEntityName;
		bool mIsFile;
		bool mToBeDeleted = false;
	};

	std::vector<FileSystemEntityDescription> directoryTree = {
		{"dir0", false},
		{"file0", true},
		{"dir0/level1dir0", false},
		{"dir0/level1dir0/level2file0", true},
		{"dir0/level1dir0/level2file1", true},
		{"dir0/level1dir0/level2dir0", false},
		{"file1", true},
		{"dir1", false},
		{"file1", true},
		{"file2", true, true},
		{"file3", true}
	};
	const char *szFilePath = "fileToDetete.bin";

	// Create the directory tree
	for (auto&& entity : directoryTree) {
		if (entity.mIsFile) {
			FileHandle file;

			ErrorCode err = fileStorage->openFile(file, entity.mEntityName.c_str(), "wb");
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
			EXPECT_TRUE(fileStorage->fileExists(entity.mEntityName.c_str()));
			err = file.close();
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}
		else {
			fileStorage->createDirectory(entity.mEntityName.c_str());
			EXPECT_TRUE(fileStorage->directoryExists(entity.mEntityName.c_str()));
		}
	}

	// Remove whatever is marked for removal
	for (auto&& entity : directoryTree) {
		if (!entity.mToBeDeleted) {
			continue;
		}

		if (entity.mIsFile) {
			ErrorCode err = fileStorage->deleteFile(entity.mEntityName.c_str());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}
		else {
			ErrorCode err = fileStorage->removeDirectory(entity.mEntityName.c_str());
			EXPECT_EQ(err, ErrorCode::RESULT_OK);
		}
	}

	// Set of entities in the root
	std::set<std::string> allEntities;
	std::map<std::string, std::string> pathPerEntityMap;
	for (auto&& entity : directoryTree) {
		if (entity.mToBeDeleted) {
			continue;
		}

		// Get only the entity-name skipping the parent path
		size_t pos = entity.mEntityName.rfind("/");
		std::string entityName;
		if (pos != std::string::npos) {
			entityName = entity.mEntityName.substr(pos + 1);
		}
		else {
			entityName = entity.mEntityName;
		}
		allEntities.insert(entityName);
		std::string fullPath = "/";
		fullPath += entity.mEntityName;
		pathPerEntityMap[entityName] = fullPath;
	}

	std::set<std::string> entitiesFoundAlready;
	int recordIndex = 0;
	fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&pathPerEntityMap, &entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
		EXPECT_NE(record.mEntityName[0], 0);
		EXPECT_FALSE(record.isDeleted());
		EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
		EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
		entitiesFoundAlready.insert(record.mEntityName);
		auto it = pathPerEntityMap.find(record.mEntityName);
		EXPECT_TRUE(it != pathPerEntityMap.end());
		if (it != pathPerEntityMap.end()) {
			EXPECT_STREQ(it->second.c_str(), fullPath.c_str());
		}
		return ErrorCode::RESULT_OK;
	});

	EXPECT_TRUE(allEntities == entitiesFoundAlready);
}

#if 0

// Tests for Directory data corruption - doesn't have the corresponging changes in FAT
TEST_F(HighLevelUnitTest, SplitFATFileSystem_FATNotSavedDetection) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	bool transactionStarted;
	ErrorCode err = fileStorage->tryStartTransaction(transactionStarted);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_TRUE(transactionStarted);

	const char* szFilePath = "testFATNotSaved.bin";

	if (transactionStarted) {

		FileHandle file;
		err = fileStorage->openFile(file, szFilePath, "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bufferSize = 8192 + 8192/2;
		std::vector<uint8_t> buffer(bufferSize);
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(i);
		}

		size_t bytesWritten = 0;
		err = file.write(buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Create a "FAT changes not saved" problem here!
		err = fileStorage->executeDebugCommand(szFilePath, "discardFATCachedChanges");
		EXPECT_EQ(err, ErrorCode::RESULT_OK); //We should be able to do that for the test.

		err = fileStorage->endTransaction();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);


		err = fileStorage->openFile(file, szFilePath, "rb");
		std::vector<uint8_t> readBuffer(bufferSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Run integrity test!
		err = fileStorage->executeDebugCommand(szFilePath, "integrityTest");
		EXPECT_EQ(err, ErrorCode::ERROR_FILES_INTEGRITY);

		//size_t bytesRead = 0;
		//err = file.read(readBuffer.data(), bufferSize, bytesRead);
		//EXPECT_EQ(err, ErrorCode::RESULT_OK);
		//EXPECT_EQ(bytesRead, bufferSize);

		//EXPECT_TRUE(buffer == readBuffer);
	}
}

// Tests the FAT integrity verification
// Create artificially a data corruption - doesn't have the corresponging changes in the Directory data
TEST_F(HighLevelUnitTest, SplitFATFileSystem_FileFATNotSavedDetection) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	bool transactionStarted;
	ErrorCode err = fileStorage->tryStartTransaction(transactionStarted);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_TRUE(transactionStarted);

	const char* szFilePath = "testFATNotSaved.bin";

	if (transactionStarted) {

		FileHandle file;
		err = fileStorage->openFile(file, szFilePath, "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bufferSize = 8192 + 8192/2; // Write a file that needs 2 clusters
		std::vector<uint8_t> buffer(bufferSize);
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(i);
		}

		size_t bytesWritten = 0;
		err = file.write(buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Create a "FAT changes not saved" problem here!
		err = fileStorage->executeDebugCommand(szFilePath, "discardDirectoryCachedChanges");
		EXPECT_EQ(err, ErrorCode::RESULT_OK); //We should be able to do that for the test.

		err = fileStorage->endTransaction();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Run the integrity test!
		// At that point we should have clusters that are allocated (and in chain),
		// but there is no FileDescriptorRecord that correposnds to the cluster chain.
		// So the detected error should be in the FAT
		err = fileStorage->executeDebugCommand(szFilePath, "integrityTest");
		EXPECT_EQ(err, ErrorCode::ERROR_FAT_INTEGRITY);

		//err = fileStorage->openFile(file, szFilePath, "rb");
		//std::vector<uint8_t> readBuffer(bufferSize);
		//EXPECT_EQ(err, ErrorCode::RESULT_OK);

		//size_t bytesRead = 0;
		//err = file.read(readBuffer.data(), bufferSize, bytesRead);
		//EXPECT_EQ(err, ErrorCode::RESULT_OK);
		//EXPECT_EQ(bytesRead, bufferSize);

		//EXPECT_TRUE(buffer == readBuffer);
	}
}

// Tests for detection of both - directory data corruption and FAT corruption
TEST_F(HighLevelUnitTest, SplitFATFileSystem_FATAndDirectoryCorruptionDetection) {

	std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);

	const char* szFilePath = "testFATNotSaved.bin";
	ErrorCode err;

	// Create a bigger file initially (5 clusters)
	{
		FileHandle file;
		err = fileStorage->openFile(file, szFilePath, "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bufferSize = 5*8192;
		std::vector<uint8_t> buffer(bufferSize);
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(i);
		}

		size_t bytesWritten = 0;
		err = file.write(buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}

	bool transactionStarted;
	err = fileStorage->tryStartTransaction(transactionStarted);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_TRUE(transactionStarted);

	if (transactionStarted) {

		FileHandle file;
		// The following should truncate the file and free up the allocated clusters in the FAT
		err = fileStorage->openFile(file, szFilePath, "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		size_t bufferSize = 8192 + 8192 / 2; // 2 clusrters
		std::vector<uint8_t> buffer(bufferSize);
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(i);
		}

		size_t bytesWritten = 0;
		err = file.write(buffer.data(), bufferSize, bytesWritten);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesWritten, bufferSize);

		err = file.close();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Create a "FAT changes not saved" problem here!
		// As result the there would be 5 clusters allocated for the file, but the FileDescriptorRecord will point to wrong end ans size (2 clusters) of the file.
		err = fileStorage->executeDebugCommand(szFilePath, "discardFATCachedChanges");
		EXPECT_EQ(err, ErrorCode::RESULT_OK); //We should be able to do that for the test.

		err = fileStorage->endTransaction();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);


		err = fileStorage->openFile(file, szFilePath, "rb");
		std::vector<uint8_t> readBuffer(bufferSize);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		// Run integrity test!
		err = fileStorage->executeDebugCommand(szFilePath, "integrityTest");
		EXPECT_EQ(err, ErrorCode::ERROR_INTEGRITY);

		// Run data consistency test! 
		// For every allocated cluster checks whether the CRC calculated
		// from the stored data matches with the one stored in the corresponding FAT cell.
		err = fileStorage->executeDebugCommand(szFilePath, "dataConsistencyTest");
		EXPECT_EQ(err, ErrorCode::ERROR_FILES_INTEGRITY);

		//size_t bytesRead = 0;
		//err = file.read(readBuffer.data(), bufferSize, bytesRead);
		//EXPECT_EQ(err, ErrorCode::RESULT_OK);
		//EXPECT_EQ(bytesRead, bufferSize);

		//EXPECT_TRUE(buffer == readBuffer);
	}
}

#endif
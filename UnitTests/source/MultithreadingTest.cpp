/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include "SplitFAT/SplitFATFileSystem.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "WindowsSplitFATConfiguration.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/utils/SFATAssert.h"
#include <memory>
#include <random>
#include <chrono>
#include <thread>
#include <iostream>

#define SPLITFAT_PRINT_LOCAL_LOG_INFO	0
#define SPLITFAT_ENABLE_COMMON_MULTITHREAD_TESTS	1

// This should remain defined as 0.
// The purpose of the test that this define enables is for future development.
// Currently the test will fail.
#define SPLITFAT_ENABLE_MULTITHREAD_WRITE_WITHOUT_TRANSACTION_TEST	0

using namespace SFAT;

namespace {
	const char* kVolumeControlAndFATDataFilePath = "SFATControl.dat";
	const char* kClusterDataFilePath = "data.dat";
	const char* kTransactionFilePath = "_SFATTransaction.dat";

	struct FileSystemEntityDescription {
		std::string mEntityName;
		bool mIsFile;
		size_t mSize = 0;
		uint32_t mSeed = 42;
	};
}

class MultithreadingTest : public testing::Test {
protected:  // You should make the members protected s.t. they can be
			// accessed from sub-classes.

	// virtual void SetUp() will be called before each test is run.  You
	// should define it if you need to initialize the variables.
	// Otherwise, this can be skipped.
	virtual void SetUp() override {
		// Start with cleaning up
		{
			std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
			lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
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
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		ErrorCode err = lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		err = fileStorage.setup(lowLevelFileAccess);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
	}
};

/// Tests the creation of the Split FAT file system.
TEST_F(MultithreadingTest, SplitFATFileSystem_Create) {

	SplitFATFileStorage fileStorage;
	createSplitFATFileStorage(fileStorage);
}

void createAndWriteFile(const std::string& filePath, std::shared_ptr<SplitFATFileStorage> fileStorage, unsigned int seed, size_t fileTargetSize) {

#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("Path: %s\t\tStart large file write test!\n", filePath.c_str());
#endif

	auto startTime = std::chrono::high_resolution_clock::now();

	FileHandle file;

	size_t bufferSize = 64 * (1 << 20);
	std::vector<uint8_t> buffer(bufferSize);

	size_t steps = (fileTargetSize + bufferSize - 1) / bufferSize;

	// Create and write the test file
	{
		ErrorCode err = fileStorage->openFile(file, filePath.c_str(), "wb");
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		std::mt19937 mt_rand(seed);
		size_t totalBytesWritten = 0;

		for (size_t i = 0; i < steps; ++i) {

			size_t bytesToWrite = std::min(bufferSize, fileTargetSize - totalBytesWritten);
			// Fill the buffer with pseudo random numbers
			for (size_t i = 0; i < bytesToWrite; ++i) {
				buffer[i] = static_cast<uint8_t>(mt_rand());
			}
			SFAT_ASSERT(fileTargetSize >= totalBytesWritten, "The bytes written shouldn't pass over the target size!");
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
	float mbPerSec = static_cast<float>((fileTargetSize / diff.count()) / (1ULL << 20));
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("Path: %s\t\tTime taken to write: %3.3f for %5.1fMB, %3.2fMB/s\n", filePath.c_str(), diff.count(), fileTargetSize / static_cast<float>(1 << 20), mbPerSec);
#endif
}

void readAndCompareFile(const std::string& filePath, std::shared_ptr<SplitFATFileStorage> fileStorage, unsigned int seed, size_t fileTargetSize)
{
	size_t bufferSize = 4 * (1 << 20); // 4MB
	FileHandle file;
	ErrorCode err = fileStorage->openFile(file, filePath.c_str(), "rb");
	EXPECT_EQ(err, ErrorCode::RESULT_OK);

	std::vector<uint8_t> readBuffer(bufferSize);
	std::mt19937 mt_rand(seed);
	size_t totalBytesRead = 0;
	std::vector<uint8_t> buffer(bufferSize);

#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("Path: %s\t\tStart large file reading test!\n", filePath.c_str());
#endif
	auto startTime = std::chrono::high_resolution_clock::now();

	size_t steps = (fileTargetSize + bufferSize - 1) / bufferSize;

	for (size_t i = 0; i < steps; ++i) {

		size_t bytesToRead = std::min(bufferSize, fileTargetSize - totalBytesRead);
		// Fill the buffer with pseudo random numbers
		for (size_t i = 0; i < bufferSize; ++i) {
			buffer[i] = static_cast<uint8_t>(mt_rand());
		}

		SFAT_ASSERT(fileTargetSize >= totalBytesRead, "The bytes written shouldn't pass over the target size!");
		size_t bytesRead = 0;
		err = file.read(readBuffer.data(), bytesToRead, bytesRead); //Trying to read twice as much, but we should get the correct size.
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(bytesRead, bytesToRead);

		if (bytesRead == bufferSize) {
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
			if (buffer != readBuffer) {
				for (int i = 0; i < buffer.size(); ++i) {
					if (buffer[i] != readBuffer[i]) {
						printf("Element #%u is different", i);
					}
				}
			}
#endif
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

	auto endTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> diff = endTime - startTime;
	float mbPerSec = static_cast<float>((fileTargetSize / diff.count()) / (1ULL << 20));
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("Path: %s\t\tTime to read: %3.3f for %5.1fMB, %3.2fMB/s\n", filePath.c_str(), diff.count(), fileTargetSize / static_cast<float>(1 << 20), mbPerSec);
#endif
}

void createDirectoryTree(std::shared_ptr<SplitFATFileStorage> fileStorage, const std::vector<FileSystemEntityDescription>& directoryTree, const std::string& parentPath) {
	// Create the directory tree
	for (auto&& entity : directoryTree) {
		std::string fullPath = parentPath + "/" + entity.mEntityName;
		if (entity.mIsFile) {
			FileHandle file;

			createAndWriteFile(fullPath, fileStorage, entity.mSeed, entity.mSize);
			EXPECT_TRUE(fileStorage->fileExists(fullPath.c_str()));
		}
		else {
			fileStorage->createDirectory(fullPath.c_str());
			EXPECT_TRUE(fileStorage->directoryExists(fullPath.c_str()));
		}
	}
}

void testReadDirectoryTree(std::shared_ptr<SplitFATFileStorage> fileStorage, const std::vector<FileSystemEntityDescription>& directoryTree, const std::string& parentPath) {
	// Read all files and compare them
	for (auto&& entity : directoryTree) {
		if (entity.mIsFile) {
			FileHandle file;

			std::string fullPath = parentPath + "/" + entity.mEntityName;
			readAndCompareFile(fullPath, fileStorage, entity.mSeed, entity.mSize);
			EXPECT_TRUE(fileStorage->fileExists(fullPath.c_str()));
		}
	}
}

#if (SPLITFAT_ENABLE_COMMON_MULTITHREAD_TESTS == 1)
/// Tests locating and reading multiple files in multithreading.
TEST_F(MultithreadingTest, TestMultithreadReadingFiles) {

	std::shared_ptr<SplitFATFileStorage> fileStorage = std::make_shared<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);


	std::vector<FileSystemEntityDescription> directoryTree = {
		{"dir0", false},
		{"file0", true, 7238, 736},
		{"dir0/level1dir0", false},
		{"dir0/level1dir0/level2file0", true, 536873},
		{"dir0/level1dir0/level2file1", true, 0},
		{"dir0/level1dir0/level2dir0", false},
		{"file1", true, 36735, 543},
		{"dir1", false},
		{"file4", true, 7823, 83},
		{"file2", true, 23, 24},
		{"file3", true, 83, 74}
	};

	const int threadsCount = 20;
	char parentDirectoryPath[50];
	for (int i = 0; i < threadsCount; ++i) {
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
		printf("\n\n");
#endif
		snprintf( parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
		fileStorage->createDirectory(parentDirectoryPath);
		EXPECT_TRUE(fileStorage->directoryExists(parentDirectoryPath));
		createDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
	}

	std::thread t[threadsCount];

	//Launch a group of threads
	const int timesToRead = 2;
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("\n\nStart reading in multiple threads\n\n");
#endif
	for (int i = 0; i < threadsCount; ++i) {
		t[i] = std::thread([i, &fileStorage, &directoryTree, &timesToRead]() {
			char parentDirectoryPath[50];
			snprintf(parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
			for (int k = 0; k < timesToRead; ++k) {
				//if (i == 0) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(50));
				//}
				testReadDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
			}
		});
	}

	//Join the threads with the main thread
	for (int i = 0; i < threadsCount; ++i) {
		t[i].join();
	}
}
#endif

#if (SPLITFAT_ENABLE_MULTITHREAD_WRITE_WITHOUT_TRANSACTION_TEST == 1)
/// Tests writing multiple files in multithreading.
/// Note! This is not supposed to pass currently. The multithreded writing is safe only with transaction!
TEST_F(MultithreadingTest, TestMultithreadWritingFiles) {

	std::shared_ptr<SplitFATFileStorage> fileStorage = std::make_shared<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);


	std::vector<FileSystemEntityDescription> directoryTree = {
		{"dir0", false},
		{"file0", true, 7238, 736},
		{"dir0/level1dir0", false},
		{"dir0/level1dir0/level2file0", true, 536873},
		{"dir0/level1dir0/level2file1", true, 0},
		{"dir0/level1dir0/level2dir0", false},
		{"file1", true, 36735, 543},
		{"dir1", false},
		{"file4", true, 7823, 83},
		{"file2", true, 23, 24},
		{"file3", true, 83, 74}
	};

	const int threadsCount = 50;

	std::thread t[threadsCount];

	//Launch a group of threads to write different files simultaneously
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("\n\nStart writing in multiple threads\n\n");
#endif
	for (int i = 0; i < threadsCount; ++i) {
		int waitTime = rand() % 10;
		t[i] = std::thread([i, &fileStorage, &directoryTree, waitTime]() {
			char parentDirectoryPath[50];
			snprintf(parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
			std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
			fileStorage->createDirectory(parentDirectoryPath);
			EXPECT_TRUE(fileStorage->directoryExists(parentDirectoryPath));
			createDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
		});
	}

	//Join the threads with the main thread
	for (int i = 0; i < threadsCount; ++i) {
		t[i].join();
	}

	//Launch a group of threads
	const int timesToRead = 1;
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("\n\nStart reading in multiple threads\n\n");
#endif
	for (int i = 0; i < threadsCount; ++i) {
		t[i] = std::thread([i, &fileStorage, &directoryTree, &timesToRead]() {
			char parentDirectoryPath[50];
			snprintf(parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
			for (int k = 0; k < timesToRead; ++k) {
				//if (i == 0) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(50));
				//}
				testReadDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
			}
		});
	}

	//Join the threads with the main thread
	for (int i = 0; i < threadsCount; ++i) {
		t[i].join();
	}
}
#endif

#if (SPLITFAT_ENABLE_COMMON_MULTITHREAD_TESTS == 1)
/// Tests writing multiple files in transaction, in multithreading.
TEST_F(MultithreadingTest, TestMultithreadWritingFilesInTransaction) {

	std::shared_ptr<SplitFATFileStorage> fileStorage = std::make_shared<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);


	std::vector<FileSystemEntityDescription> directoryTree = {
		{"dir0", false},
		{"file0", true, 7238, 736},
		{"dir0/level1dir0", false},
		{"dir0/level1dir0/level2file0", true, 536873},
		{"dir0/level1dir0/level2file1", true, 0},
		{"dir0/level1dir0/level2dir0", false},
		{"file1", true, 36735, 543},
		{"dir1", false},
		{"file4", true, 7823, 83},
		{"file2", true, 23, 24},
		{"file3", true, 83, 74}
	};

	const int threadsCount = 10;
	std::thread t[threadsCount];

	//Launch a group of threads to write different files simultaneously
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("\n\nStart writing in multiple threads\n\n");
#endif
	for (int i = 0; i < threadsCount; ++i) {
		int waitTime = rand() % 50;
		t[i] = std::thread([i, &fileStorage, &directoryTree, waitTime]() {
			char parentDirectoryPath[50];
			snprintf(parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
			std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

			bool createdTransaction = false;
			while (!createdTransaction) {
				fileStorage->tryStartTransaction(createdTransaction);
				if (!createdTransaction) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			}
			EXPECT_TRUE(createdTransaction);
			if (createdTransaction) {
				fileStorage->createDirectory(parentDirectoryPath);
				EXPECT_TRUE(fileStorage->directoryExists(parentDirectoryPath));
				createDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
				fileStorage->endTransaction();
			}
		});
	}

	//Join the threads with the main thread
	for (int i = 0; i < threadsCount; ++i) {
		t[i].join();
	}

	//Launch a group of threads
	const int timesToRead = 1;
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("\n\nStart reading in multiple threads\n\n");
#endif
	for (int i = 0; i < threadsCount; ++i) {
		t[i] = std::thread([i, &fileStorage, &directoryTree, &timesToRead]() {
			char parentDirectoryPath[50];
			snprintf(parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
			for (int k = 0; k < timesToRead; ++k) {
				//if (i == 0) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(50));
				//}
				testReadDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
			}
		});
	}

	//Join the threads with the main thread
	for (int i = 0; i < threadsCount; ++i) {
		t[i].join();
	}
}
#endif

#if (SPLITFAT_ENABLE_COMMON_MULTITHREAD_TESTS == 1)
/// Tests writing multiple files in transaction, in multithreading.
TEST_F(MultithreadingTest, TestMultithreadWritingAndReadingFilesInTransaction) {

	std::shared_ptr<SplitFATFileStorage> fileStorage = std::make_shared<SplitFATFileStorage>();
	createSplitFATFileStorage(*fileStorage);


	std::vector<FileSystemEntityDescription> directoryTree = {
		{"dir0", false},
		{"file0", true, 7238, 736},
		{"dir0/level1dir0", false},
		{"dir0/level1dir0/level2file0", true, 536873},
		{"dir0/level1dir0/level2file1", true, 0},
		{"dir0/level1dir0/level2dir0", false},
		{"file1", true, 36735, 543},
		{"dir1", false},
		{"file4", true, 7823, 83},
		{"file2", true, 23, 24},
		{"file3", true, 83, 74}
	};

	const int threadsCount = 10;
	std::thread t[threadsCount];
	const int timesToRead = 3;

	//Launch a group of threads to write different files simultaneously
#if (SPLITFAT_PRINT_LOCAL_LOG_INFO == 1)
	printf("\n\nStart writing in multiple threads\n\n");
#endif
	for (int i = 0; i < threadsCount; ++i) {
		int waitTime = rand() % 50;
		t[i] = std::thread([i, &fileStorage, &directoryTree, &timesToRead, waitTime]() {
			char parentDirectoryPath[50];
			snprintf(parentDirectoryPath, sizeof(parentDirectoryPath), "path%02u", i);
			std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

			bool createdTransaction = false;
			while (!createdTransaction) {
				fileStorage->tryStartTransaction(createdTransaction);
				if (!createdTransaction) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			}
			EXPECT_TRUE(createdTransaction);
			
			// Once the thread is onwer of a transaction, it should not be blocked, but also shouldn't be able to create another transaction.
			bool createdSecondTransaction = false;
			fileStorage->tryStartTransaction(createdSecondTransaction);
			EXPECT_FALSE(createdSecondTransaction);
			EXPECT_TRUE(fileStorage->isInTransaction());

			fileStorage->createDirectory(parentDirectoryPath);
			EXPECT_TRUE(fileStorage->directoryExists(parentDirectoryPath));
			createDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
			fileStorage->endTransaction();
			for (int k = 0; k < timesToRead; ++k) {
				testReadDirectoryTree(fileStorage, directoryTree, parentDirectoryPath);
			}
		});
	}

	//Join the threads with the main thread
	for (int i = 0; i < threadsCount; ++i) {
		t[i].join();
	}
}

#endif
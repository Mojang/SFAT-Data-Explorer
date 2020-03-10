/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include "SplitFAT/SplitFATFileSystem.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "WindowsSplitFATConfiguration.h"
#include "SplitFAT/FileDescriptorRecord.h"
#include "SplitFAT/VirtualFileSystem.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/Transaction.h"
#include <memory>
#include <random>
#include <chrono>

using namespace SFAT;

namespace {
	const char* kVolumeControlAndFATDataFilePath = "SFATControl.dat";
	const char* kClusterDataFilePath = "data.dat";
	const char* kTransactionFilePath = "_SFATTransaction.dat";
}

class DefragmentationUnitTest : public testing::Test {
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

ErrorCode writeFile(SFAT::FileHandle file, size_t targetFileSize, uint32_t id) {
	size_t maxBufferSize = 10ULL << 20; // 10MB
	std::vector<uint32_t> buffer;
	buffer.resize(maxBufferSize / sizeof(uint32_t), id);
	size_t totalWritten = 0ULL;
	while (totalWritten < targetFileSize) {
		size_t sizeToWrite = std::min(targetFileSize - totalWritten, maxBufferSize);
		size_t sizeWritten = 0ULL;
		ErrorCode err = file.writeAtPosition(buffer.data(), sizeToWrite, totalWritten, sizeWritten);
		totalWritten += sizeWritten;
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}
	}

	return ErrorCode::RESULT_OK;
}

ErrorCode readFile(SFAT::FileHandle file, size_t targetFileSize, uint32_t id) {
	size_t maxBufferSize = 10ULL << 20; // 10MB
	std::vector<uint32_t> buffer;
	buffer.resize(maxBufferSize / sizeof(uint32_t), static_cast<uint32_t>(-1));
	size_t totalRead = 0ULL;
	while (totalRead < targetFileSize) {
		size_t sizeToRead = std::min(targetFileSize - totalRead, maxBufferSize);
		size_t sizeRead = 0ULL;
		ErrorCode err = file.readAtPosition(buffer.data(), sizeToRead, totalRead, sizeRead);
		totalRead += sizeRead;
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		// While not enforsed, assuming that the targetFileSize, maxBufferSize and sizeRead are multiple of 4 bytes.
		size_t elementsCount = sizeRead / sizeof(uint32_t);
		for (size_t i = 0; i < elementsCount; ++i) {
			if (buffer[i] != id) {
				EXPECT_EQ(buffer[i], id);
				return ErrorCode::RESULT_OK;
			}
		}
	}

	return ErrorCode::RESULT_OK;
}

/// Tests creating a new file and writing to it.
TEST_F(DefragmentationUnitTest, OnCorrectTransactionDataShouldBeStored) {

	struct EntityDescription {
		EntityDescription(const char* szPath, bool isFile, size_t fileSize = 0, bool toBeDeleted = false)
			: mEntityName(szPath)
			, mIsFile(isFile)
			, mToBeDeleted(toBeDeleted)
			, mFileSize(fileSize) {
		}

		std::string mEntityName;
		bool mIsFile;
		bool mToBeDeleted = false;
		size_t mFileSize;
	};

	uint32_t countFiles = 0;
	size_t mb = 1ULL << 20;

	// First transaction
	// 250 MB of the first cluster-data block
	// More files and directories just to check that everything works with directories as well.
	std::vector<EntityDescription> transaction1_DirectoryTree = {
		{ "dir0", false },
		{ "file0", true, 180 * mb },
		{ "dir0/level1dir0", false },
		{ "dir0/level1dir0/level2file0", true, 5 * mb, true },
		{ "dir0/level1dir0/level2file1", true, 15 * mb },
		{ "dir0/level1dir0/level2dir0", false },
		{ "smallFileToBeDeleted", true, 10 * mb, true },
		{ "dir1", false },
		{ "file1", true, 40 * mb },
	};

	// Second transaction
	// 100 MB of the first cluster-data block
	std::vector<EntityDescription> transaction2_DirectoryTree = {
		{ "file100mb", true, 100 * mb },
	};

	//
	// Stage 1 start
	// - Create a SplitFAT file storage and populate it with data
	//
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		bool createdTransaction = false;
		EXPECT_FALSE(fileStorage->isInTransaction());
		fileStorage->tryStartTransaction(createdTransaction);
		EXPECT_TRUE(fileStorage->isInTransaction());
		EXPECT_TRUE(createdTransaction);

		// Create the directory tree
		for (auto&& entity : transaction1_DirectoryTree) {
			if (entity.mIsFile) {
				FileHandle file;

				ErrorCode err = fileStorage->openFile(file, entity.mEntityName.c_str(), "wb");
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				EXPECT_TRUE(fileStorage->fileExists(entity.mEntityName.c_str()));
				err = writeFile(file, entity.mFileSize, countFiles + 1);
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				err = file.close();
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				++countFiles;
			}
			else {
				fileStorage->createDirectory(entity.mEntityName.c_str());
				EXPECT_TRUE(fileStorage->directoryExists(entity.mEntityName.c_str()));
			}
		}

		// Remove whatever is marked for removal
		for (auto&& entity : transaction1_DirectoryTree) {
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

		fileStorage->endTransaction();
		EXPECT_FALSE(fileStorage->isInTransaction());


		//
		// Test read of all files
		//

		uint32_t countReadFiles = 0;
		for (auto&& entity : transaction1_DirectoryTree) {
			if (entity.mIsFile) {
				if (entity.mToBeDeleted) {
					EXPECT_FALSE(fileStorage->fileExists(entity.mEntityName.c_str()));
				}
				else {
					FileHandle file;

					ErrorCode err = fileStorage->openFile(file, entity.mEntityName.c_str(), "rb");
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					EXPECT_TRUE(fileStorage->fileExists(entity.mEntityName.c_str()));
					err = readFile(file, entity.mFileSize, countReadFiles + 1);
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
					err = file.close();
					EXPECT_EQ(err, ErrorCode::RESULT_OK);
				}
				++countReadFiles;
			}
		}
	} // Stage 1 end


	//
	// Stage 2 start
	// - Try to fill up the last 6MB of the previous block and also fill the gaps from the removed files
	//
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		bool createdTransaction = false;
		EXPECT_FALSE(fileStorage->isInTransaction());
		fileStorage->tryStartTransaction(createdTransaction);
		EXPECT_TRUE(fileStorage->isInTransaction());
		EXPECT_TRUE(createdTransaction);

		// Create the directory tree
		for (auto&& entity : transaction2_DirectoryTree) {
			if (entity.mIsFile) {
				FileHandle file;

				ErrorCode err = fileStorage->openFile(file, entity.mEntityName.c_str(), "wb");
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				EXPECT_TRUE(fileStorage->fileExists(entity.mEntityName.c_str()));
				err = writeFile(file, entity.mFileSize, countFiles + 1);
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				err = file.close();
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
				++countFiles;
			}
			else {
				fileStorage->createDirectory(entity.mEntityName.c_str());
				EXPECT_TRUE(fileStorage->directoryExists(entity.mEntityName.c_str()));
			}
		}

		fileStorage->endTransaction();
		EXPECT_FALSE(fileStorage->isInTransaction());

	} // Stage 2 end

}


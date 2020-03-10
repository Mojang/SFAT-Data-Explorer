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

class TransactionUnitTest : public testing::Test {
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


/// Tests creating a new file and writing to it.
TEST_F(TransactionUnitTest, OnCorrectTransactionDataShouldBeStored) {

	struct EntityDescription {
		EntityDescription(const char* szPath, bool isFile, bool toBeDeleted = false)
			: mEntityName(szPath)
			, mIsFile(isFile)
			, mToBeDeleted(toBeDeleted) {
		}

		std::string mEntityName;
		bool mIsFile;
		bool mToBeDeleted = false;
	};

	std::vector<EntityDescription> directoryTree = {
		{ "dir0", false },
		{ "file0", true },
		{ "dir0/level1dir0", false },
		{ "dir0/level1dir0/level2file0", true },
		{ "dir0/level1dir0/level2file1", true },
		{ "dir0/level1dir0/level2dir0", false },
		{ "file1", true },
		{ "dir1", false },
		{ "file1", true },
		{ "file2", true, true },
		{ "file3", true }
	};
	const char *szFilePath = "fileToDetete.bin";

	// First stage
	// - Create a SplitFAT file storage and populate it with data
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		EXPECT_FALSE(fileStorage->isInTransaction());
		bool createdTransaction = false;
		fileStorage->tryStartTransaction(createdTransaction);
		EXPECT_TRUE(createdTransaction);
		EXPECT_TRUE(fileStorage->isInTransaction());

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

		fileStorage->endTransaction();
		EXPECT_FALSE(fileStorage->isInTransaction());

		// Set of entities in the root
		std::set<std::string> allEntities;
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
		}

		std::set<std::string> entitiesFoundAlready;
		int recordIndex = 0;
		fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			EXPECT_NE(record.mEntityName[0], 0);
			EXPECT_FALSE(record.isDeleted());
			EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
			EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
			entitiesFoundAlready.insert(record.mEntityName);
			return ErrorCode::RESULT_OK;
		});

		EXPECT_TRUE(allEntities == entitiesFoundAlready);
	}


	// Second stage
	// - We should be able to read the same
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		EXPECT_FALSE(fileStorage->isInTransaction());

		// Set of entities in the root
		std::set<std::string> allEntities;
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
		}

		std::set<std::string> entitiesFoundAlready;
		int recordIndex = 0;
		fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			EXPECT_NE(record.mEntityName[0], 0);
			EXPECT_FALSE(record.isDeleted());
			EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
			EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
			entitiesFoundAlready.insert(record.mEntityName);
			return ErrorCode::RESULT_OK;
		});

		EXPECT_TRUE(allEntities == entitiesFoundAlready);
	}
}

/// Tests creating a new file and writing to it.
TEST_F(TransactionUnitTest, OnInterruptedTransactionDataShouldBeMissing) {

	struct EntityDescription {
		EntityDescription(const char* szPath, bool isFile, bool toBeDeleted = false)
			: mEntityName(szPath)
			, mIsFile(isFile)
			, mToBeDeleted(toBeDeleted) {
		}

		std::string mEntityName;
		bool mIsFile;
		bool mToBeDeleted = false;
	};

	std::vector<EntityDescription> directoryTree = {
		{ "dir0", false },
		{ "file0", true },
		{ "dir0/level1dir0", false },
		{ "dir0/level1dir0/level2file0", true },
		{ "dir0/level1dir0/level2file1", true },
		{ "dir0/level1dir0/level2dir0", false },
		{ "file1", true },
		{ "dir1", false },
		{ "file1", true },
		{ "file2", true, true },
		{ "file3", true }
	};
	const char *szFilePath = "fileToDetete.bin";

	// First stage
	// Create a SplitFAT file storage and populate it with data
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		EXPECT_FALSE(fileStorage->isInTransaction());
		bool createdTransaction = false;
		fileStorage->tryStartTransaction(createdTransaction);
		EXPECT_TRUE(createdTransaction);
		EXPECT_TRUE(fileStorage->isInTransaction());

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

		// Do not end the transaction here!
		EXPECT_TRUE(fileStorage->isInTransaction());

		//
		// The data should be still available in the current scope.

		// Set of entities in the root
		std::set<std::string> allEntities;
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
		}

		std::set<std::string> entitiesFoundAlready;
		int recordIndex = 0;
		fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			EXPECT_NE(record.mEntityName[0], 0);
			EXPECT_FALSE(record.isDeleted());
			EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
			EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
			entitiesFoundAlready.insert(record.mEntityName);
			return ErrorCode::RESULT_OK;
		});

		EXPECT_TRUE(allEntities == entitiesFoundAlready);
	}


	// Second stage
	// The data should be missing this time as we didn't commit the changes in the transaction.
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		EXPECT_FALSE(fileStorage->isInTransaction());

		// Set of entities in the root
		std::set<std::string> allEntities;
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
		}

		std::set<std::string> entitiesFoundAlready;
		int recordIndex = 0;
		fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			EXPECT_NE(record.mEntityName[0], 0);
			EXPECT_FALSE(record.isDeleted());
			EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
			EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
			entitiesFoundAlready.insert(record.mEntityName);
			return ErrorCode::RESULT_OK;
		});

		EXPECT_EQ(entitiesFoundAlready.size(), 0);
	}
}

/// Tests creating a new file and writing to it.
TEST_F(TransactionUnitTest, RestoreFromTransaction) {

	struct EntityDescription {
		EntityDescription(const char* szPath, bool isFile, bool toBeDeleted = false)
			: mEntityName(szPath)
			, mIsFile(isFile)
			, mToBeDeleted(toBeDeleted) {
		}

		std::string mEntityName;
		bool mIsFile;
		bool mToBeDeleted = false;
	};

	std::vector<EntityDescription> directoryTree = {
		{ "dir0", false },
		{ "file0", true },
		{ "dir0/level1dir0", false },
		{ "dir0/level1dir0/level2file0", true },
		{ "dir0/level1dir0/level2file1", true },
		{ "dir0/level1dir0/level2dir0", false },
		{ "file1", true },
		{ "dir1", false },
		{ "dir2", false },
		{ "dir2/level1dir2", false, true }, //Directory to be removed
		{ "dir2/level1dir2/level2file2", true, true }, //File to be deleted
		{ "file1", true },
		{ "file2", true, true }, //File to be deleted
		{ "file3", true }
	};
	const char *szFilePath = "fileToDetete.bin";

	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		// Start 
		EXPECT_FALSE(fileStorage->isInTransaction());
		bool createdTransaction = false;
		ErrorCode err = fileStorage->tryStartTransaction(createdTransaction);
		EXPECT_TRUE(createdTransaction);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_TRUE(fileStorage->isInTransaction());

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

		err = fileStorage->endTransaction();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_FALSE(fileStorage->isInTransaction());

		// Start second transaction
		//
		createdTransaction = false;
		err = fileStorage->tryStartTransaction(createdTransaction);
		EXPECT_TRUE(createdTransaction);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_TRUE(fileStorage->isInTransaction());

		// Remove all files marked for removal
		for (auto&& entity : directoryTree) {
			if (!entity.mToBeDeleted) {
				continue;
			}
			if (entity.mIsFile) {
				err = fileStorage->deleteFile(entity.mEntityName.c_str());
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
			}
		}

		// Remove all directories marked for removal
		for (auto&& entity : directoryTree) {
			if (!entity.mToBeDeleted) {
				continue;
			}
			if (!entity.mIsFile) {
				err = fileStorage->removeDirectory(entity.mEntityName.c_str());
				EXPECT_EQ(err, ErrorCode::RESULT_OK);
			}
		}

		// Set of entities in the root
		std::set<std::string> allEntities;
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
		}

		std::set<std::string> entitiesFoundAlready;
		int recordIndex = 0;
		fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			EXPECT_NE(record.mEntityName[0], 0);
			EXPECT_FALSE(record.isDeleted());
			EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
			EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
			entitiesFoundAlready.insert(record.mEntityName);
			return ErrorCode::RESULT_OK;
		});

		EXPECT_TRUE(allEntities == entitiesFoundAlready);

		// Instead of calling "err = fileStorage->endTransaction();",
		// call a private function that will flush the cached data, but will not delete the transaction file.
		// After that we close the storage. The next time we open the storage,
		// we should find first the transaction file and restore from it.
		err = fileStorage->getVirtualFileSystem().mVolumeManager.mTransaction._finalizeTransacion();
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_FALSE(fileStorage->isInTransaction());

	}


	// Second stage
	// Should reopen the storage and restore from the trannsaction file.
	{
		std::unique_ptr<SplitFATFileStorage> fileStorage = std::make_unique<SplitFATFileStorage>();
		createSplitFATFileStorage(*fileStorage);

		EXPECT_FALSE(fileStorage->isInTransaction());

		// Set of entities in the root
		std::set<std::string> allEntities;
		for (auto&& entity : directoryTree) {
			//if (entity.mToBeDeleted) {
			//	continue;
			//}

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
		}

		std::set<std::string> entitiesFoundAlready;
		int recordIndex = 0;
		fileStorage->iterateThroughDirectory("/", DI_ALL | DI_RECURSIVE, [&entitiesFoundAlready, &allEntities](bool& doQuit, const FileDescriptorRecord& record, const std::string& fullPath)->ErrorCode {
			EXPECT_NE(record.mEntityName[0], 0);
			EXPECT_FALSE(record.isDeleted());
			EXPECT_TRUE(allEntities.find(record.mEntityName) != allEntities.end());
			EXPECT_FALSE(entitiesFoundAlready.find(record.mEntityName) != entitiesFoundAlready.end());
			entitiesFoundAlready.insert(record.mEntityName);
			return ErrorCode::RESULT_OK;
		});

		EXPECT_TRUE(allEntities == entitiesFoundAlready);
	}
}


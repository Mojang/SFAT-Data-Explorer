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
	const char* kBlockVirtualizationOldFilePath = "blockVirtStable.dat";
	const char* kBlockVirtualizationNewFilePath = "blockVirtNew.dat";
}

class BlockVirtualizationUnitTest : public testing::Test {
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

	static VolumeManager& getVolumeManager(VirtualFileSystem &vfs) {
		return vfs.mVolumeManager;
	}

	static BlockVirtualization& getBlockVirtualization(VolumeManager& volumeManager) {
		return volumeManager.mBlockVirtualization;
	}
};


/// Tests creating a new file and writing to it.
TEST_F(BlockVirtualizationUnitTest, InitialCreation) {

	removeVolume();

	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = getVolumeManager(vfs);
		//volumeManager.getLowLevelFileAccess();

		BlockVirtualization& blockVirtualization = getBlockVirtualization(volumeManager);
//		blockVirtualization.setup();
		uint32_t blocksCount = volumeManager.getVolumeDescriptor().getMaxBlocksCount();
		uint32_t scratchBlockIndex = blockVirtualization.getScratchBlockIndex();

		// The scratchBlockIndex should point to the last allocated block. Note that currently we allocate all blocks at the start.
		EXPECT_EQ(scratchBlockIndex, 0);
		
		// The virtual block indices should match the physical ones, because the Block Virtualization was just initialized.
		for (uint32_t i = volumeManager.getFirstFileDataBlockIndex(); i < blocksCount; ++i) {
			uint32_t blockIndex = blockVirtualization.getPhysicalBlockIndex(i - volumeManager.getFirstFileDataBlockIndex());
			EXPECT_EQ(blockIndex, i);
		}
	}

	// Open it again
	{
		VirtualFileSystem vfs;
		createVirtualFileSystem(vfs);

		VolumeManager &volumeManager = getVolumeManager(vfs);
		//volumeManager.getLowLevelFileAccess();

		BlockVirtualization& blockVirtualization = getBlockVirtualization(volumeManager);
//		blockVirtualization.setup();
		uint32_t blocksCount = volumeManager.getVolumeDescriptor().getMaxBlocksCount();
		uint32_t scratchBlockIndex = blockVirtualization.getScratchBlockIndex();

		// The scratchBlockIndex should still point to the last allocated block.
		EXPECT_EQ(scratchBlockIndex, 0);

		// The virtual block indices should match the physical ones, because the Block Virtualization was just initialized.
		for (uint32_t i = volumeManager.getFirstFileDataBlockIndex(); i < blocksCount; ++i) {
			uint32_t blockIndex = blockVirtualization.getPhysicalBlockIndex(i - volumeManager.getFirstFileDataBlockIndex());
			EXPECT_EQ(blockIndex, i);
		}
	}
}


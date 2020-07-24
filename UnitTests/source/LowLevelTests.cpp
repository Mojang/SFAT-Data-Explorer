/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include "SplitFAT/Common.h"
#include "SplitFAT/DataBlockManager.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/utils/CRC.h"
#include "WindowsSplitFATConfiguration.h"
#include <memory>

using namespace SFAT;

namespace {
	const char* kVolumeControlAndFATDataFilePath = "SFATControl.dat";
	const char* kClusterDataFilePath = "data.dat";
	const char* kTransactionFilePath = "";
}


class LowLevelUnitTest : public testing::Test {
protected:  // You should make the members protected s.t. they can be
			// accessed from sub-classes.

	// virtual void SetUp() will be called before each test is run.  You
	// should define it if you need to initialize the variables.
	// Otherwise, this can be skipped.
	virtual void SetUp() override {
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		ErrorCode err = volumeManager.setup(lowLevelFileAccess);
		volumeManager.removeVolume();
	}

	// virtual void TearDown() will be called after each test is run.
	// You should define it if there is cleanup work to do.  Otherwise,
	// you don't have to provide it.
	//
	virtual void TearDown() override {
	}

	template <typename T>
	void fillWithRandomNumbers(T& data) {
		size_t size = sizeof(T);
		uint8_t* byteBuffer = reinterpret_cast<uint8_t*>(&data);
		for (size_t i = 0; i < size; ++i) {
			byteBuffer[i] = static_cast<uint8_t>(rand());
		}
	}

	void fillWithRandomNumbers(void *data, size_t sizeInBytes) {
		uint8_t* byteBuffer = reinterpret_cast<uint8_t*>(data);
		for (size_t i = 0; i < sizeInBytes; ++i) {
			byteBuffer[i] = static_cast<uint8_t>(rand());
		}
	}

	template <typename T>
	void fillWithAllDifferentNumbers(T& dest, const T& source) {
		size_t size = sizeof(T);
		uint8_t* destBuffer = reinterpret_cast<uint8_t*>(&dest);
		const uint8_t* srcBuffer = reinterpret_cast<const uint8_t*>(&source);
		for (size_t i = 0; i < size; ++i) {
			destBuffer[i] = srcBuffer[i] ^ 0xff;
		}
	}

	template <typename T>
	bool compare(const T& d0, const T& d1) {
		size_t size = sizeof(T);
		const uint8_t* byteBuffer0 = reinterpret_cast<const uint8_t*>(&d0);
		const uint8_t* byteBuffer1 = reinterpret_cast<const uint8_t*>(&d1);
		for (size_t i = 0; i < size; ++i) {
			if (byteBuffer0[i] != byteBuffer1[i]) {
				return false;
			}
		}

		return true;
	}

};

/// Tests the VolumeDescriptor's default c'tor.
TEST_F(LowLevelUnitTest, VolumeDescriptorDefaultConstructor) {
	// You can access data in the test fixture here.
	VolumeDescriptor volumeDescriptor;

	EXPECT_EQ(volumeDescriptor.getVerificationCode(), 0);
	EXPECT_FALSE(volumeDescriptor.isInitialized());

	volumeDescriptor.initializeWithDefaults();
	EXPECT_EQ(volumeDescriptor.getVerificationCode(), 0x5FA7C0DE);
	EXPECT_TRUE(volumeDescriptor.isInitialized());
}

/// Tests the VolumeDescriptor initializeWithDefaults() function
TEST_F(LowLevelUnitTest, VolumeDescriptorDefaultInitialization) {
	// You can access data in the test fixture here.
	VolumeDescriptor vd0, vd1;
	srand(0);
	fillWithRandomNumbers(vd0);
	fillWithAllDifferentNumbers(vd1, vd0);

	EXPECT_FALSE(vd0.compare(vd1));
	EXPECT_FALSE(compare(vd0, vd1));

	vd0.initializeWithDefaults();
	EXPECT_FALSE(vd0.compare(vd1));
	EXPECT_FALSE(compare(vd0, vd1));

	//Do we initialize everything
	vd1.initializeWithDefaults();
	EXPECT_TRUE(compare(vd0, vd1));
	EXPECT_TRUE(vd0.compare(vd1)); // Check if we test everything with our compare function.
}


/// Tests the VolumeDescriptor initializeWithTestValues() function
TEST_F(LowLevelUnitTest, VolumeDescriptorTestInitialization) {
	// You can access data in the test fixture here.
	VolumeDescriptor vd0, vd1;
	srand(0);
	fillWithRandomNumbers(vd0);
	fillWithAllDifferentNumbers(vd1, vd0);

	EXPECT_FALSE(vd0.compare(vd1));
	EXPECT_FALSE(compare(vd0, vd1));

	vd0.initializeWithTestValues();
	EXPECT_FALSE(vd0.compare(vd1));
	EXPECT_FALSE(compare(vd0, vd1));

	//Do we initialize everything
	vd1.initializeWithTestValues();
	EXPECT_TRUE(compare(vd0, vd1));
	EXPECT_TRUE(vd0.compare(vd1)); // Check if we test everything with our compare function.
}

/// Tests the the data-files are created on the physical storage.
TEST_F(LowLevelUnitTest, DataFileCreation) {
	std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
	lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
	VolumeManager volumeManager;
	volumeManager.setup(lowLevelFileAccess);

	EXPECT_FALSE(volumeManager.fatDataFileExists());
	EXPECT_FALSE(volumeManager.clusterDataFileExists());

	volumeManager.createVolume();

	EXPECT_TRUE(volumeManager.fatDataFileExists());
	EXPECT_TRUE(volumeManager.clusterDataFileExists());
}

/// Tests a block allocation
TEST_F(LowLevelUnitTest, BlockAllocation) {

	std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
	lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
	VolumeManager volumeManager;
	volumeManager.setup(lowLevelFileAccess);

	volumeManager.createVolume();
	EXPECT_EQ(volumeManager.getCountAllocatedDataBlocks(), 0);
	ErrorCode err = volumeManager.allocateBlockByIndex(0);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(volumeManager.getCountAllocatedDataBlocks(), 1);
	EXPECT_EQ(volumeManager.getCountAllocatedFATBlocks(), 1);

	err = volumeManager.allocateBlockByIndex(1);
	EXPECT_EQ(err, ErrorCode::RESULT_OK);
	EXPECT_EQ(volumeManager.getCountAllocatedDataBlocks(), 2);
	EXPECT_EQ(volumeManager.getCountAllocatedFATBlocks(), 2);

	// The following should give an error
	err = volumeManager.allocateBlockByIndex(volumeManager.getMaxPossibleBlocksCount());
	EXPECT_EQ(err, ErrorCode::ERROR_VOLUME_CAN_NOT_EXPAND);
	EXPECT_EQ(volumeManager.getCountAllocatedDataBlocks(), 2);
	EXPECT_EQ(volumeManager.getCountAllocatedFATBlocks(), 2);
}

TEST_F(LowLevelUnitTest, VolumeDescriptorReadWrite) {

	VolumeDescriptor vdCopy;

	// Check first if it saves the default values.
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();

		const VolumeDescriptor& vd = volumeManager.getVolumeDescriptor();
		vdCopy = vd;

		EXPECT_TRUE(compare(vd, vdCopy));
	}

	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.openVolume();

		const VolumeDescriptor& vd = volumeManager.getVolumeDescriptor();

		EXPECT_TRUE(compare(vd, vdCopy));
	}

	// Now to be sure the data is really written and loaded, save and read something more random
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();

		const VolumeDescriptor& cvd = volumeManager.getVolumeDescriptor();
		VolumeDescriptor& vd = const_cast<VolumeDescriptor&>(cvd);
		vd.initializeWithTestValues();
		EXPECT_FALSE(compare(vd, vdCopy));

		vdCopy = vd;
		EXPECT_TRUE(compare(vd, vdCopy));

		volumeManager._writeVolumeDescriptor();
	}

	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.openVolume();

		const VolumeDescriptor& vd = volumeManager.getVolumeDescriptor();

		EXPECT_TRUE(compare(vd, vdCopy));
		//EXPECT_TRUE(vd.compare(vdCopy));
	}
}

/// Tests that setting a FAT cell executes without an error and actually allocates the first block.
TEST_F(LowLevelUnitTest, SetFATCell) {
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 0);
		volumeManager.setFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, FATCellValueType::singleElementClusterChainValue());
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 1);
	}
}

/*
* Tests that setting a FAT cell writes the data physically on the storage.
* It could be then read back when the Volume is closed and reopened.
*/
TEST_F(LowLevelUnitTest, GetFATCell) {
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 0);
		FATCellValueType cellValue = FATCellValueType::singleElementClusterChainValue();
		volumeManager.setFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, cellValue);
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 1);
	}

	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.openVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 1);
		FATCellValueType cellValue = FATCellValueType::badCellValue();
		volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, cellValue);
		EXPECT_EQ((cellValue.getRawNext() & ClusterValues::CHAIN_START_END_MASK), ClusterValues::END_OF_CHAIN);
		EXPECT_EQ((cellValue.getRawPrev() & ClusterValues::CHAIN_START_END_MASK), ClusterValues::START_OF_CHAIN);
		EXPECT_TRUE(cellValue.isStartOfChain());
		EXPECT_TRUE(cellValue.isEndOfChain());
	}
}

/*
 * Tests that setting a FAT cell writes the data physically on the storage. 
 * It could be then read back when the Volume is closed and reopened.
 * In this case we set and get cell values only in the first allocated block.
 */
TEST_F(LowLevelUnitTest, SetGetMoreFATCells) {
	uint32_t lastClusterIndexInCurrentBlock = 1;

	// Initial creation of the Volume
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 0);

		volumeManager.setFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, FATCellValueType::singleElementClusterChainValue());
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		FATCellValueType value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain());
		EXPECT_TRUE(value.isStartOfChain());

		lastClusterIndexInCurrentBlock = volumeManager.getVolumeDescriptor().getClustersPerFATBlock() - 1;
		value.setNext( lastClusterIndexInCurrentBlock );
		value.makeStartOfChain();
		volumeManager.setFATCell(0x00000001, value);
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(0x00000001, value);
		EXPECT_EQ(value.getNext(), lastClusterIndexInCurrentBlock);

		EXPECT_EQ((value.getRawPrev() & ClusterValues::CHAIN_START_END_MASK) , ClusterValues::START_OF_CHAIN);
		EXPECT_TRUE(value.isStartOfChain());
		EXPECT_FALSE(value.isEndOfChain());
	}

	// Open the already created before Volume
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.openVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		FATCellValueType value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain());
		EXPECT_TRUE(value.isStartOfChain());

		value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(0x00000001, value);
		EXPECT_EQ(value.getNext(), lastClusterIndexInCurrentBlock);
		EXPECT_TRUE(value.isStartOfChain());
	}
}

/// Tests the reading and writing of FAT cells, including the allocation of new block when necessary.
TEST_F(LowLevelUnitTest, SetGetFATCellsInMoreBlocks) {
	uint32_t firstClusterIndexInFirstDataBlock = 1;
	uint32_t firstClusterIndexInSecondDataBlock = 1;

	// Initial creation of the Volume
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 0);
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 0);

		// Create root	
		volumeManager.setFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, FATCellValueType::singleElementClusterChainValue());
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1);
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 1);
		FATCellValueType value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain());
		EXPECT_TRUE(value.isEndOfChain());

		// Note that a FAT block should be allocated either for the cell to be available, or for the nextCell pointed from the value.
		// In the code below, we set FAT cell 0x00000001 only, but the value.next is changed.
		firstClusterIndexInFirstDataBlock = volumeManager.getFirstFileDataClusterIndex();
		value.setNext(firstClusterIndexInFirstDataBlock);
		value.makeStartOfChain();
		volumeManager.setFATCell(0x00000001, value);
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 1 + volumeManager.getFirstFileDataBlockIndex());
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 1 + volumeManager.getFirstFileDataBlockIndex());
		value = FATCellValueType::badCellValue();;
		volumeManager.getFATCell(0x00000001, value);
		EXPECT_EQ(value.getNext(), firstClusterIndexInFirstDataBlock);
		EXPECT_TRUE(value.isStartOfChain());

		firstClusterIndexInSecondDataBlock = volumeManager.getFirstFileDataClusterIndex() + volumeManager.getVolumeDescriptor().getClustersPerFATBlock();
		value.setNext(firstClusterIndexInSecondDataBlock);
		value.makeStartOfChain();
		volumeManager.setFATCell(0x00000001, value);
		countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 2 + volumeManager.getFirstFileDataBlockIndex());
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 2 + volumeManager.getFirstFileDataBlockIndex());
		value = FATCellValueType::badCellValue();;
		volumeManager.getFATCell(0x00000001, value);
		EXPECT_EQ(value.getNext(), firstClusterIndexInSecondDataBlock);
		EXPECT_TRUE(value.isStartOfChain());
	}

	// Open the already created before Volume
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.openVolume();
		uint32_t countBlocks = volumeManager.getCountAllocatedFATBlocks();
		EXPECT_EQ(countBlocks, 2 + volumeManager.getFirstFileDataBlockIndex());
		countBlocks = volumeManager.getCountAllocatedDataBlocks();
		EXPECT_EQ(countBlocks, 2 + volumeManager.getFirstFileDataBlockIndex());
		FATCellValueType value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(ClusterValues::ROOT_START_CLUSTER_INDEX, value);
		EXPECT_TRUE(value.isEndOfChain());
		EXPECT_TRUE(value.isStartOfChain());

		value = FATCellValueType::badCellValue();
		volumeManager.getFATCell(0x00000001, value);
		EXPECT_EQ(value.getNext(), firstClusterIndexInSecondDataBlock);
		EXPECT_TRUE(value.isStartOfChain());
	}
}

/// Tests cluster read/write operations in the cluster-data storage.
TEST_F(LowLevelUnitTest, ClusterWriteRead) {

	std::vector<uint8_t> buffer0;
	std::vector<uint8_t> buffer1;
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.createVolume();

		size_t clusterSize = static_cast<size_t>(volumeManager.getVolumeDescriptor().getClusterSize());
		buffer0.resize(clusterSize);
		buffer1.resize(clusterSize);

		EXPECT_EQ(volumeManager.getCountAllocatedDataBlocks(), 0);
		ErrorCode err = volumeManager.allocateBlockByIndex(0);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_EQ(volumeManager.getCountAllocatedDataBlocks(), 1);
		EXPECT_EQ(volumeManager.getCountAllocatedFATBlocks(), 1);

		FATCellValueType rootCellValue = FATCellValueType::singleElementClusterChainValue();
		err = volumeManager.setFATCell(0, rootCellValue);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		fillWithRandomNumbers(&buffer0[0], clusterSize);
		err = volumeManager.writeCluster(buffer0, 0);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);

		uint32_t calculatedCRC = SFAT::CRC16::calculate(buffer0.data(), clusterSize);
		err = volumeManager.readCluster(buffer1, 0);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		buffer1[0] ^= 0xff;
		EXPECT_FALSE(buffer0 == buffer1);

		err = volumeManager.readCluster(buffer1, 0);
		EXPECT_EQ(err, ErrorCode::RESULT_OK);
		EXPECT_TRUE(buffer0 == buffer1);
	}

	// Check what is written/read after reopening of the volume
	{
		std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
		lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
		VolumeManager volumeManager;
		volumeManager.setup(lowLevelFileAccess);

		volumeManager.openVolume();

		size_t clusterSize = static_cast<size_t>(volumeManager.getVolumeDescriptor().getClusterSize());
		buffer1.resize(clusterSize);

		volumeManager.readCluster(buffer1, 0);
		EXPECT_TRUE(buffer0 == buffer1);
	}

}

/// Tests cluster read/write operations in the cluster-data storage.
TEST_F(LowLevelUnitTest, EncodingAndDecodingCRCInFATCell) {
	std::shared_ptr<WindowsSplitFATConfiguration> lowLevelFileAccess = std::make_shared<WindowsSplitFATConfiguration>();
	lowLevelFileAccess->setup(kVolumeControlAndFATDataFilePath, kClusterDataFilePath, kTransactionFilePath);
	VolumeManager volumeManager;
	volumeManager.setup(lowLevelFileAccess);
	uint32_t clusterSize = volumeManager.getClusterSize();
	uint32_t recordSize = volumeManager.getFileDescriptorRecordStorageSize();
	uint32_t recordsPerCluster = clusterSize / recordSize;
	ClusterIndexType lastCluster = volumeManager.getCountTotalClusters() - 1;


	std::vector<FATCellValueType> cellValueArray;
	FATCellValueType cellValue = FATCellValueType::invalidCellValue();
	//cellValueArray.push_back(cellValue);
	cellValue = FATCellValueType::freeCellValue();
	cellValueArray.push_back(cellValue);

	// Start of chain only
	{
		cellValue.makeStartOfChain();
		cellValue.encodeFileDescriptorLocation(0, 0);
		cellValue.setNext(0);
		cellValueArray.push_back(cellValue);
		cellValue.setNext(lastCluster);
		cellValueArray.push_back(cellValue);
		cellValue.setNext(lastCluster & 0xAAAAAAAA);
		cellValue.encodeFileDescriptorLocation(1, 2);
		cellValue.setNext(lastCluster & 0x55555555);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(2, 1);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(15, recordsPerCluster - 1);
	}

	// End of chain only
	{
		cellValue.makeEndOfChain();
		cellValue.encodeFileDescriptorLocation(0, 0);
		cellValue.setPrev(0);
		cellValueArray.push_back(cellValue);
		cellValue.setPrev(lastCluster);
		cellValueArray.push_back(cellValue);
		cellValue.setPrev(lastCluster & 0xAAAAAAAA);
		cellValue.encodeFileDescriptorLocation(1, 2);
		cellValue.setPrev(lastCluster & 0x55555555);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(2, 1);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(15, recordsPerCluster - 1);
	}

	// In the middle of the chain
	{
		cellValue = FATCellValueType::freeCellValue();
		cellValue.setPrev(0);
		cellValue.setNext(1);
		cellValueArray.push_back(cellValue);
		cellValue.setPrev(1);
		cellValue.setNext(0);
		cellValueArray.push_back(cellValue);

		cellValue.setPrev(lastCluster & 0xAAAAAAAA);
		cellValue.setNext(lastCluster);
		cellValueArray.push_back(cellValue);
		cellValue.setPrev(lastCluster & 0x55555555);
		cellValue.setNext(lastCluster);
		cellValueArray.push_back(cellValue);

		cellValue.setNext(lastCluster & 0xAAAAAAAA);
		cellValue.setPrev(lastCluster);
		cellValueArray.push_back(cellValue);
		cellValue.setNext(lastCluster & 0x55555555);
		cellValue.setPrev(lastCluster);
		cellValueArray.push_back(cellValue);
	}

	// Start/End of chain
	{
		cellValue.makeStartOfChain();
		cellValue.makeEndOfChain();
		cellValue.encodeFileDescriptorLocation(0, 0);
		cellValueArray.push_back(cellValue);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(1, 2);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(2, 1);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(15, recordsPerCluster - 1);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(0xA, (recordsPerCluster - 1) & 0x55555555);
		cellValueArray.push_back(cellValue);
		cellValue.encodeFileDescriptorLocation(0x5, (recordsPerCluster - 1) & 0xAAAAAAAA);
	}

	//Testing
	using CRCType = decltype(cellValue.decodeCRC());
	uint32_t countCRCs = 1 << ClusterValues::CRC_BIT_COUNT;
	for (size_t k = 0; k < cellValueArray.size(); ++k) {
		for (uint32_t i = 0; i < countCRCs; ++i) {
			cellValue = cellValueArray[k];
			cellValue.encodeCRC(static_cast<CRCType>(i));
			CRCType readCRC = cellValue.decodeCRC();
			EXPECT_EQ(static_cast<CRCType>(i), readCRC);
			EXPECT_TRUE(cellValue.isCRCInitialized());
			//Check if other peroperties have changes
			EXPECT_EQ(cellValue.isStartOfChain(), cellValueArray[k].isStartOfChain());
			if (cellValueArray[k].isEndOfChain() || cellValueArray[k].isStartOfChain()) {
				ClusterIndexType ci0, ci1;
				uint32_t r0, r1;
				cellValue.decodeFileDescriptorLocation(ci0, r0);
				cellValueArray[k].decodeFileDescriptorLocation(ci1, r1);
				EXPECT_EQ(ci0, ci1);
				EXPECT_EQ(r0, r1);
			} 
			else if (cellValueArray[k].isEndOfChain()) {
				ClusterIndexType ci0, ci1;
				ci0 = cellValue.getPrev();
				ci1 = cellValueArray[k].getPrev();
				EXPECT_EQ(ci0, ci1);
			}
			else if (cellValueArray[k].isStartOfChain()) {
				ClusterIndexType ci0, ci1;
				ci0 = cellValue.getNext();
				ci1 = cellValueArray[k].getNext();
				EXPECT_EQ(ci0, ci1);
			}
		}
	}

	for (uint32_t i = 0; i < countCRCs; ++i) {
		cellValue = FATCellValueType::freeCellValue();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setNext(0);
		CRCType readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());

		cellValue = FATCellValueType::freeCellValue();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setPrev(0);
		readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());

		cellValue = FATCellValueType::freeCellValue();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setNext(0);
		readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());

		cellValue = FATCellValueType::freeCellValue();
		cellValue.makeStartOfChain();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setNext(0);
		readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());

		cellValue = FATCellValueType::freeCellValue();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.makeStartOfChain();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeFileDescriptorLocation(0, 0); //With descriptor location change
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setNext(0);
		readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());

		cellValue = FATCellValueType::freeCellValue();
		cellValue.makeEndOfChain();
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setPrev(0);
		readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());

		cellValue = FATCellValueType::freeCellValue();
		cellValue.makeEndOfChain();
		cellValue.encodeFileDescriptorLocation(0, 0); //With descriptor location change
		EXPECT_FALSE(cellValue.isCRCInitialized());
		cellValue.encodeCRC(static_cast<CRCType>(i));
		cellValue.setPrev(0);
		readCRC = cellValue.decodeCRC();
		EXPECT_EQ(static_cast<CRCType>(i), readCRC);
		EXPECT_TRUE(cellValue.isCRCInitialized());
	}
}

//TODO: Implement
TEST_F(LowLevelUnitTest, AllocateCluster) {
	// Test the cluster allocation
	// How it allocates a block if necessary
	// How the number of free/total available/allocated clusters is tracked in the VolumeControlData and the corresponding BlockControlData?
	
	EXPECT_TRUE(true);
}
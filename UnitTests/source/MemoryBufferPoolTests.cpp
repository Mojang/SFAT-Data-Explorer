/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include <gtest/gtest.h>
#include "SplitFAT/utils/MemoryBufferPool.h"
#include <memory>
#include <random>
#include <chrono>
#include <thread>
#include <iostream>

using namespace SFAT;

class MemoryBufferPoolTest : public testing::Test {
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
};

/// Tests the creation of MemoryBufferPool.
TEST_F(MemoryBufferPoolTest, MemoryBuffer_Create) {

	MemoryBufferPool memoryBufferPool(2, 8192, 5);
}

/// Tests the creation of MemoryBufferPool and acquiring resources from it.
TEST_F(MemoryBufferPoolTest, AcquireAndReleaseResources) {

	size_t requiredByteSize = 8192;
	MemoryBufferPool memoryBufferPool(2, requiredByteSize, 5);

	EXPECT_EQ(memoryBufferPool.getCountFree(), 2);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);

	{
		auto handle = memoryBufferPool.acquireBuffer();
		EXPECT_EQ(handle->get().size(), requiredByteSize);

		//One is in use, one should be free
		EXPECT_EQ(memoryBufferPool.getCountFree(), 1);
		EXPECT_EQ(memoryBufferPool.getCountInUse(), 1);
	}

	//Both resource blocks should be free again, 0 are used
	EXPECT_EQ(memoryBufferPool.getCountFree(), 2);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);
}

/// Tests the acquiring of all preallocated resources from it.
TEST_F(MemoryBufferPoolTest, AcquireAllPreallocatedResources) {

	size_t requiredByteSize = 8192;
	MemoryBufferPool memoryBufferPool(2, requiredByteSize, 5);

	EXPECT_EQ(memoryBufferPool.getCountFree(), 2);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);

	{
		auto handle0 = memoryBufferPool.acquireBuffer();
		EXPECT_EQ(handle0->get().size(), requiredByteSize);
		auto handle1 = memoryBufferPool.acquireBuffer();
		EXPECT_EQ(handle1->get().size(), requiredByteSize);

		//One is in use, one should be free
		EXPECT_EQ(memoryBufferPool.getCountFree(), 0);
		EXPECT_EQ(memoryBufferPool.getCountInUse(), 2);
	}

	//Both resource blocks should be free again, 0 are used
	EXPECT_EQ(memoryBufferPool.getCountFree(), 2);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);
}

void TestIfResourcesAreDifferent(std::vector<std::unique_ptr<SFAT::MemoryBufferHandle>>& handles) {
	size_t size = handles.size();
	for (size_t i = 0; i < size; ++i) {
		for (size_t j = i+1; j < size; ++j) {
			EXPECT_TRUE(handles[i]->get().data() != handles[j]->get().data());
		}
	}
}

TEST_F(MemoryBufferPoolTest, AcquireAllThatWillKeepAllocated) {

	size_t requiredByteSize = 8192;
	const size_t countBuffersToPreallocate = 2;
	const size_t countBuffersToKeepAllocated = 5;
	MemoryBufferPool memoryBufferPool(countBuffersToPreallocate, requiredByteSize, countBuffersToKeepAllocated);

	EXPECT_EQ(memoryBufferPool.getCountFree(), 2);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);

	{
		std::vector<std::unique_ptr<SFAT::MemoryBufferHandle>> handles(countBuffersToKeepAllocated);
		for (size_t i = 0; i < countBuffersToKeepAllocated; ++i) {
			handles[i] = memoryBufferPool.acquireBuffer();
			EXPECT_EQ(handles[i]->get().size(), requiredByteSize);
			if (i < countBuffersToPreallocate) {
				EXPECT_EQ(memoryBufferPool.getCountFree(), countBuffersToPreallocate - i - 1);
			}
			else {
				EXPECT_EQ(memoryBufferPool.getCountFree(), 0);
			}
			EXPECT_EQ(memoryBufferPool.getCountInUse(), i + 1);
		}

		TestIfResourcesAreDifferent(handles);
	}

	//Both resource blocks should be free again, 0 are used
	EXPECT_EQ(memoryBufferPool.getCountFree(), countBuffersToKeepAllocated);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);
}

TEST_F(MemoryBufferPoolTest, AcquireMoreThanWhatWillKeepAllocated) {

	size_t requiredByteSize = 8192;
	const int countBuffersToPreallocate = 2;
	const int countBuffersToKeepAllocated = 5;
	MemoryBufferPool memoryBufferPool(countBuffersToPreallocate, requiredByteSize, countBuffersToKeepAllocated);

	EXPECT_EQ(memoryBufferPool.getCountFree(), 2);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);

	{
		const int countToAcquire = 50;
		std::vector<std::unique_ptr<SFAT::MemoryBufferHandle>> handles(countToAcquire);
		for (size_t i = 0; i < countToAcquire; ++i) {
			handles[i] = memoryBufferPool.acquireBuffer();
			EXPECT_EQ(handles[i]->get().size(), requiredByteSize);
			if (i < countBuffersToPreallocate) {
				EXPECT_EQ(memoryBufferPool.getCountFree(), countBuffersToPreallocate - i - 1);
			}
			else {
				EXPECT_EQ(memoryBufferPool.getCountFree(), 0);
			}
			EXPECT_EQ(memoryBufferPool.getCountInUse(), i + 1);
		}

		TestIfResourcesAreDifferent(handles);
	}

	//Both resource blocks should be free again, 0 are used
	EXPECT_EQ(memoryBufferPool.getCountFree(), countBuffersToKeepAllocated);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);
}

TEST_F(MemoryBufferPoolTest, AcquireInMultithreading) {

	size_t requiredByteSize = 8192;
	const size_t countBuffersToPreallocate = 2;
	const size_t countBuffersToKeepAllocated = 5;
	MemoryBufferPool memoryBufferPool(countBuffersToPreallocate, requiredByteSize, countBuffersToKeepAllocated);

	EXPECT_EQ(memoryBufferPool.getCountFree(), countBuffersToPreallocate);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);

	const size_t threadsCount = countBuffersToKeepAllocated + 10;
	const size_t countToAcquire = threadsCount;
	const size_t timesAllocationsPerThread = 200;
	std::thread t[threadsCount];

	{
		std::vector<std::unique_ptr<SFAT::MemoryBufferHandle>> handles(countToAcquire);
		for (size_t threadIndex = 0; threadIndex < threadsCount; ++threadIndex) {
			int seed = rand();
			auto& handle = handles[threadIndex];
			t[threadIndex] = std::thread([&handle, &memoryBufferPool, seed, timesAllocationsPerThread, threadIndex, requiredByteSize]() {
				std::mt19937 mt_rand(seed);
				for (size_t k = 0; k < timesAllocationsPerThread; ++k) {
					if (mt_rand() % 3 != 0) {
						std::this_thread::sleep_for(std::chrono::milliseconds(mt_rand() % 50));
					}
					handle = memoryBufferPool.acquireBuffer();
					auto& buffer = handle->get();
					EXPECT_EQ(buffer.size(), requiredByteSize);
					for (size_t i = 0; i < requiredByteSize; ++i) {
						buffer[i] = static_cast<uint8_t>(threadIndex);
					}
					if (mt_rand() % 3 != 0) {
						std::this_thread::sleep_for(std::chrono::milliseconds(mt_rand() % 50));
					}
					for (size_t i = 0; i < requiredByteSize; ++i) {
						EXPECT_EQ(buffer[i], static_cast<uint8_t>(threadIndex));
					}
				}
			});
		}

		//Join the threads with the main thread
		for (size_t i = 0; i < threadsCount; ++i) {
			t[i].join();
		}
	}

	//Both resource blocks should be free again, 0 are used
	EXPECT_EQ(memoryBufferPool.getCountFree(), countBuffersToKeepAllocated);
	EXPECT_EQ(memoryBufferPool.getCountInUse(), 0);
}

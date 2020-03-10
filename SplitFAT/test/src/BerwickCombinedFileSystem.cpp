/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#if defined(MCPE_PLATFORM_ORBIS)

#include "Core/Platform/orbis/file/sfat/BerwickCombinedFileSystem.h"
#include "Core/Platform/orbis/file/sfat/BerwickDataPlacementStrategy.h"
#include "Core/Debug/DebugUtils.h"
#include "Core/Debug/Log.h"
#include <stdio.h>
#include <algorithm>

#include <kernel.h>
#include <sys/dmem.h>
#include <app_content.h>
#include <libsysmodule.h>
#include <vector>

#else

#include "BerwickCombinedFileSystem.h"
#include "BerwickDataPlacementStrategy.h"
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <stdint.h>

#endif

namespace Core { namespace SFAT {

	namespace {
		const uint32_t kInvalidDataBlockIndex = static_cast<uint32_t>(-1);
		const uint32_t kInvalidChunkIndex = static_cast<uint32_t>(-1);
		const uint32_t kInternalBufferSize = 256 << 10; // 256KB
	}

	/*************************************************************************************
		BerwickCombinedFile implementation
	*************************************************************************************/

	BerwickCombinedFile::BerwickCombinedFile(BerwickCombinedFileStorage& fileStorage)
		: ::SFAT::FileBase(fileStorage)
		, mFileLW(*fileStorage.mBerwickFileStorageLargeWrites)
		, mDirectoriesDataFile(*fileStorage.mBerwickFileStorage)
		, mCachedBlockIndex(kInvalidDataBlockIndex)
		, mIsCacheInSync(false)
		, mDirectoryDataFilePath(fileStorage.mDirectoryDataFilePath)
		, mCountWrittenClusters(0)
		, mBlockOptimizationPerformed(false) {
	}

	BerwickCombinedFile::~BerwickCombinedFile() {
		::SFAT::ErrorCode err = flush();
		DEBUG_ASSERT(err == ::SFAT::ErrorCode::RESULT_OK, "The final flush before closing the storage failed!");
	}

	bool BerwickCombinedFile::isOpen() const {
		return mFileLW.isOpen();
	}

	::SFAT::ErrorCode BerwickCombinedFile::open(const char *szFilePath, uint32_t accessMode) {

		SceKernelStat status;
		memset(&status, 0, sizeof(SceKernelStat));
		int res = sceKernelStat(szFilePath, &status);
		if ((res < 0) && (res != SCE_KERNEL_ERROR_ENOENT)){
			ALOGE(LOG_AREA_FILE, "Can't get the file status for \"%s\"! Error code #%8X", szFilePath, res);
			return ::SFAT::ErrorCode::ERROR_GETTING_FILE_STATUS;
		}

		if ((res == SCE_KERNEL_ERROR_ENOENT) || (status.st_size == 0)) {
			// We need to allocate initial block
			::SFAT::ErrorCode err = _initialBlockAllocation(szFilePath);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}

			// Verify
			res = sceKernelStat(szFilePath, &status);
			if ((res < 0) && (res != SCE_KERNEL_ERROR_ENOENT)) {
				ALOGE(LOG_AREA_FILE, "Can't get the file status for \"%s\"! Error code #%8X", szFilePath, res);
				return ::SFAT::ErrorCode::ERROR_GETTING_FILE_STATUS;
			}
		}

		::SFAT::ErrorCode err = mFileLW.open(szFilePath, accessMode);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGE(LOG_AREA_FILE, "Can't open the Large-Writes data file!");
		}

		// Allocate the cache memory
		mMemoryCache.initialize(mBlockSize, mClusterSize, kInternalBufferSize);

		// Open the directory data file in /download0/
		{
			//
			// If the file is not opened for read/write access, we need to close it first and then reopen it with read/write access.
			//
			if (mDirectoriesDataFile.isOpen() && !mDirectoriesDataFile.checkAccessMode(::SFAT::AM_READ | ::SFAT::AM_WRITE)) {
				mDirectoriesDataFile.close();
			}
			if (!mDirectoriesDataFile.isOpen()) {
				// Open for read/write only. We don't want to create or truncate it.
				err = mDirectoriesDataFile.open(mDirectoryDataFilePath.c_str(), accessMode & ~(::SFAT::AM_CREATE_IF_DOES_NOT_EXIST | ::SFAT::AM_TRUNCATE));
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					err = mDirectoriesDataFile.open(mDirectoryDataFilePath.c_str(), ::SFAT::AM_WRITE | ::SFAT::AM_CREATE_IF_DOES_NOT_EXIST | ::SFAT::AM_UPDATE);
				}
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					ALOGE(LOG_AREA_FILE, "Can't open the directory data file!");
				}
			}
		}

		mChunkBuffer.resize(mChunkSize);
		mChunkIndex = kInvalidChunkIndex;

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickCombinedFile::close() {
		::SFAT::ErrorCode errFileLW = mFileLW.close();
		mChunkBuffer.clear();
		mChunkIndex = kInvalidChunkIndex;
		mMemoryCache.shutDown();

		return errFileLW;
	}

	::SFAT::ErrorCode BerwickCombinedFile::_readCluster(void* buffer, ::SFAT::FilePositionType globalPosition) {
		if (buffer == nullptr) {
			return ::SFAT::ErrorCode::ERROR_NULL_POINTER_MEMORY_BUFFER;
		}

		const ::SFAT::ClusterIndexType clusterIndex = globalPosition / mClusterSize;
		if (clusterIndex < mClustersPerBlockCount*mFirstClusterDataBlockIndex) {
			//The first few blocks are reserved for directory data and the storage used is /download0/

			size_t sizeRead = 0;
			::SFAT::ErrorCode err = mDirectoriesDataFile.readAtPosition(buffer, mClusterSize, globalPosition, sizeRead);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}
			if (sizeRead != mClusterSize) {
				return ::SFAT::ErrorCode::ERROR_READING_CLUSTER_DATA;
			}
		}
		else {
			globalPosition -= mFirstClusterDataBlockIndex*mBlockSize; // Skiping the first block

			const ::SFAT::FilePositionType localPosition = globalPosition % mBlockSize;
			const uint32_t blockIndex = static_cast<uint32_t>(globalPosition / mBlockSize);
			if (mCachedBlockIndex == blockIndex) {
				::SFAT::ErrorCode err = mMemoryCache.readCluster(buffer, localPosition);
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					return err;
				}
			}
			else {
				Bedrock::Threading::LockGuard<decltype(mChunkUpdateMutex)> lockGuard(mChunkUpdateMutex);

				//Block Virtualization
				// blockIndex contains the virtual block index.
				const uint32_t physicalBlockIndex = getDataPlacementStrategy()->getPhysicalBlockIndex(blockIndex);
				const ::SFAT::FilePositionType physicalGlobalPosition = physicalBlockIndex * static_cast<::SFAT::FilePositionType>(mBlockSize) + localPosition;

				size_t sizeRead = 0;
				const ::SFAT::FilePositionType offset = physicalGlobalPosition % mChunkSize;
				const uint32_t virtualChunkIndex = globalPosition / mChunkSize;
				::SFAT::FilePositionType chunkPosition = physicalGlobalPosition - offset;
				DEBUG_ASSERT(mChunkBuffer.size() == mChunkSize, "The chunk-buffer should have correct size!");
				if (virtualChunkIndex != mChunkIndex) { // Check again if the chunk index hasn't been changed in between.
					::SFAT::ErrorCode err = mFileLW.readAtPosition(mChunkBuffer.data(), mChunkSize, chunkPosition, sizeRead);
					if (err != ::SFAT::ErrorCode::RESULT_OK) {
						return err;
					}
					if (sizeRead != mChunkSize) {
						return ::SFAT::ErrorCode::ERROR_READING_CLUSTER_DATA;
					}
					mChunkIndex = virtualChunkIndex;
				}
				memcpy(buffer, &mChunkBuffer[offset], mClusterSize);
			}
		}

		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickCombinedFile::_writeCluster(const void* buffer, ::SFAT::FilePositionType globalPosition) {
		if (buffer == nullptr) {
			return ::SFAT::ErrorCode::ERROR_NULL_POINTER_MEMORY_BUFFER;
		}

		::SFAT::ErrorCode err = ::SFAT::ErrorCode::RESULT_OK;
		const ::SFAT::ClusterIndexType clusterIndex = globalPosition / mClusterSize;
		// Check if the cluster is in the first data block.
		if (clusterIndex < mClustersPerBlockCount*mFirstClusterDataBlockIndex) {
			//The first few blocks are reserved for directory data and the storage used is /download0/

			size_t sizeWritten = 0;
			err = mDirectoriesDataFile.writeAtPosition(buffer, mClusterSize, globalPosition, sizeWritten);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}
			if (sizeWritten != mClusterSize) {
				return ::SFAT::ErrorCode::ERROR_WRITING_CLUSTER_DATA;
			}
		}
		else {
			globalPosition -= mFirstClusterDataBlockIndex*mBlockSize; // Skiping the first block
			uint32_t blockIndex = static_cast<uint32_t>(globalPosition / mBlockSize);

			if ((mCachedBlockIndex != blockIndex) && (mCachedBlockIndex != kInvalidDataBlockIndex)) {
				// There is a block currently cached and it is not the block that we need.
				// So flush this block to the Large Write storage (if not necessary).
				err = copyCacheToBlock();
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					return err;
				}
				// Mark that nothing is cached.
				mCachedBlockIndex = kInvalidDataBlockIndex;
			}

			if (mCachedBlockIndex == kInvalidDataBlockIndex) {
				// There is no cached data block
				err = copyBlockToCache(blockIndex);
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					return err;
				}
			}

			DEBUG_ASSERT(mCachedBlockIndex == blockIndex, "The block should be already in the cache!");
			if (mCachedBlockIndex == blockIndex) {
				// We will loose the sync writing into the cache.
				mIsCacheInSync = false;

				// Do the writing itself
				::SFAT::FilePositionType localPosition = globalPosition % mBlockSize;
				err = mMemoryCache.writeCluster(buffer, localPosition);
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					return err;
				}
				++mCountWrittenClusters;
			}
			else {
				err = ::SFAT::ErrorCode::ERROR_WRITING__INVALID_CACHE;
			}
		}

		return err;
	}

	::SFAT::ErrorCode BerwickCombinedFile::readAtPosition(void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeRead) {
		sizeRead = 0;
		if (sizeInBytes == mClusterSize) {
			if (position % mClusterSize == 0) {
				::SFAT::ErrorCode err = _readCluster(buffer, position);
				if (err == ::SFAT::ErrorCode::RESULT_OK) {
					sizeRead = mClusterSize;
				}
				return err;
			}
		}

		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickCombinedFile::_writeAtPosition(int fileDescriptor, const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) {
		sizeWritten = 0;

		DEBUG_ASSERT(fileDescriptor >= 0, "The file descriptor should not be negative!");

		off_t res = sceKernelLwfsLseek(fileDescriptor, static_cast<off_t>(position), SCE_KERNEL_SEEK_SET);
		if (res < 0) {
			ALOGE(LOG_AREA_FILE, "Can't set the read/write position! Error code #%8X", res);
			return ::SFAT::ErrorCode::ERROR_POSITIONING_IN_FILE_LOW_LEVEL;
		}

		ssize_t writeResult = sceKernelLwfsWrite(fileDescriptor, buffer, sizeInBytes);
		if (writeResult < 0) {
			ALOGE(LOG_AREA_FILE, "Can't write to LW file! Error code #%8X", writeResult);
			return ::SFAT::ErrorCode::ERROR_WRITING_LOW_LEVEL;
		}

		sizeWritten = static_cast<size_t>(writeResult);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode BerwickCombinedFile::writeAtPosition(const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) {
		sizeWritten = 0;
		if (sizeInBytes == mClusterSize) {
			if (position % mClusterSize == 0) {
				::SFAT::ErrorCode err = _writeCluster(buffer, position);
				if (err == ::SFAT::ErrorCode::RESULT_OK) {
					sizeWritten = mClusterSize;
				}
				return err;
			}
		}

		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickCombinedFile::seek(::SFAT::FilePositionType offset, ::SFAT::SeekMode mode) {
		if ((offset % mClusterSize == 0) && (mode == ::SFAT::SeekMode::SM_SET)) {
			return mFileLW.seek(offset, mode);
		}

		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickCombinedFile::getPosition(::SFAT::FilePositionType& position) {
		UNUSED_PARAMETER(position);
		return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
	}

	::SFAT::ErrorCode BerwickCombinedFile::getSize(::SFAT::FileSizeType& size) {
		UNUSED_PARAMETER(size);
		return ::SFAT::ErrorCode::NOT_IMPLEMENTED;
	}


	::SFAT::ErrorCode BerwickCombinedFile::flush() {
		::SFAT::ErrorCode err = mDirectoriesDataFile.flush();
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}
		if (!mIsCacheInSync) {
			err = copyCacheToBlock();
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}
		}
		return mFileLW.flush();
	}

	::SFAT::ErrorCode BerwickCombinedFile::_initialBlockAllocation(const char* szFilePath) {
		sceKernelUnlink(szFilePath);

		DEBUG_ASSERT(!mFileLW.isOpen(), "Should not be currently open!");
		DEBUG_ASSERT(!isOpen(), "Should not be currently open!");

		::SFAT::ErrorCode err = mFileLW.open(szFilePath, ::SFAT::AM_WRITE | ::SFAT::AM_BINARY | ::SFAT::AM_CREATE_IF_DOES_NOT_EXIST);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			ALOGI(LOG_AREA_FILE, "Can't open the Large-Writes file!");
		}

		// Create the directory data file in /download0/
		{
			// Open for read/write only. We don't want to create or truncate it.
			err = mDirectoriesDataFile.open(mDirectoryDataFilePath.c_str(), ::SFAT::AM_WRITE | ::SFAT::AM_CREATE_IF_DOES_NOT_EXIST | ::SFAT::AM_TRUNCATE);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				ALOGE(LOG_AREA_FILE, "Can't open the directory data file!");
			}
		}

		return err;
	}

	/*
		blockIndex == 0 - stored on the 1GB (/download0), file - mDirectoriesDataFile
		blockIndex > 0 - stored on the 15GB (/download1), write file - mWriteFile, read file - mReadFile
			Note that the global block 1 will become local block 0 in the /download1.
	*/
	::SFAT::ErrorCode BerwickCombinedFile::blockAllocation(uint32_t blockIndex) {
		::SFAT::ErrorCode err = ::SFAT::ErrorCode::RESULT_OK;
		if (blockIndex == 0) {
			::SFAT::FilePositionType position = 0;

			std::vector<uint8_t> buffer;
			size_t bufferSize = std::min(static_cast<size_t>(16*(1 << 20)), static_cast<size_t>(mBlockSize));
			buffer.resize(bufferSize, 0);

			size_t bytesRemainingToWrite = mBlockSize;
			while (bytesRemainingToWrite > 0) {
				size_t bytesToWrite = std::min(bytesRemainingToWrite, bufferSize);
				size_t bytesWritten = 0;
				err = mDirectoriesDataFile.writeAtPosition(buffer.data(), bytesToWrite, position, bytesWritten);
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					ALOGE(LOG_AREA_FILE, "Error #%08X during data block allocation!", err);
					return err;
				}
				if (bytesWritten != bytesToWrite) {
					return ::SFAT::ErrorCode::ERROR_EXPANDING_DATA_BLOCK;
				}
				position += bytesToWrite;
				bytesRemainingToWrite -= bytesWritten;
			}
			
			err = mDirectoriesDataFile.flush();
		}
		else {
			const uint32_t virtualBlockIndex = blockIndex - mFirstClusterDataBlockIndex;
			const uint32_t physicalBlockIndex = getDataPlacementStrategy()->getPhysicalBlockIndex(virtualBlockIndex);
			err = mFileLW.blockAllocation(physicalBlockIndex);
		}

		return err;
	}


	::SFAT::ErrorCode BerwickCombinedFile::copyCacheToBlock() {
		if (mCachedBlockIndex == kInvalidDataBlockIndex) {
			// Nothing in the cache, so no need to transfer anything.
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		if (mIsCacheInSync) {
			// The cache is already in sync. Nothing to transfer.
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		return copyCacheToBlock(mCachedBlockIndex);
	}

	uint32_t MemoryCache::getLastChangedChunk() const {
		size_t bitIndexFound;
		if (mChangedChunksMap.findLast(bitIndexFound, true)) {
			return static_cast<uint32_t>(bitIndexFound);
		}
		return kInvalidChunkIndex;
	}

	::SFAT::ErrorCode BerwickCombinedFile::optimizeCachedBlockContent() {
		if (kInvalidDataBlockIndex == mCachedBlockIndex) {
			// Nothing to optimize
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		// Find the last changed chunk
		uint32_t lastChangedChunkIndex = mMemoryCache.getLastChangedChunk();
		if (lastChangedChunkIndex == kInvalidChunkIndex) {
			// Nothing to optimize
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		if (mBlockOptimizationPerformed) {
			return ::SFAT::ErrorCode::RESULT_OK;
		}
		mBlockOptimizationPerformed = true;

		std::shared_ptr<BerwickDataPlacementStrategy> dataPlacementStrategy = std::static_pointer_cast<BerwickDataPlacementStrategy>(static_cast<BerwickCombinedFileStorage&>(mFileStorage).mDataPlacementStrategy);
		SFAT_ASSERT(dataPlacementStrategy != nullptr, "The DataPlacementStrategy should be available!");

#if (SPLIT_FAT_PROFILING == 1)
		auto startTime = std::chrono::high_resolution_clock::now();
		ALOGI(LOG_AREA_FILE, "Performing data block #%u optimization.", mCachedBlockIndex);
#endif
		::SFAT::ErrorCode err = dataPlacementStrategy->optimizeBlockContent(mCachedBlockIndex, lastChangedChunkIndex, mInitialFreeClustersSet);
#if (SPLIT_FAT_PROFILING == 1)
		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = endTime - startTime;
		ALOGI(LOG_AREA_FILE, "Finished data block #%u optimization in %3.3f secs.", mCachedBlockIndex, diff.count());
#endif

		return err;
	}

	/*
		Copy only the allocated clusters.
	*/
	::SFAT::ErrorCode BerwickCombinedFile::copyCacheToBlock(uint32_t blockIndex) {
		ALOGI(LOG_AREA_FILE, "Start cache to data block #%u transfer.", blockIndex);
#if (SPLIT_FAT_PROFILING == 1)
		auto startTime = std::chrono::high_resolution_clock::now();
#endif
		mIsCacheInSync = false;
		const size_t chunkSize = mMemoryCache.getChunkSize();

		::SFAT::ErrorCode err;
		// Optimize the clusters placement
#if (SPLITFAT_ENABLE_DEFRAGMENTATION == 1)
		err = optimizeCachedBlockContent();
		SFAT_ASSERT(err == ::SFAT::ErrorCode::RESULT_OK, "Block data optimization failed!");
#endif //SPLITFAT_ENABLE_DEFRAGMENTATION

		// Find the last used chunk
		uint32_t lastUsedChunkIndex = kInvalidChunkIndex;
		bool invalidateMemoryCache = false;

		std::shared_ptr<::SFAT::DataPlacementStrategyBase> dataPlacementStrategy = static_cast<BerwickCombinedFileStorage&>(mFileStorage).mDataPlacementStrategy;
		const uint32_t chunksCount = mBlockSize / chunkSize;
		::SFAT::BitSet finalFreeClustersSet;
		if ((nullptr != dataPlacementStrategy) &&
			(::SFAT::ErrorCode::RESULT_OK == dataPlacementStrategy->copyFreeClustersBitSet(finalFreeClustersSet, blockIndex + mFirstClusterDataBlockIndex))) {

			uint32_t clustersPerChunk = mMemoryCache.getChunkSize() / mClusterSize;
			for (uint32_t localChunkIndex = 0; localChunkIndex < chunksCount; ++localChunkIndex) {
				uint32_t startClusterIndex = localChunkIndex * clustersPerChunk;
				uint32_t endClusterIndex = startClusterIndex + clustersPerChunk;
				for (uint32_t localClusterIndex = startClusterIndex; localClusterIndex < endClusterIndex; ++localClusterIndex) {
					if (!finalFreeClustersSet.getValue(localClusterIndex)) {
						// This chunk is occupied, save the index and move to the next chunk.
						lastUsedChunkIndex = localChunkIndex;
						break;
					}
				}
			}
		}
		else {
			// We shouldn't end up here at all, but in case we do, save all chunks!
			SFAT_ASSERT(false, "Something went wrong with the dataPlacementStrategy!");
			lastUsedChunkIndex = chunksCount;
			invalidateMemoryCache = true; // In case of error and once everything is saved, invalidate the cache, so it could be reloaded properly.
		}

		if (lastUsedChunkIndex == kInvalidChunkIndex) {
			// Nothing to write
			return ::SFAT::ErrorCode::RESULT_OK;
		}

		const uint32_t scratchBlockIndex = getDataPlacementStrategy()->getScratchBlockIndex();
		::SFAT::FilePositionType posWrite = static_cast<::SFAT::FilePositionType>(mBlockSize) * scratchBlockIndex;
		err = ::SFAT::ErrorCode::RESULT_OK;
		for (uint32_t i = 0; i <= lastUsedChunkIndex; ++i) {
			const uint8_t* bufferPtr = mMemoryCache.getConstMemoryChunk(i);
			size_t sizeWritten = 0;
			err = mFileLW.writeAtPosition(bufferPtr, chunkSize, posWrite, sizeWritten);
			if (err != ::SFAT::ErrorCode::RESULT_OK) {
				return err;
			}
			posWrite += sizeWritten;
		}

		err = mFileLW.flush();
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		mMemoryCache.setAllChunksChanged(false);
		mIsCacheInSync = true;
		mCountWrittenClusters = 0;

		err = getDataPlacementStrategy()->swapScratchBlockWithVirtualBlock(blockIndex);
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		if (invalidateMemoryCache) {
			// Mark that nothing is cached.
			mCachedBlockIndex = kInvalidDataBlockIndex;
		}

#if (SPLIT_FAT_PROFILING == 1)
		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = endTime - startTime;
		ALOGI(LOG_AREA_FILE, "Finished cache to data block #%u transfer in %3.3f secs.", blockIndex, diff.count());
#endif
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	//TODO: Task 222923 - Optimize
	/*
		The transfer could be probably optimized to copy only the 256KB that contain clusters currently allocated.
		This way an almost empty block will be copied a lot faster even if the data is scattered inside.
	*/
	::SFAT::ErrorCode BerwickCombinedFile::copyBlockToCache(uint32_t blockIndex) {
		ALOGI(LOG_AREA_FILE, "Start data block #%u to cache transfer.", blockIndex);
#if (SPLIT_FAT_PROFILING == 1)
		auto startTime = std::chrono::high_resolution_clock::now();
#endif

		// Flush first everything written in the Large Write storage, becase we will read from it with another file-manipulator.
		::SFAT::ErrorCode err = mFileLW.flush();
		if (err != ::SFAT::ErrorCode::RESULT_OK) {
			return err;
		}

		mCachedBlockIndex = kInvalidDataBlockIndex;
		mIsCacheInSync = false;
		mBlockOptimizationPerformed = false;
		size_t bufferSize = mMemoryCache.getChunkSize();
		SFAT_ASSERT(mChunkSize == mMemoryCache.getChunkSize(), "Both sizes should be the same!");
		uint32_t chunksCount = mBlockSize / bufferSize;

		std::shared_ptr<::SFAT::DataPlacementStrategyBase> dataPlacementStrategy = static_cast<BerwickCombinedFileStorage&>(mFileStorage).mDataPlacementStrategy;

		::SFAT::BitSet chunksToRead(chunksCount);
		if ((nullptr != dataPlacementStrategy) &&
			(::SFAT::ErrorCode::RESULT_OK == dataPlacementStrategy->copyFreeClustersBitSet(mInitialFreeClustersSet, blockIndex + mFirstClusterDataBlockIndex))) {

			uint32_t clustersPerChunk = mMemoryCache.getChunkSize() / mClusterSize;
			chunksToRead.setAll(false);
			for (uint32_t localClusterIndex = 0; localClusterIndex < mClustersPerBlockCount; ++localClusterIndex) {
				if (!mInitialFreeClustersSet.getValue(localClusterIndex)) {
					chunksToRead.setValue(localClusterIndex / clustersPerChunk, true);
				}
			}
		}
		else {
			mInitialFreeClustersSet.setAll(false);
			chunksToRead.setAll(true);
		}

		mMemoryCache.clearChunkMaps();
		const uint32_t physicalBlockIndex = getDataPlacementStrategy()->getPhysicalBlockIndex(blockIndex);
		::SFAT::FilePositionType posRead = static_cast<::SFAT::FilePositionType>(mBlockSize) * physicalBlockIndex;
		err = ::SFAT::ErrorCode::RESULT_OK;
		for (uint32_t i = 0; i < chunksCount; ++i) {
			uint8_t* bufferPtr = mMemoryCache.getMemoryChunk(i);
			if (chunksToRead.getValue(i)) {
				size_t sizeRead = 0;
				err = mFileLW.readAtPosition(bufferPtr, bufferSize, posRead, sizeRead);
				if (err != ::SFAT::ErrorCode::RESULT_OK) {
					return err;
				}
				SFAT_ASSERT(sizeRead == bufferSize, "We should be able to read exactly the buffer size!");
			}
			else {
				// The chunk is not accupied, so we don't need to read it! Clearing with encoding of the virtual block index. Just redundancy data.
				memset(bufferPtr, 0, 0x80 + (blockIndex & 0x3f));
			}
			posRead += bufferSize;
		}

		mIsCacheInSync = true;
		mCachedBlockIndex = blockIndex;

		// Invalidate the mChunkIndex if the block contains this particular chunk
		if (mChunkIndex != kInvalidChunkIndex) {
			::SFAT::FilePositionType chunkPosition = mChunkIndex * mChunkSize /*+ mFirstClusterDataBlockIndex * mBlockSize*/;
			::SFAT::FilePositionType blockPosition = mCachedBlockIndex * mBlockSize /*+ mFirstClusterDataBlockIndex * mBlockSize*/;
			if ((chunkPosition >= blockPosition) && (chunkPosition + mChunkSize <= blockPosition + mBlockSize)) {
				// We need to invalidate the cached chunk.
				mChunkIndex = kInvalidChunkIndex;
			}
		}

#if (SPLIT_FAT_PROFILING == 1)
		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = endTime - startTime;
		ALOGI(LOG_AREA_FILE, "Finished data block #%u to cache transfer in %3.3f secs.", blockIndex, diff.count());
#endif
		return err;
	}

	std::shared_ptr<BerwickDataPlacementStrategy> BerwickCombinedFile::getDataPlacementStrategy() {
		return std::static_pointer_cast<BerwickDataPlacementStrategy>(static_cast<BerwickCombinedFileStorage&>(mFileStorage).mDataPlacementStrategy);
	}


	/*************************************************************************************
		BerwickCombinedFileStorage implementation
	*************************************************************************************/

	BerwickCombinedFileStorage::BerwickCombinedFileStorage(std::shared_ptr<BerwickFileStorage> berwickFileStorage, 
														 std::shared_ptr<BerwickFileStorageLargeWrites> berwickFileStorageLargeWrites,
														 std::string directoryDataFilePath)
		: BerwickFileStorage(berwickFileStorage->getMountPath())
		, mBerwickFileStorage(std::move(berwickFileStorage))
		, mBerwickFileStorageLargeWrites(std::move(berwickFileStorageLargeWrites))
		, mDirectoryDataFilePath(std::move(directoryDataFilePath)) {
	}

	BerwickCombinedFileStorage::~BerwickCombinedFileStorage() {
	}

	::SFAT::ErrorCode BerwickCombinedFileStorage::createFileImpl(std::shared_ptr<::SFAT::FileBase>& fileImpl) {
		fileImpl = std::make_shared<BerwickCombinedFile>(*this);
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	bool BerwickCombinedFileStorage::isAvailable() {
		SceAppContentMountPoint	mountPoint;
		strncpy(mountPoint.data, "/download1", SCE_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE);

		//Check if /download1 is available.
		SceKernelStat stat;
		int ret = sceKernelStat(mountPoint.data, &stat);
		if (ret != SCE_OK)
		{
			//Please make sure to configure 'Storage setting(1)' in 'param.sfo' with 'orbis-pub-sfo.exe'
			//or 'param.sfo' is in the correct position. 
			ALOGI(LOG_AREA_FILE, "/download1 is not available.");
			return false;
		}
		else {
			ALOGI(LOG_AREA_FILE, "/download1 is available.");
		}
		return true;
	}

	void BerwickCombinedFileStorage::setDataPlacementStrategy(std::shared_ptr<::SFAT::DataPlacementStrategyBase> dataPlacementStrategy) {
		mDataPlacementStrategy = std::move(dataPlacementStrategy);
	}


	/*************************************************************************************
		MemoryCache implementation
	*************************************************************************************/

	MemoryCache::~MemoryCache() {
		shutDown();
	}


	::SFAT::ErrorCode MemoryCache::initialize(size_t bufferSize, size_t clusterSize, size_t chunkSize) {
		mBufferSize = bufferSize;
		mClusterSize = clusterSize;
		mChunkSize = chunkSize;
		mCountChunks = mBufferSize / mChunkSize;
		mChangedChunksMap.setSize(mCountChunks);
		clearChunkMaps();

		DEBUG_ASSERT((mChunkSize % mClusterSize) == 0, "The internal block size should be multiple of the cluster-size!");

		size_t memAlign = 64 * (1 << 10); // Alignment: 64KiB
		int32_t res = sceKernelAllocateDirectMemory(0,
			SCE_KERNEL_MAIN_DMEM_SIZE, mBufferSize, memAlign,
			SCE_KERNEL_WB_ONION, &mMemStart);
		if (res < 0) {
			ALOGE(LOG_AREA_FILE, "Can't allocate %uMB direct memory", bufferSize / (1 << 20));
			return ::SFAT::ErrorCode::ERROR_ALLOCATING_MEMORY_BUFFER;
		}

		// Maps the direct memory to the virtual address space
		void* addr = nullptr;
		res = sceKernelMapDirectMemory(&addr, /*mapLen*/mBufferSize, SCE_KERNEL_PROT_CPU_RW, 0, mMemStart, memAlign);
		if (res < 0) {
			// Unmap the direct memory
			sceKernelCheckedReleaseDirectMemory(mMemStart, mBufferSize);
			ALOGE(LOG_AREA_FILE, "Can't map the direct memory to a virtual address");
			return ::SFAT::ErrorCode::ERROR_ALLOCATING_MEMORY_BUFFER;
		}

		mBufferPtr = static_cast<uint8_t*>(addr);
		mIsReady = true;
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	void MemoryCache::clearChunkMaps() {
		mChangedChunksMap.setAll(false);
	}

	void MemoryCache::shutDown() {
		if (mIsReady) {
			// Unmap the direct memory
			int32_t ret = sceKernelCheckedReleaseDirectMemory(mMemStart, mBufferSize);
			DEBUG_ASSERT(ret >= 0, "Can't release properly the direct memory!");

			mBufferSize = 0;
			mClusterSize = 0;
			mChunkSize = 0;
			mBufferPtr = nullptr;

			mIsReady = false;
		}
	}

	::SFAT::ErrorCode MemoryCache::readCluster(void* buffer, ::SFAT::FilePositionType position) {
		if (position >= mBufferSize) {
			return ::SFAT::ErrorCode::ERROR_POSITIONING_OUT_OF_RANGE;
		}

#if (SPLIT_FAT_EXTRA_PROFILING == 1)
		auto startTime = std::chrono::high_resolution_clock::now();
#endif
		memcpy(buffer, &mBufferPtr[position], mClusterSize);
#if (SPLIT_FAT_EXTRA_PROFILING == 1)
		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = endTime - startTime;
		mTimeToMemCopyOn_readCluster += diff.count();
		mBytesCopied_readCluster += mClusterSize;
#endif
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	::SFAT::ErrorCode MemoryCache::writeCluster(const void* buffer, ::SFAT::FilePositionType position) {
		if (position >= mBufferSize) {
			return ::SFAT::ErrorCode::ERROR_POSITIONING_OUT_OF_RANGE;
		}

#if (SPLIT_FAT_EXTRA_PROFILING == 1)
		auto startTime = std::chrono::high_resolution_clock::now();
#endif
		memcpy(&mBufferPtr[position], buffer, mClusterSize);
		size_t chunkIndex = static_cast<size_t>(position) / mChunkSize;
		mChangedChunksMap.setValue(chunkIndex, true);
#if (SPLIT_FAT_EXTRA_PROFILING == 1)
		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = endTime - startTime;
		mTimeToMemCopyOn_writeCluster += diff.count();
		mBytesCopied_writeCluster += mClusterSize;
#endif
		return ::SFAT::ErrorCode::RESULT_OK;
	}

	const uint8_t* MemoryCache::getConstMemoryChunk(size_t chunkIndex) const {
		SFAT_ASSERT(chunkIndex < mBufferSize / mChunkSize, "The memory-chunk index is out of range");
		size_t offset = chunkIndex * mChunkSize;
		return mBufferPtr + offset;
	}

	uint8_t* MemoryCache::getMemoryChunk(size_t chunkIndex) {
		SFAT_ASSERT(chunkIndex < mBufferSize / mChunkSize, "The memory-chunk index is out of range");
		size_t offset = chunkIndex * mChunkSize;
		return mBufferPtr + offset;
	}

	bool MemoryCache::isChunkChanged(size_t chunkIndex) const {
		return mChangedChunksMap.getValue(chunkIndex);
	}

	void MemoryCache::setChunkChanged(size_t chunkIndex, bool value) {
		mChangedChunksMap.setValue(chunkIndex, value);
	}

	void MemoryCache::setAllChunksChanged(bool value) {
		mChangedChunksMap.setAll(value);
	}

#if (SPLIT_FAT_EXTRA_PROFILING == 1)
	double MemoryCache::mTimeToMemCopyOn_readCluster = 0.0;
	double MemoryCache::mTimeToMemCopyOn_writeCluster = 0.0;
	uint64_t MemoryCache::mBytesCopied_readCluster = 0ULL;
	uint64_t MemoryCache::mBytesCopied_writeCluster = 0ULL;

	//static
	void MemoryCache::cleanUpCounters() {
		mTimeToMemCopyOn_readCluster = 0.0;
		mTimeToMemCopyOn_writeCluster = 0.0;
		mBytesCopied_readCluster = 0ULL;
		mBytesCopied_writeCluster = 0ULL;
	}

	//static
	void MemoryCache::printCounters() {
		if (mTimeToMemCopyOn_readCluster > 0.0) {
			double f = mBytesCopied_readCluster / mTimeToMemCopyOn_readCluster;
			ALOGI(LOG_AREA_FILE, "MemoryCache: memcpy() for readCluster() : %3.3fMB/sec", f / (1 << 20));
		}
		else {
			ALOGI(LOG_AREA_FILE, "MemoryCache: memcpy() for readCluster() - Copied : %uMB", mBytesCopied_readCluster / (1 << 20));
		}
		if (mTimeToMemCopyOn_writeCluster > 0.0) {
			double f = mBytesCopied_writeCluster / mTimeToMemCopyOn_writeCluster;
			ALOGI(LOG_AREA_FILE, "MemoryCache: memcpy() for writeCluster() : %3.3fMB/sec", f / (1 << 20));
		}
		else {
			ALOGI(LOG_AREA_FILE, "MemoryCache: memcpy() for writeCluster() - Copied : %uMB", mBytesCopied_writeCluster / (1 << 20));
		}
	}
#endif

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

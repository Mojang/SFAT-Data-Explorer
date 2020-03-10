/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#if defined(MCPE_PLATFORM_ORBIS)

#include "Core/Platform/orbis/file/sfat/BerwickFileSystemLargeWrites.h"
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/Common.h"

#include <unistd.h>
#include <vector>

#else

#include "BerwickToWindowsPort.h"

#include "BerwickFileSystemLargeWrites.h"
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/Common.h"

//#include <unistd.h>
#include <vector>

#endif

#define SPLIT_FAT_PROFILING	1
#define SPLIT_FAT_EXTRA_PROFILING	0

struct SceAppContentMountPoint;

namespace SFAT {
	class DataPlacementStrategyBase;
}

namespace Core { namespace SFAT {

	class BerwickCombinedFileStorage;
	class BerwickDataPlacementStrategy;

	/*
	 * The MemoryCache class is used to keep all writes and reads for particlular 256MB storage block in memory.
	 * The main purpose is to overcome the limitations of precise file positioning on the target file-system,
	 * and read/write only blocks of 256MB to the storage.
	 * The secondary purpose is to be used as a cache.
	 */
	class MemoryCache {
	public:
		virtual ~MemoryCache();
		::SFAT::ErrorCode initialize(size_t bufferSize, size_t clusterSize, size_t chunkSize);
		void shutDown();
		::SFAT::ErrorCode readCluster(void* buffer, ::SFAT::FilePositionType position);
		::SFAT::ErrorCode writeCluster(const void* buffer, ::SFAT::FilePositionType position);
		uint8_t* getMemoryChunk(size_t chunkIndex);
		const uint8_t* getConstMemoryChunk(size_t chunkIndex) const;
		size_t getChunkSize() const { return mChunkSize; }

#if (SPLIT_FAT_EXTRA_PROFILING == 1)
		// Profiling
		static void cleanUpCounters();
		static void printCounters();
#endif
		bool isChunkChanged(size_t chunkIndex) const;
		void setChunkChanged(size_t chunkIndex, bool value);
		void setAllChunksChanged(bool value);
		uint32_t getLastChangedChunk() const;
		void clearChunkMaps();

	private:
		size_t mBufferSize;
		size_t mClusterSize;
		size_t mChunkSize;
		off_t  mMemStart;
		bool mIsReady = false;
		uint8_t* mBufferPtr = nullptr;
		size_t mCountChunks;
		::SFAT::BitSet mChangedChunksMap;

#if (SPLIT_FAT_EXTRA_PROFILING == 1)
		static double mTimeToMemCopyOn_readCluster;
		static double mTimeToMemCopyOn_writeCluster;
		static uint64_t mBytesCopied_readCluster;
		static uint64_t mBytesCopied_writeCluster;
#endif
	};

	class BerwickCombinedFile : public ::SFAT::FileBase {
	public:
		BerwickCombinedFile(BerwickCombinedFileStorage& fileStorage);
		virtual ~BerwickCombinedFile() override;
		virtual bool isOpen() const override;
		virtual ::SFAT::ErrorCode open(const char *szFilePath, uint32_t accessMode) override;
		virtual ::SFAT::ErrorCode close() override;

		// Disabling the use of the function because the getPosition() function is may not be reliable otherwise
		// Note that the SplitFAT file system will rely entirely on readAtPosition()
		virtual ::SFAT::ErrorCode read(void* buffer, size_t sizeInBytes, size_t& sizeRead) override final {
			UNUSED_PARAMETER(buffer);
			UNUSED_PARAMETER(sizeInBytes);
			UNUSED_PARAMETER(sizeRead);
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}
		// Disabling the use of the function because the getPosition() function may not be reliable otherwise
		// Note that the SplitFAT file system will rely entirely on writeAtPosition()
		virtual ::SFAT::ErrorCode write(const void* buffer, size_t sizeInBytes, size_t& sizeWritten) override final {
			UNUSED_PARAMETER(buffer);
			UNUSED_PARAMETER(sizeInBytes);
			UNUSED_PARAMETER(sizeWritten);
			return ::SFAT::ErrorCode::ERROR_FEATURE_NOT_SUPPORTED;
		}

		virtual ::SFAT::ErrorCode readAtPosition(void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeRead) override;
		virtual ::SFAT::ErrorCode writeAtPosition(const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten) override;
		virtual ::SFAT::ErrorCode seek(::SFAT::FilePositionType offset, ::SFAT::SeekMode mode) override;
		virtual ::SFAT::ErrorCode getPosition(::SFAT::FilePositionType& position) override;
		virtual ::SFAT::ErrorCode getSize(::SFAT::FileSizeType& size) override;
		virtual ::SFAT::ErrorCode flush() override;

		::SFAT::ErrorCode copyCacheToBlock(uint32_t blockIndex);
		::SFAT::ErrorCode copyCacheToBlock();
		::SFAT::ErrorCode copyBlockToCache(uint32_t blockIndex);

		::SFAT::ErrorCode blockAllocation(uint32_t blockIndex);
		::SFAT::ErrorCode optimizeCachedBlockContent();

	private:
		::SFAT::ErrorCode _readCluster(void* buffer, ::SFAT::FilePositionType globalPosition);
		::SFAT::ErrorCode _writeCluster(const void* buffer, ::SFAT::FilePositionType globalPosition);
		::SFAT::ErrorCode _initialBlockAllocation(const char* szFilePath);
		::SFAT::ErrorCode _writeAtPosition(int fileDescriptor, const void* buffer, size_t sizeInBytes, ::SFAT::FilePositionType position, size_t& sizeWritten);
		std::shared_ptr<BerwickDataPlacementStrategy> getDataPlacementStrategy();

	private:
		std::vector<uint8_t>	mChunkBuffer; // 256kb buffer
		volatile uint32_t mChunkIndex;
		uint32_t mClusterSize = ::SFAT::DefaultSetupValues::CLUSTER_SIZE; // 8KB
		const uint32_t mBlockSize = 256 * (1 << 20); // 256MB
		const size_t mChunkSize = 256 * (1 << 10); // 256KB - the read/write access should be aligned and on portions of this.
		const ::SFAT::ClusterIndexType mClustersPerBlockCount = static_cast<::SFAT::ClusterIndexType>(mBlockSize / mClusterSize);
		const uint32_t mFirstClusterDataBlockIndex = 1;
		BerwickFileLargeWrites mFileLW; // /download1 data area
		MemoryCache mMemoryCache; // 256MB Sysmem Memory cache in chunks of 256KB
		BerwickFile mDirectoriesDataFile; // /download0 data area
		uint32_t mCachedBlockIndex;
		bool mIsCacheInSync;
		std::string mDirectoryDataFilePath;
		Bedrock::Threading::Mutex mChunkUpdateMutex;
		::SFAT::BitSet mInitialFreeClustersSet;
		uint32_t mCountWrittenClusters; // Count of the written clusters for the cached block.
		bool mBlockOptimizationPerformed;
	};

	class BerwickCombinedFileStorage : public BerwickFileStorage {
		friend class BerwickCombinedFile; //Giving access to mBerwickFileStorage
	public:
		BerwickCombinedFileStorage(std::shared_ptr<BerwickFileStorage> berwickFileStorage,
									std::shared_ptr<BerwickFileStorageLargeWrites> berwickFileStorageLargeWrites,
									std::string directoryDataFilePath);
		~BerwickCombinedFileStorage();
		void setDataPlacementStrategy(std::shared_ptr<::SFAT::DataPlacementStrategyBase> dataPlacementStrategy);

	private:
		bool isAvailable();

	protected:
		virtual ::SFAT::ErrorCode createFileImpl(std::shared_ptr<::SFAT::FileBase>& fileImpl) override;

	private:
		std::shared_ptr<BerwickFileStorage> mBerwickFileStorage;
		std::shared_ptr<BerwickFileStorageLargeWrites> mBerwickFileStorageLargeWrites;
		std::string mDirectoryDataFilePath;
		std::shared_ptr<::SFAT::DataPlacementStrategyBase> mDataPlacementStrategy;
	};

} } // namespace Core::SFAT

//#endif // MCPE_PLATFORM_ORBIS

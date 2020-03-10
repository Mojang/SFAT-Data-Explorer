/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/DataBlockManager.h"
#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/AbstractFileSystem.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/utils/Logger.h"
#include <algorithm>
#include <string.h>

#if !defined(MCPE_PUBLISH)
#	define SPLITFAT_VIRIFY_CONSISTENCY		0
#endif

#define SPLITFAT_FORCE_CRC_VERIFICATION_ON_MEMORY_DATA	0

namespace SFAT {


	DataBlockManager::DataBlockManager(VolumeManager& volumeManager)
		: mVolumeManager(volumeManager) {
		mClustersPerFATBlock = mVolumeManager.getVolumeDescriptor().getClustersPerFATBlock();
		mMaxPossibleBlocksCount = mVolumeManager.getMaxPossibleBlocksCount();
		mDataBlockSize = static_cast<size_t>(mVolumeManager.getVolumeDescriptor().getDataBlockSize());
		mClusterSize = mVolumeManager.getClusterSize();
	}

	DataBlockManager::~DataBlockManager() {

	}

	bool DataBlockManager::canExpand() const {
		uint32_t currentBlocksCount = mVolumeManager.getCountAllocatedDataBlocks();
		if (currentBlocksCount >= mMaxPossibleBlocksCount) {
			return false;
		}

		return true;
	}

	FilePositionType DataBlockManager::_getPosition(ClusterIndexType clusterIndex) const {
		// The calculations here consider that the cluster-data-blocks may not be sequential.
		// This may happen if both - the FAT-data and the cluster-data are interleaved in a single expandable file.
		uint32_t blockIndex = mVolumeManager.getBlockIndex(clusterIndex);
		FilePositionType blockStartPosition = mVolumeManager.getDataBlockStartPosition(blockIndex);
		FilePositionType relativeClusterIndex = clusterIndex % mClustersPerFATBlock;
		FilePositionType position = blockStartPosition + relativeClusterIndex*mClusterSize;
		return position;
	}

	ErrorCode DataBlockManager::readCluster(std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex, bool isDirectoryData) {
		SFATLockGuard guard(mClusterReadWriteMutex);
		
		if (buffer.size() < mClusterSize) {
			buffer.resize(mClusterSize);
		}

		if (isDirectoryData) {
			// Check first if we have the cluster data cached
			auto it = mCachedClusters.find(clusterIndex);
			if (it != mCachedClusters.end()) {
				// We have it cached.
				SFAT_ASSERT(it->second.mBuffer.size() == mClusterSize, "The cached cluster data buffer should have correct size!");
				memcpy(buffer.data(), it->second.mBuffer.data(), mClusterSize);
#if defined(_DEBUG) && (SPLITFAT_VIRIFY_CONSISTENCY == 1)
				FilePositionType position = _getPosition(clusterIndex);
				std::vector<uint8_t> localBuffer(mClusterSize);
				FileHandle file = mVolumeManager.getLowLevelFileAccess().getClusterDataFile(AccessMode::AM_READ);
				SFAT_ASSERT(file.isOpen(), "The cluster/directory data file should be open!");

				size_t bytesRead = 0;
				ErrorCode err = file.readAtPosition(localBuffer.data(), mClusterSize, position, bytesRead);
				if (err != ErrorCode::RESULT_OK) {
					SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X reading cluster!", err);
				}
				else if (localBuffer != it->second.mBuffer) {
					SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Inconsistent cached directory data!");
				}
#endif

#if (SPLITFAT_FORCE_CRC_VERIFICATION_ON_MEMORY_DATA == 1)
				return mVolumeManager.verifyCRCOnRead(buffer, clusterIndex);
#else 
				return ErrorCode::RESULT_OK;
#endif //(SPLITFAT_FORCE_CRC_VERIFICATION_ON_MEMORY_CACHED_DATA == 1)
			}
		}
		
		// The data is not cached, so read it from the storage
		//

		FilePositionType position = _getPosition(clusterIndex);

		FileHandle file = mVolumeManager.getLowLevelFileAccess().getClusterDataFile(AccessMode::AM_READ);
		SFAT_ASSERT(file.isOpen(), "The cluster/directory data file should be open!");

		size_t bytesRead = 0;
		ErrorCode err = file.readAtPosition(buffer.data(), mClusterSize, position, bytesRead);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X reading cluster!", err);
			return err;
		}
		if (bytesRead != mClusterSize) {
			return ErrorCode::ERROR_READING_CLUSTER_DATA;
		}

		err = mVolumeManager.verifyCRCOnRead(buffer, clusterIndex);
		if (err != ErrorCode::RESULT_OK) {
			return err;
		}

		if (isDirectoryData) {
			// We need to add this data to the cache now
			ClusterDataCache clusterCache;
			clusterCache.mClusterIndex = clusterIndex;
			clusterCache.mIsCacheInSync = true;
			clusterCache.mBuffer.resize(mClusterSize);
			memcpy(clusterCache.mBuffer.data(), buffer.data(), mClusterSize);
			mCachedClusters.insert(std::pair<ClusterIndexType, ClusterDataCache>(clusterIndex, std::move(clusterCache)));
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode DataBlockManager::writeCluster(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex, bool isDirectoryData) {
		SFATLockGuard guard(mClusterReadWriteMutex);

		ErrorCode err = ErrorCode::RESULT_OK;
		bool clusterWritten = false;
		if (!mVolumeManager.isInTransaction() || !isDirectoryData) {
			// When not in transaction, we have to write the cluster on spot.
			err = _writeCluster(buffer, clusterIndex);
			clusterWritten = (err == ErrorCode::RESULT_OK);
			if (!clusterWritten) {
				SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Failed to write cluster data!");
			}
		}
		if (isDirectoryData) {
			// Check first if we have the cluster data cached
			ClusterDataCache clusterCache;
			auto res = mCachedClusters.insert(std::pair<ClusterIndexType, ClusterDataCache>(clusterIndex, clusterCache));
			ClusterDataCache& clusterData = res.first->second;
			if (res.second) {
				// A new cache element has been just inserted. Initialize it
				clusterData.mBuffer.resize(mClusterSize);
				clusterData.mClusterIndex = clusterIndex;
			}
			memcpy(clusterData.mBuffer.data(), buffer.data(), mClusterSize);
			clusterData.mIsCacheInSync = clusterWritten;
		}

		if (err == ErrorCode::RESULT_OK) {
			err = mVolumeManager.updateCRCOnWrite(buffer, clusterIndex);
		}
			
		return err;
	}

	ErrorCode DataBlockManager::_writeCluster(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex) {
		FilePositionType position = _getPosition(clusterIndex);

		FileHandle file = mVolumeManager.getLowLevelFileAccess().getClusterDataFile(AccessMode::AM_WRITE);
		SFAT_ASSERT(file.isOpen(), "The cluster data file should be open!");
		SFAT_ASSERT(buffer.size() >= mClusterSize, "The buffer size should be at least one cluster big in size!");

		size_t bytesWritten = 0;
		ErrorCode err = file.writeAtPosition(buffer.data(), mClusterSize, position, bytesWritten);
		if (err != ErrorCode::RESULT_OK) {
			SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X writing cluster!", err);
			return err;
		}
		if (bytesWritten != mClusterSize) {
			return ErrorCode::ERROR_WRITING_CLUSTER_DATA;
		}

		return ErrorCode::RESULT_OK;
	}

	ErrorCode DataBlockManager::flush() {
		SFATLockGuard guard(mClusterReadWriteMutex);

		for (auto& elem : mCachedClusters) {
			ClusterDataCache& clusterCache = elem.second;
			if (!clusterCache.mIsCacheInSync) {
				ErrorCode err = _writeCluster(clusterCache.mBuffer, clusterCache.mClusterIndex);
				if (err != ErrorCode::RESULT_OK) {
					return err;
				}
				clusterCache.mIsCacheInSync = true;
			}
		}
		return ErrorCode::RESULT_OK;
	}

	//For testing purposes only
#if !defined(MCPE_PUBLISH)
	//To be used for simulation of missed data flush.
	ErrorCode DataBlockManager::discardCachedChanges() {
		SFATLockGuard guard(mClusterReadWriteMutex);

		FileHandle file = mVolumeManager.getLowLevelFileAccess().getClusterDataFile(AccessMode::AM_READ);
		SFAT_ASSERT(file.isOpen(), "The cluster/directory data file should be open!");

		for (auto& elem : mCachedClusters) {
			ClusterDataCache& clusterCache = elem.second;
			if (!clusterCache.mIsCacheInSync) {

				FilePositionType position = _getPosition(clusterCache.mClusterIndex);

				size_t bytesRead = 0;
				ErrorCode err = file.readAtPosition(clusterCache.mBuffer.data(), mClusterSize, position, bytesRead);
				if (err != ErrorCode::RESULT_OK) {
					SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Error #%08X reading cluster!", err);
					return err;
				}
				if (bytesRead != mClusterSize) {
					return ErrorCode::ERROR_READING_CLUSTER_DATA;
				}

				clusterCache.mIsCacheInSync = true;
			}
		}
		return ErrorCode::RESULT_OK;
	}
#endif //!defined(MCPE_PUBLISH)


} // namespace SFAT

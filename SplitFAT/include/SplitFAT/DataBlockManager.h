/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/Common.h"
#include "SplitFAT/utils/Mutex.h"
#include <vector>
#include <map>

//Forward declaration of the UnitTest class
#if !defined(MCPE_PUBLISH)
class LowLevelUnitTest;
#endif //!defined(MCPE_PUBLISH)

namespace SFAT {

	class VolumeManager;

	struct ClusterDataCache {
		ClusterIndexType mClusterIndex;
		std::vector<uint8_t> mBuffer;
		bool mIsCacheInSync;
	};

	class DataBlockManager {

#if !defined(MCPE_PUBLISH)
		friend class LowLevelUnitTest;
#endif //!defined(MCPE_PUBLISH)

	public:
		DataBlockManager(VolumeManager& volumeManager);
		~DataBlockManager();
		bool canExpand() const;
		ErrorCode flush();

		ErrorCode readCluster(std::vector<uint8_t>& buffer, ClusterIndexType clusterIndex, bool isDirectoryData);
		ErrorCode writeCluster(const std::vector<uint8_t>& buffer, ClusterIndexType clusterIndex, bool isDirectoryData);

		//For testing purposes only
#if !defined(MCPE_PUBLISH)
		ErrorCode discardCachedChanges();
#endif //!defined(MCPE_PUBLISH)

	private:
		FilePositionType _getPosition(ClusterIndexType clusterIndex) const;
		ErrorCode _writeCluster(const std::vector<uint8_t> &buffer, ClusterIndexType clusterIndex);

	private:
		VolumeManager& mVolumeManager;
		uint32_t	mClustersPerFATBlock;
		uint32_t	mMaxPossibleBlocksCount;
		size_t		mClusterSize;
		size_t		mDataBlockSize;
		std::map<ClusterIndexType, ClusterDataCache> mCachedClusters;
		SFATMutex		mClusterReadWriteMutex;
	};
} // namespace SFAT

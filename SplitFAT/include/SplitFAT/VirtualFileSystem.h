/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "SplitFAT/VolumeManager.h"
#include "SplitFAT/RecoveryManager.h"
#include "SplitFAT/DataPlacementStrategyBase.h"
#include "SplitFAT/utils/MemoryBufferPool.h"
#include <stack>

#if !defined(MCPE_PUBLISH)
class VirtualFileSystemTests;
class VirtualFileSystemTests_Creation_Test;
class VirtualFileSystemTests_AppendClusterToChain_Test;
class VirtualFileSystemTests_CreateANewClusterChain_Test;
class VirtualFileSystemTests_WritingFileDescriptorRecord_Test;
class VirtualFileSystemTests_CreateFile_Test;
class VirtualFileSystemTests_ExpandFile_Test;
class VirtualFileSystemTests_CreateFileManipulatorForExistingFile_Test;
class VirtualFileSystemTests_ExpandExistingFile_Test;
class VirtualFileSystemTests_CreateSubdirectory_Test;
class VirtualFileSystemTests_isDirectoryEmpty_Test;
class VirtualFileSystemTests_FileDescriptorRecordFromFirstFileClusterIndex_Test;
class VirtualFileSystemTests_ForwardAndBackwardClusterChainPropagation_Test;
class VirtualFileSystemTests_LastClusterUpdateCreatingSeveralClustersBigFile_Test;
class VirtualFileSystemTests_TruncatingFile_Test;
class VirtualFileSystemTests_MoveClusterNoTransaction_Test;
class BlockVirtualizationUnitTest;
#endif //!defined(MCPE_PUBLISH)

#define SPLIT_FAT_ENABLE_DEFRAGMENTATION	1

namespace SFAT {

	const size_t kMaxFileSize = 2 ^ 30; /// 1GB, where having cluster size of 8192 makes max 131072 clusters per file (cluster chain). 
	const uint32_t kMaxCountNestedDirectories = 32;
	const uint32_t kMaxCountEntitiesInDirectory = 65536; //Big enough number
	const uint32_t kInvalidDirectoryEntityIndex = static_cast<uint32_t>(-1);
	static_assert(kInvalidDirectoryEntityIndex >= kMaxCountEntitiesInDirectory, "The index kInvalidDirectoryEntityIndex shouldn't be allowed");

	struct FileDescriptorRecord;
	struct DescriptorLocation;
	class FileManipulator;
	class PathString;
	class FileStorageBase;

	using FileManipulatorStack = std::stack<FileManipulator>;

	class StackAutoElement {
	public:
		StackAutoElement(FileManipulatorStack& stack);

		~StackAutoElement();

		FileManipulator& getTop();

	private:
		FileManipulatorStack& mStackRef;
	};

#if !defined(MCPE_PUBLISH)
	/**
	 *  Used only for functionality related to the unit-tests.
	 */
	struct ClusterChainNode {
		ClusterChainNode(ClusterIndexType clusterIndex, FATCellValueType cellValue)
			: mClusterIndex(clusterIndex), mCellValue(cellValue) {
		}
		
		ClusterIndexType mClusterIndex;
		FATCellValueType mCellValue;
	};

	/**
	 *  Used only for functionality related to the unit-tests.
	 */
	using ClusterChainVector = std::vector<ClusterChainNode>;
#endif //!defined(MCPE_PUBLISH)


	struct SearchResult {
		ClusterIndexType mStartClusterIndex; /// Should point to the first cluster of the directory
		
		ClusterIndexType mClusterIndex;
		uint32_t		 mRecordIndex;
	};

	class VirtualFileSystem {
#if !defined(MCPE_PUBLISH)
		friend class VirtualFileSystemTests;
		friend class VirtualFileSystemTests_Creation_Test;
		friend class VirtualFileSystemTests_AppendClusterToChain_Test;
		friend class VirtualFileSystemTests_CreateANewClusterChain_Test;
		friend class VirtualFileSystemTests_WritingFileDescriptorRecord_Test;
		friend class VirtualFileSystemTests_CreateFile_Test;
		friend class VirtualFileSystemTests_ExpandFile_Test;
		friend class VirtualFileSystemTests_CreateFileManipulatorForExistingFile_Test;
		friend class VirtualFileSystemTests_ExpandExistingFile_Test;
		friend class VirtualFileSystemTests_CreateSubdirectory_Test;
		friend class VirtualFileSystemTests_isDirectoryEmpty_Test;
		friend class TransactionUnitTest_RestoreFromTransaction_Test;
		friend class VirtualFileSystemTests_FileDescriptorRecordFromFirstFileClusterIndex_Test;
		friend class VirtualFileSystemTests_ForwardAndBackwardClusterChainPropagation_Test;
		friend class VirtualFileSystemTests_LastClusterUpdateCreatingSeveralClustersBigFile_Test;
		friend class VirtualFileSystemTests_TruncatingFile_Test;
		friend class VirtualFileSystemTests_MoveClusterNoTransaction_Test;
		friend class BlockVirtualizationUnitTest;
#endif //!defined(MCPE_PUBLISH)

		friend class RecoveryManager;

	public:
		VirtualFileSystem();
		~VirtualFileSystem();

		ErrorCode setup(std::shared_ptr<SplitFATConfigurationBase> lowLevelFileAccess);

		ErrorCode createFile(const PathString& filePath, uint32_t accessMode, bool isBinaryFile, FileManipulator& outputFileManipulator);
		ErrorCode createDirectory(const PathString& directiryPath, FileManipulator& outputFileManipulator);
		ErrorCode renameFile(const PathString& filePath, const PathString& newName);
		ErrorCode renameDirectory(const PathString& directoryPath, const PathString& newName);
		/**
		 * Creates file-manipulator for an existing file without specified access-mode.
		 */
		ErrorCode createGenericFileManipulatorForFilePath(const PathString& filePath, FileManipulator& fileManipulator);
		/**
		 * Creates file-manipulator for an existing file or directory without specified access-mode.
		 */
		ErrorCode createGenericFileManipulatorForExistingEntity(PathString entiryPath, FileManipulator& fileManipulator);
		ErrorCode seek(FileManipulator& fileManipulator, FilePositionType offset, SeekMode mode);
		ErrorCode truncateFile(FileManipulator& fileManipulator, size_t newSize);
		ErrorCode deleteFile(const PathString& filePath);
		ErrorCode removeDirectory(const PathString& directoryPath);
		ErrorCode flush(FileManipulator& fileManipulator);
		ErrorCode read(FileManipulator& fileManipulator, void* buffer, size_t sizeToRead, size_t& sizeRead);
		ErrorCode write(FileManipulator& fileManipulator, const void* buffer, size_t sizeToWrite, size_t& sizeWritten);
		ErrorCode iterateThroughDirectory(const PathString& directoryPath, uint32_t flags, DirectoryIterationCallback callback);

		bool fileExists(const PathString& path);
		bool directoryExists(const PathString& path);
		bool fileOrDirectoryExists(const PathString& path);
		ErrorCode getCountFreeClusters(uint32_t& countFreeClusters, uint32_t blockIndex);
		ErrorCode getFreeSpace(FileSizeType& countFreeBytes);

		ErrorCode removeVolume();

		//
		// Transaction control functions
		//
		bool isInTransaction() const;
		ErrorCode startTransaction();
		ErrorCode endTransaction();
		ErrorCode tryRestoreFromTransactionFile();

		//
		// Defragmentation and DataPlacementStrategy
		//
		ErrorCode moveCluster(ClusterIndexType sourceClusterIndex, ClusterIndexType destClusterIndex);

		//
		// To be used for unit and functional tests
		//
		ErrorCode executeDebugCommand(const std::string& path, const std::string& command);

		//
		// Functionality for analysis and verification of the SplitFAT integrity
		//
		ErrorCode findFileFromCluster(ClusterIndexType clusterIndex, FileManipulator& fileManipulator);
		ErrorCode createFullFilePathFromCluster(ClusterIndexType clusterIndex, std::string& fullFilePath);
		ErrorCode createFullFilePathFromFileManipulator(const FileManipulator& fileManipulator, std::string& fullFilePath);

	private:
		//
		// Functions that work directly with PathString and FileManipulator
		//

		// Creates a file-manipulator for an existing directory
		ErrorCode _createFileManipulatorForDirectoryPath(PathString directoryPath, FileManipulator& fileManipulator);
		// Creates file or directory
		ErrorCode _createEntity(const PathString& path, uint32_t mAccessMode, uint32_t attributes, FileManipulator& outputFileManipulator);
		ErrorCode _renameEntity(const PathString& entityPath, const PathString& newName);
		/**
		 * Performs recursive iteration through all records that satisfy the filter flags.
		 * Skips the hidden, deleted records. Stops the iteration not later than the first empty record.
		 */
		ErrorCode _iterateThroughDirectoryRecursively(const PathString& directoryPath, uint32_t flags, DirectoryIterationCallbackInternal callback);

		//
		// Functions that deal with FileDescriptorRecord and FileManipulator
		//

		ErrorCode _findRecordInDirectory(FileManipulator& parentDirFM, const std::string& entityName, FileManipulator& outputFileManipulator);
		/**
		 * Performs a flat iteration through all FileDescriptorRecords inside a directory.
		 * Will only skip hidden records, but go through everything else, even deleted or empty ones.
		 */
		ErrorCode _iterateThroughDirectory(FileManipulator& parentDirFM, DirectoryIterationCallbackInternal callback);
		/**
		 * Performs recursive iteration through all records that satisfy the filter flags.
		 * Skips the hidden, deleted records. Stops the iteration not later than the first empty record.
		 */
		ErrorCode _iterateThroughDirectoryRecursively(FileManipulatorStack& fwStack, uint32_t flags, DirectoryIterationCallbackInternal callback);
		ErrorCode _isDirectoryEmpty(FileManipulator& parentDirFM, bool& result);
		ErrorCode _getFileSize(const FileManipulator& fileManipulator, size_t& fileSize) const;
		ErrorCode _createEntity(FileManipulator& parentDirFM, const std::string& entityName, uint32_t accessMode, uint32_t attributes, FileManipulator& outputFileManipulator);
		ErrorCode _trunc(FileManipulator& fileManipulator, size_t newSize, bool deleteIfEmpty = false);
		ErrorCode _deleteFile(FileManipulator& fileManipulator);
		ErrorCode _removeDirectory(FileManipulator& fileManipulator);

		ErrorCode _createRoot();
		/**
		 * Expands the file when necessary and if it is possible, and sets the new position.
		 * The expanding depends on the file access mode and available storage.
		 */
		ErrorCode _updatePosition(FileManipulator& fileManipulator, size_t nextWriteSize);
		/**
		 * To be used when the file is already expanded to the required size or at least to the size that is possible
		 */
		ErrorCode _updatePosition(FileManipulator& fileManipulator);
		ErrorCode _writeFileDescriptor(const FileManipulator& fileManipulator);
		ErrorCode _expandFile(FileManipulator& fileManipulator, size_t newSize);
		
		//
		// Functions that work with cluster-indices and FileDescriptorRecord
		//

		ErrorCode _createRootDirFileManipulator(FileManipulator& fileManipulator);
		ErrorCode _createFileManipulatorForDirectory(const DescriptorLocation& location, const FileDescriptorRecord& record, FileManipulator& fileManipulator);
		ErrorCode _createFileManipulatorForExisting(const DescriptorLocation& location, const FileDescriptorRecord& record, uint32_t accessMode, FileManipulator& fileManipulator);

		/**
		 *	Finds the cluster index for particular position in a file, given the file's FileDescriptorRecord.
		 *
		 *	@param record This is the FileDescriptorRecord which provides the start cluster of the file.
		 *	@param position The position in the file.
		 *	@param[out] clusterIndex The cluster index of the corresponding position is returned here. The value will be ClusterValue::INVALID_VALUE if the position is greater or equal to the file-size.
		 *	@returns Corresponding error code. On error the output parameter clusterIndex is not modified.
		 */
		ErrorCode _getClusterForPosition(const FileDescriptorRecord& record, size_t position, ClusterIndexType& clusterIndex);

		/**
		 *  Appends a new allocated cluster to the end of the chain. Requires the end-of-chain cluster index.
		 */
		ErrorCode _appendClusterToEndOfChain(const DescriptorLocation& location, ClusterIndexType endOfChainClusterIndex, ClusterIndexType& allocatedClusterIndex, bool useFileDataStorage);

		/**
		 * Iterates through a chain of clusters
		 *
		 * @param doQuit The callback function should set this parameter to true to quit the iteration earlier.
		 * @param startClusterIndex The starting cluster index. An error is returned if the cluster index is not valid.
		 * @param maxClusterCount Limits the count of clusters to be iterated through. The default value of 0, means the limit is the total amount of clusters allocated in the volume.
		 * @returns ErrorCode::RESULT_OK on success or the corresponding error code.
		 */
		ErrorCode _iterateThroughClusterChain(ClusterIndexType startClusterIndex,
			std::function<ErrorCode(bool& doQuit, ClusterIndexType currentCluster, FATCellValueType cellValue)> callback, 
									bool iterateForward = true, uint32_t maxClusterCount = 0);
		
		ErrorCode _findFreeCluster(ClusterIndexType& newClusterIndex, bool useFileDataStorage);

		/**
		 *  Expands cluster chain with multiple clusters added at the end. Returns the index of the last cluster.
		 *  The location of the FileDescriptorRecord is encoded into the cell-values corresponding to the first and last clusters of the chain.
		 */
		ErrorCode _expandClusterChain(const FileManipulator& fileManipulator, uint32_t countClusters, ClusterIndexType& resultStartClusterIndex, ClusterIndexType& resultEndClusterIndex, bool useFileDataStorage);
		ErrorCode _getCountClusters(ClusterIndexType startClusterIndex, uint32_t& countClusters, ClusterIndexType& lastClusterIndex);

		FileDescriptorRecord *_getFileDescriptorRecordInCluster(uint8_t *clusterData, uint32_t relativeClusterIndex);
		uint32_t _getClusterSize() const;
		uint32_t _getRecordsPerCluster() const;

		void _logReadingError(ErrorCode err, const FileManipulator& fileManipulator);

#if !defined(MCPE_PUBLISH)
		//
		// For debugging only
		//
		ErrorCode _printClusterChain(ClusterIndexType startClusterIndex);
		ErrorCode _loadClusterChain(ClusterIndexType startClusterIndex, ClusterChainVector &clusterChain);
		/**
		 * Finds the last cluster in a chain of clusters. Currently used only for verification.
		 * The fastest altertative to this fucntion is to get the last cluster index directly from the FileDescriptorRecord.mLastCluster
		 */
		ErrorCode _findLastClusterInChain(ClusterIndexType startClusterIndex, ClusterIndexType& endOfChainClusterIndex);
		ErrorCode _findFirstClusterInChain(ClusterIndexType clusterIndex, ClusterIndexType& startOfChainClusterIndex);
		ErrorCode _testReadFile(const std::string& path);
#endif //!defined(MCPE_PUBLISH)

		ErrorCode _findFileDescriptorFromCluster(ClusterIndexType clusterIndex, FileDescriptorRecord& descriptorFound, ClusterIndexType& descriptorClusterIndex, uint32_t& relativeRecordIndex);

	private:
		VolumeManager mVolumeManager;
		bool mIsValid;
		std::unique_ptr<RecoveryManager> mRecoveryManager;

		//Cached values
		uint32_t mClusterSize;

		uint32_t getFileDescriptorRecordStorageSize() const;
		uint32_t getCountClustersForSize(size_t size) const;

		std::unique_ptr<MemoryBufferPool> mMemoryBufferPool;
#if (SPLIT_FAT_ENABLE_DEFRAGMENTATION == 1)
		std::shared_ptr<DataPlacementStrategyBase>	mDefragmentation;
#endif
	};

}

/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#ifndef _SPLIT_FAT_COMMON_H_
#define _SPLIT_FAT_COMMON_H_

#include <stdint.h>
#include <functional>
#include "SplitFAT/utils/BitSet.h"
#include "SplitFAT/utils/SFATAssert.h"
#include "SplitFAT/FATCellValue.h"

namespace SFAT {

	/*
		1. Physical storage
			- (FATDataStorage) FAT and Control data file - aroung 100MB
			- (ClusterDataStorage) Data-blocks file - could become 10GB+

		2. Data layout

		FATDataStorage: [VolumeDescriptor] [VolumeControlData] [[BlockControlData][FATDataBlock]] ... [[BlockControlData][FATDataBlock]]

		ClusterDataStorage: [ClusterDataBlock] ... [ClusterDataBlock]

		-----------------------------------------------------------------------------------------------------------------------------------

		VolumeDescriptor	-	Stores the general parameters of the volume.
		VolumeControlData	-	To keep track of the blocks of data. Updated when a new FATDataBlock / ClusterDataBlock is added.

		BlockControlData	-	Keeps control data for the corresponding FAT-block and Cluster-block.
								Created for every FATDataBlock and corresponding ClusterDataBlock.
								The type of data that it keeps is like this - number of free clusters, CRC32 for the FAT, etc.

		ClusterDataBlock	-	Stores pure cluster data.

	*/

	enum class ErrorCode : uint32_t {
		RESULT_OK = 0,
		UNKNOWN_ERROR,
		ERROR_LOW_LEVEL_STORAGE_IS_NOT_SETUP,
		ERROR_CLUSTER_DATA_STORAGE_NOT_AVAILABLE,
		ERROR_CAN_NOT_GET_AVAILABLE_STORAGE_SPACE,
		ERROR_CREATING_FILE,
		ERROR_CREATING_FILE_LOW_LEVEL,
		ERROR_OPENING_FILE_LOW_LEVEL,
		ERROR_OPENING_FILE_NOT_FOUND,
		ERROR_FILE_COULD_NOT_BE_FOUND,
		ERROR_DIRECTORY_NOT_FOUND,
		ERROR_CLOSING_FILE_LOW_LEVEL,
		ERROR_CLOSING_NOT_OPENED_LOW_LEVEL,
		ERROR_FILE_NOT_OPENED_LOW_LEVEL,
		ERROR_POSITIONING_IN_FILE_LOW_LEVEL,
		ERROR_POSITIONING_OUT_OF_RANGE,
		ERROR_FLUSH_LOW_LEVEL,
		ERROR_GETTING_FILE_SIZE,
		ERROR_CAN_NOT_GET_FILE_SIZE_OF_DIRECTORY,
		ERROR_GETTING_FILE_STATUS,
		ERROR_WRITING,
		ERROR_WRITING__INVALID_CACHE,
		ERROR_WRITING_LOW_LEVEL,
		ERROR_READING,
		ERROR_READING_LOW_LEVEL,
		ERROR_READING_CLUSTER_DATA,
		ERROR_READING_CLUSTER_DATA_CRC_DOES_NOT_MATCH,
		ERROR_WRITING_CLUSTER_DATA,
		ERROR_DELETING_FILE_LOW_LEVEL,
		ERROR_REMOVING_DIRECTORY_LOW_LEVEL,
		ERROR_RENAMING_FILE_LOW_LEVEL,
		ERROR_RENAMING_DIRECTORY_LOW_LEVEL,
		ERROR_VOLUME_CAN_NOT_EXPAND,
		ERROR_VOLUME_CAN_NOT_BE_OPENED,
		ERROR_VOLUME_TRANSACTION_ERROR,
		ERROR_VOLUME_RESTORE_FROM_TRANSACTION_ERROR,
		ERROR_EXPANDING_DATA_BLOCK,
		ERROR_BLOCK_INDEX_OUT_OF_RANGE,
		ERROR_FAT_NOT_CACHED,
		ERROR_INVALID_FAT_BLOCK_INDEX,
		ERROR_TRYING_TO_READ_NOT_ALLOCATED_FAT_BLOCK,
		ERROR_SFAT_CANT_RECOVER,
		ERROR_SFAT_CANT_BE_RECREATED,
		ERROR_WRITING_INVALID_FAT_CELL_VALUE,
		ERROR_INVALID_CLUSTER_INDEX,
		ERROR_ITERATING_THROUGH_CLUSTER_CHAIN,
		ERROR_INCONSISTENCY,
		ERROR_INCONSISTENCY_POINTING_TO_FREE_CLUSTER,
		ERROR_CAN_NOT_MOVE_CLUSTER,
	
		//High Level
		ERROR_FILE_ACCESS_MODE_UNSPECIFIED,
		ERROR_TRYING_TO_READ_FILE_WITHOUT_READ_ACCESS_MODE,
		ERROR_EXPANDING_FILE_IN_READ_ACCESS_MODE,
		ERROR_REACHED_MAX_DIRECTORY_DEPTH,
		ERROR_INVALID_FILE_MANIPULATOR,
		ERROR_NOT_ENOUGH_BUFFER_SIZE,
		ERROR_NULL_POINTER_MEMORY_BUFFER,
		ERROR_ALLOCATING_MEMORY_BUFFER,
		ERROR_INVALID_SEEK_PARAMETERS,
		ERROR_MAXIMUM_ALLOWED_FILES_PER_DIRECTORY_REACHED,
		ERROR_FILE_OR_DIRECTORY_WITH_SAME_NAME_ALREADY_EXISTS,
		ERROR_PARENT_DIRECTORY_DOES_NOT_EXIST,
		ERROR_INVALID_FILE_PATH,
		ERROR_CAN_NOT_TRUNCATE_FILE_TO_BIGGER_SIZE,
		ERROR_CAN_NOT_DELETE_ROOT_DIRECTORY,
		ERROR_CANT_REMOVE_NOT_EMPTY_DIRECTORY,
		ERROR_CANT_RENAME_A_FILE__NAME_DUPLICATION,
		ERROR_CAN_NOT_GET_FILE_POSITION,
		ERROR_FILE_NOT_OPENED,
		ERROR_FEATURE_NOT_SUPPORTED,
		ERROR_TRANSACTION_IS_ALREADY_STARTED,
		ERROR_NO_TRANSACTION_HAS_BEEN_STARTED,
		ERROR_NO_TRANSACTION_FILE_FOUND,
		ERROR_FATAL_ERROR, //TODO: Define more error types.

		//Integrity errors
		ERROR_FAT_INTEGRITY, // Clusters that are allocated, but to refered from any file or directory
		ERROR_FILES_INTEGRITY, // FileDescriptorRecords with not consistent content of any kind (file size, pointing to not allocated clusters, etc.)
		ERROR_INTEGRITY, // Integrity error of any kind (both types above combined).

		NOT_IMPLEMENTED = 0xFFFFFFFF
	};


	enum class BlockIndexValues : uint32_t {
		INVALID_VALUE = 0xFFFFFFFF,
	};

	inline bool isValidBlockIndex(uint32_t blockIndex) {
		return (blockIndex != static_cast<uint32_t>(BlockIndexValues::INVALID_VALUE));
	}

	inline FilePositionType sizeToPosition(FileSizeType size) {
		FilePositionType pos = static_cast<FilePositionType>(size);
		SFAT_ASSERT(pos >= 0, "The position can't be negative!");
		return pos;
	}

	enum DefaultSetupValues : uint32_t {
		CLUSTER_SIZE = 8 * (1 << 10), // 8KB
		CHUNK_SIZE = 256 * (1 << 10), // 256KB
		DATA_BLOCK_SIZE = 256 * (1 << 20), // 256MB
		FIRST_FILE_DATA_BLOCK_INDEX = 1
	};

} // namespace SFAT

#endif //_SPLIT_FAT_COMMON_H_

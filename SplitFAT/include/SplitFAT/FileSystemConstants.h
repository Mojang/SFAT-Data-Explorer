/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

namespace SFAT {

	enum class SeekMode {
		SM_SET,		// SEEK_SET
		SM_CURRENT,	// SEEK_CUR
		SM_END		// SEEK_END
	};

	enum AccessMode {
		AM_UNSPECIFIED = 0,
		AM_READ = 1,   /// Read
		AM_WRITE = 2,  /// Write
		AM_UPDATE = 4, /// Read/Write operations
		AM_APPEND = 8, /// Write only at the end
		AM_TEXT = 16,
		AM_BINARY = 32,
		AM_TRUNCATE = 64,
		AM_CREATE_IF_DOES_NOT_EXIST = 128,
	};

	enum DirectoryIterationFlags : uint32_t {
		DI_FILE = 1,
		DI_DIRECTORY = 1 << 1,
		DI_ALL = DI_FILE | DI_DIRECTORY,
		DI_RECURSIVE = 1 << 2,
	};

} // namespace SFAT
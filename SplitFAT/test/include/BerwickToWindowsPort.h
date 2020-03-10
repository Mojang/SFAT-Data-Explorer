/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#include <mutex>
#include "SplitFAT/utils/Logger.h"

// Berwick

enum BerwickFileOpenFlags {
	SCE_KERNEL_O_APPEND = 1,
	SCE_KERNEL_O_TRUNC = 2,
	SCE_KERNEL_O_CREAT = 4,
	SCE_KERNEL_O_EXCL = 8,
	SCE_KERNEL_O_WRONLY = 16,
	SCE_KERNEL_O_RDONLY = 32,
	SCE_KERNEL_O_DIRECT = 64,
	SCE_KERNEL_O_FSYNC = 128,
	SCE_KERNEL_O_RDWR = SCE_KERNEL_O_RDONLY | SCE_KERNEL_O_WRONLY,
	SCE_KERNEL_S_IRWU = 256,
};

enum BerwickFlags {
	SCE_KERNEL_SEEK_SET,
	SCE_KERNEL_SEEK_CUR,
	SCE_KERNEL_SEEK_END,

	SCE_KERNEL_LWFS_DISABLE,
	SCE_KERNEL_LWFS_ENABLE,

	/* for sceKernelLwfsLseek */
	SCE_KERNEL_LWFS_SEEK_SET,
	SCE_KERNEL_LWFS_SEEK_CUR,
	SCE_KERNEL_LWFS_SEEK_END,
	SCE_KERNEL_LWFS_SEEK_DATAEND,
};

#define SCE_OK	0
#define SCE_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE	128
#define SCE_FIOS_OK 0
#define SCE_FIOS_STATUS_DIRECTORY	1
#define SCE_KERNEL_MAIN_DMEM_SIZE	(256 << 20)
#define SCE_KERNEL_WB_ONION	0
#define SCE_KERNEL_PROT_CPU_RW 0

#define NOT_IMPLEMENTED_FUNCTION	-1;

struct SceAppContentMountPoint {
	char	data[SCE_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE];
};

typedef uint16_t SceKernelMode;
typedef int64_t ssize_t;

struct SceKernelStat {
	int64_t st_size;
};

struct SceFiosOpAttr {
};

struct SceFiosStat {
	int64_t fileSize;
	uint32_t statFlags;
};

ssize_t sceKernelRead(int fd, void *buf, size_t nbytes);
ssize_t sceKernelWrite(int fd, const void *buf, size_t nbytes);
int sceKernelOpen(const char *path, int flags, SceKernelMode mode);
int sceKernelClose(int fd);
int sceKernelUnlink(const char *path);
int sceKernelFsync(int fd);
int sceKernelRename(const char *from, const char *to);
int sceKernelMkdir(const char *path, SceKernelMode mode);
int sceKernelRmdir(const char *path);
int sceKernelStat(const char *path, SceKernelStat *sb);
int sceKernelFstat(int fd, SceKernelStat *sb);
ssize_t sceKernelPread(int fd, void *buf, size_t nbytes, off_t offset);
//ssize_t sceKernelPwrite(int fd, const void *buf, size_t nbytes, off_t offset);
off_t sceKernelLseek(int fd, off_t offset, int whence);
//int sceKernelTruncate(const char *path, off_t length);
//int sceKernelFtruncate(int fd, off_t length);
int sceKernelLwfsSetAttribute(int fd, int flags);
int sceKernelLwfsAllocateBlock(int fd, off_t size);
int sceKernelLwfsTrimBlock(int fd, off_t size);
off_t sceKernelLwfsLseek(int fd, off_t offset, int whence);
ssize_t sceKernelLwfsWrite(int fd, const void *buf, size_t nbytes);

int sceFiosStatSync(const SceFiosOpAttr *pAttr, const char *pPath, SceFiosStat *pOutStatus);
bool sceFiosFileExistsSync(const SceFiosOpAttr *pAttr, const char *pPath);
bool sceFiosDirectoryExistsSync(const SceFiosOpAttr *pAttr, const char *pPath);
bool sceFiosExistsSync(const SceFiosOpAttr *pAttr, const char *pPath);

int32_t sceAppContentDownloadDataGetAvailableSpaceKb(const SceAppContentMountPoint	*mountPoint, size_t	*availableSpaceKb);

// Memory
int32_t sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t len, size_t alignment, int memoryType, off_t *physAddrOut);
int32_t sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags, off_t directMemoryStart, size_t maxPageSize);
int32_t sceKernelCheckedReleaseDirectMemory(off_t start, size_t len);

// Errors
#define SCE_KERNEL_ERROR_ENOENT 0x80020002

// MC

#define UNUSED_PARAMETER(param) { (void)(param); }
#define UNUSED1(param) { (void)(param); }
#define UNUSED2(param0, param1) { (void)(param0); (void)(param1); }
#define UNUSED3(param0, param1, param2) { (void)(param0); (void)(param1); (void)(param2); }
#define UNUSED4(param0, param1, param2, param3) { (void)(param0); (void)(param1); (void)(param2); (void)(param3); }
#define UNUSED5(param0, param1, param2, param3, param4) { (void)(param0); (void)(param1); (void)(param2); (void)(param3); (void)(param4); }

namespace Bedrock {
	namespace Threading {
		using RecursiveMutex = std::recursive_mutex;

		using Mutex = std::mutex;
		
		template <typename T>
		using LockGuard = std::lock_guard<T>;
	}
}

#define LOG_AREA_PLATFORM ::SFAT::LogArea::LA_EXTERNAL_AREA_PLATFORM
#define LOG_AREA_FILE  ::SFAT::LogArea::LA_EXTERNAL_AREA_FILE

#define ALOGI(mcLogArea, ...) { ::SFAT::SFAT_LOGI( mcLogArea, __VA_ARGS__); }
#define ALOGW(mcLogArea, ...) { ::SFAT::SFAT_LOGW( mcLogArea, __VA_ARGS__); }
#define ALOGE(mcLogArea, ...) { ::SFAT::SFAT_LOGE( mcLogArea, __VA_ARGS__); }

#define DEBUG_ASSERT( condition, message) { assert((condition) && message); }
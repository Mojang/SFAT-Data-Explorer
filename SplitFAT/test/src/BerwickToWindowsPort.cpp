/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "BerwickToWindowsPort.h"
#include "SplitFAT/utils/Mutex.h"
#include "SplitFAT/utils/Logger.h"
#include "SplitFAT/utils/SFATAssert.h"
#include <map>
#include <vector>
#include <direct.h>
#ifdef _WIN32
#	include <Shlwapi.h>
#	pragma comment(lib, "Shlwapi.lib") //Used for PathFileExistsA()
#	include <sys/stat.h>
#endif

using namespace ::SFAT;

class FileHandlePull {
public:
	FileHandlePull() : mCounter(0) {
	}

	~FileHandlePull() {
		SFATLockGuard guard(mMutex);

		for (auto it : mFileHandleMap) {
			if (it.second != nullptr) {
				int res = fclose(it.second);
				SFAT_ASSERT(res != -1, "Was not able to close a file ");
				it.second = nullptr;
			}
		}
	}

	FILE* getFileHandle(int fd) {
		SFATLockGuard guard(mMutex);

		auto it = mFileHandleMap.find(fd);
		if (it != mFileHandleMap.end()) {
			return it->second;
		}

		SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "Trying to access unknown file handle!");
		return nullptr;
	}

	void setFileHandle(int fd, FILE* file) {
		SFATLockGuard guard(mMutex);

		auto it = mFileHandleMap.find(fd);
		if (it != mFileHandleMap.end()) {
			//SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "The same file descriptor (%u) is already registered!", fd);
			it->second = file;
		}
		else {
			mFileHandleMap.emplace(fd, file);
		}
	}

	int registerFileHandle(FILE* file) {
		SFATLockGuard guard(mMutex);

		int fd = mCounter;
		mFileHandleMap.emplace(fd, file);
		++mCounter;

		return fd;
	}

	void unregisterFileHandle(int fd) {
		SFATLockGuard guard(mMutex);

		mFileHandleMap.erase(fd);
	}

private:
	SFATMutex mMutex;
	std::map<int, FILE*> mFileHandleMap;
	int mCounter;
};

class BerwickEmulation {
private:
	BerwickEmulation() {
	}

public:
	~BerwickEmulation() {
	}

	static BerwickEmulation& getInstance() {
		if (mInstance == nullptr) {
			SFATLockGuard guard(mMutex);
			if (mInstance == nullptr) {
				mInstance = new BerwickEmulation();
			}
		}
		return *mInstance;
	}

	FileHandlePull& getFileHandlePull() {
		return mFileHandlePull;
	}

	int allocateGlobalMemoryBuffer(size_t byteSize) {
		mGlobalMemoryBuffer.resize(byteSize);
		return 0;
	}

	int releaseGlobalMemoryBuffer() {
		mGlobalMemoryBuffer.clear();
		return 0;
	}

	void* getGlobalMemoryBufferPtr() {
		if (mGlobalMemoryBuffer.size() == 0) {
			return nullptr;
		}
		return mGlobalMemoryBuffer.data();
	}

private:
	static SFATMutex mMutex;
	static BerwickEmulation* mInstance;

	FileHandlePull mFileHandlePull;
	std::vector<uint8_t> mGlobalMemoryBuffer;
};

BerwickEmulation* BerwickEmulation::mInstance = nullptr;
SFATMutex BerwickEmulation::mMutex;



ssize_t sceKernelRead(int fd, void *buf, size_t nbytes) {
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);
	size_t sizeRead = fread(buf, 1, nbytes, file);

	return static_cast<ssize_t>(sizeRead);
}

ssize_t sceKernelWrite(int fd, const void *buf, size_t nbytes) {
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);
	size_t sizeWritten = fwrite(buf, 1, nbytes, file);

	return static_cast<ssize_t>(sizeWritten);
}

int sceKernelOpen(const char *path, int flags, SceKernelMode mode) {
	//TODO: Curretly whence is not used. Implement for completeness.
	UNUSED1(mode);
	std::string modeStr;
	if ((0 != (flags & SCE_KERNEL_O_CREAT)) || (0 != (flags & SCE_KERNEL_O_TRUNC))){
		if (flags & SCE_KERNEL_O_APPEND) {
			modeStr += "a";
		}
		else {
			modeStr += "w";
		}
		SFAT_ASSERT(0 == (flags & SCE_KERNEL_O_EXCL), "");

		if (flags & SCE_KERNEL_O_RDONLY) {
			modeStr += "+";
		}
	}
	else if ((flags & SCE_KERNEL_O_RDONLY) || (flags & SCE_KERNEL_O_WRONLY)) {
		modeStr += "r";

		if (flags & SCE_KERNEL_O_WRONLY) {
			modeStr += "+";
		}
	}

	modeStr += "b";

	FILE* file = fopen(path, modeStr.c_str());
	int fd = -1;
	if (file != nullptr) {
		fd = BerwickEmulation::getInstance().getFileHandlePull().registerFileHandle(file);
	}

	return fd;
}

int sceKernelClose(int fd) {
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);
	if (nullptr == file) {
		return -1;
	}

	int res = fclose(file);
	BerwickEmulation::getInstance().getFileHandlePull().unregisterFileHandle(fd);

	return res;
}

int sceKernelUnlink(const char *path) {
	int res = remove(path);
	if (res != 0) {
		char buf[64];
		if (strerror_s(buf, sizeof(buf), errno) != 0)
		{
			snprintf(buf, sizeof(buf), "Unknown error:%u", errno);
		}
		SFAT_LOGE(LogArea::LA_PHYSICAL_DISK, "File remove error: %s", buf);
		return res;
	}
	return 0;
}

int sceKernelFsync(int fd) {
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);
	if (nullptr == file) {
		return -1;
	}

	int res = fflush(file);
	return res;
}

int sceKernelRename(const char *from, const char *to) {
	return rename(from, to);
}

int sceKernelMkdir(const char *path, SceKernelMode mode) {
	(void)mode;
	return _mkdir(path);
}

int sceKernelRmdir(const char *path) {
	return _rmdir(path);
}

int sceKernelStat(const char *path, SceKernelStat *sb) {
	if (sb == nullptr) {
		return -1;
	}

	memset(sb, 0, sizeof(SceKernelStat));

	DWORD attribs = ::GetFileAttributesA(path);
	if (attribs == INVALID_FILE_ATTRIBUTES) {
		return SCE_KERNEL_ERROR_ENOENT;
	}

	struct __stat64 buffer;
	int res = _stat64(path, &buffer);
	if (res == 0) {
		sb->st_size = buffer.st_size;
	}

	return res;
}

int sceKernelFstat(int fd, SceKernelStat *sb) {
	UNUSED2(fd, sb);
	return NOT_IMPLEMENTED_FUNCTION;
}

ssize_t sceKernelPread(int fd, void *buf, size_t nbytes, off_t offset) {
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);

	if (file == nullptr) {
		return -1;
	}

	int res = fseek(file, offset, SEEK_SET);
	if (res != 0) {
		res |= 1 << 31;
		return res;
	}

	size_t sizeRead = fread(buf, 1, nbytes, file);

	return static_cast<ssize_t>(sizeRead);
}

//ssize_t sceKernelPwrite(int fd, const void *buf, size_t nbytes, off_t offset) {
//	return NOT_IMPLEMENTED_FUNCTION;
//}

off_t sceKernelLseek(int fd, off_t offset, int whence) {
	//TODO: Curretly whence is not used. Implement for completeness.
	UNUSED1(whence);
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);

	if (file == nullptr) {
		return -1;
	}

	int res = fseek(file, offset, SEEK_SET);
	if (res != 0) {
		res |= 1 << 31;
	}
	return res;
}

//int sceKernelTruncate(const char *path, off_t length) {
//	return NOT_IMPLEMENTED_FUNCTION;
//}

//int sceKernelFtruncate(int fd, off_t length) {
//	return NOT_IMPLEMENTED_FUNCTION;
//}

int sceKernelLwfsSetAttribute(int fd, int flags) {
	UNUSED2(fd, flags);
	return 0; //Should I do something here?
}

int sceKernelLwfsAllocateBlock(int fd, off_t size) {
	UNUSED2(fd, size);
	return 0; //Should I do something here?
}

int sceKernelLwfsTrimBlock(int fd, off_t size) {
	UNUSED2(fd, size);
	return 0; //Should I do something here?
}

off_t sceKernelLwfsLseek(int fd, off_t offset, int whence) {
	//TODO: Curretly whence is not used. Implement for completeness.
	UNUSED1(whence);
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);

	if (file == nullptr) {
		return -1;
	}

	int res = fseek(file, offset, SEEK_SET);
	if (res != 0) {
		res |= 1 << 31;
	}
	return res;
}

ssize_t sceKernelLwfsWrite(int fd, const void *buf, size_t nbytes) {
	FILE* file = BerwickEmulation::getInstance().getFileHandlePull().getFileHandle(fd);
	size_t sizeWritten = fwrite(buf, 1, nbytes, file);

	return static_cast<ssize_t>(sizeWritten);
}

int32_t sceAppContentDownloadDataGetAvailableSpaceKb(const SceAppContentMountPoint	*mountPoint, size_t	*availableSpaceKb) {
	UNUSED2(mountPoint, availableSpaceKb);
	*availableSpaceKb = 16ULL << 20; //16GB / 1024
	return 0;
}

int sceFiosStatSync(const SceFiosOpAttr *pAttr, const char *pPath, SceFiosStat *pOutStatus) {
	UNUSED3(pAttr, pPath, pOutStatus);
	return NOT_IMPLEMENTED_FUNCTION;
}

bool sceFiosFileExistsSync(const SceFiosOpAttr *pAttr, const char *pPath) {
	UNUSED1(pAttr);
	DWORD attribs = ::GetFileAttributesA(pPath);
	if (attribs == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	return (attribs & FILE_ATTRIBUTE_ARCHIVE);
}

bool sceFiosDirectoryExistsSync(const SceFiosOpAttr *pAttr, const char *pPath) {
	UNUSED1(pAttr);
	DWORD attribs = ::GetFileAttributesA(pPath);
	if (attribs == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	return (attribs & FILE_ATTRIBUTE_DIRECTORY);
}

bool sceFiosExistsSync(const SceFiosOpAttr *pAttr, const char *pPath) {
	UNUSED1(pAttr);
	BOOL res = PathFileExistsA(pPath);
	return (res == TRUE);
}


int32_t sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t len, size_t alignment, int memoryType, off_t *physAddrOut) {
	UNUSED5(searchStart, searchEnd, alignment, memoryType, physAddrOut);
	BerwickEmulation::getInstance().allocateGlobalMemoryBuffer(len);
	return 0;
}

int32_t sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags, off_t directMemoryStart, size_t maxPageSize) {
	UNUSED5(len, prot, flags, directMemoryStart, maxPageSize);
	*addr = BerwickEmulation::getInstance().getGlobalMemoryBufferPtr();
	return 0;
}

int32_t sceKernelCheckedReleaseDirectMemory(off_t start, size_t len) {
	UNUSED2(start, len);
	BerwickEmulation::getInstance().releaseGlobalMemoryBuffer();
	return 0;
}
/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#include "SplitFAT/utils/Mutex.h"

namespace SFAT {


	SFATMutex::SFATMutex() : mLocked(false) {
	}

	SFATMutex::~SFATMutex() {
		if (mLocked) {
			mMutex.unlock();
		}
	}

	void SFATMutex::lock() {
		mMutex.lock();
		mLocked = true;
		mThreadId = std::this_thread::get_id();
	}

	void SFATMutex::unlock() {
		mLocked = false;
		mThreadId = std::thread::id();
		mMutex.unlock();
	}

	bool SFATMutex::tryLock() {
		bool res = mMutex.try_lock();
		if (res) {
			mLocked = true;
			mThreadId = std::this_thread::get_id();
		}
		return res;
	}

	bool SFATMutex::isLocked() const {
		return mLocked;
	}

	bool SFATMutex::isSameThread() const {
		if (!mLocked) {
			return false;
		}
		std::thread::id threadId = std::this_thread::get_id();
		return (threadId == mThreadId);
	}


	SFATRecursiveMutex::SFATRecursiveMutex() : mLockCount(0) {
	}

	SFATRecursiveMutex::~SFATRecursiveMutex() {
		while (getLockCount()>0) {
			unlock();
		}
	}

	void SFATRecursiveMutex::lock() {
		mMutex.lock();
		if (mLockCount == 0) {
			mThreadId = std::this_thread::get_id();
		}
		++mLockCount;
	}

	void SFATRecursiveMutex::unlock() {
		if (mLockCount > 0) {
			--mLockCount;
			if (mLockCount == 0) {
				mThreadId = std::thread::id();
			}
		}
		mMutex.unlock();
	}

	bool SFATRecursiveMutex::tryLock() {
		bool res = mMutex.try_lock();
		if (res) {
			if (mLockCount == 0) {
				mThreadId = std::this_thread::get_id();
			}
			++mLockCount;
		}
		return res;
	}

	bool SFATRecursiveMutex::isLocked() const {
		return mLockCount.load() > 0;
	}

	int32_t SFATRecursiveMutex::getLockCount() const {
		return mLockCount.load();
	}

	bool SFATRecursiveMutex::isSameThread() const {
		if (!isLocked()) {
			return false;
		}
		std::thread::id threadId = std::this_thread::get_id();
		return (threadId == mThreadId);
	}


#if (SPLIT_FAT_ENABLE_BEDROCK_MUTEX_IMPLEMENTATION != 1)
	SFATLockGuard::SFATLockGuard(SFATMutex& mutex)
		: mMutex(mutex) {
		mutex.lock();
	}

	SFATLockGuard::~SFATLockGuard() {
		mMutex.unlock();
	}
#endif

} // namespace SFAT

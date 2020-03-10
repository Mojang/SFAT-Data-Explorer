/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#ifdef _SFAT_USE_MC_CORE_UTILS
#	define SPLIT_FAT_ENABLE_BEDROCK_MUTEX_IMPLEMENTATION	1
#endif

#if (SPLIT_FAT_ENABLE_BEDROCK_MUTEX_IMPLEMENTATION == 1)
#	include "Platform/Threading/Mutex.h"
#else
#	include <mutex>
#	include <thread>
#endif
#include <atomic>

namespace SFAT {


	class SFATMutex	{
	public:

		SFATMutex();

		~SFATMutex();

		void lock();

		void unlock();

		bool tryLock();

		bool isLocked() const;

		bool isSameThread() const;

	private:

#if (SPLIT_FAT_ENABLE_BEDROCK_MUTEX_IMPLEMENTATION == 1)
		Bedrock::Threading::Mutex	mMutex;
#else
		std::mutex mMutex;
#endif
		volatile bool mLocked;
		std::thread::id mThreadId;
	};

#if (SPLIT_FAT_ENABLE_BEDROCK_MUTEX_IMPLEMENTATION == 1)
	using SFATLockGuard = Bedrock::Threading::LockGuard<SFATMutex>;
#else
	class SFATLockGuard {
	public:
		SFATLockGuard(SFATMutex& mutex);
		SFATLockGuard(const SFATLockGuard&) = delete;
		~SFATLockGuard();

	private:
		SFATMutex& mMutex;
	};
#endif

	class SFATRecursiveMutex {
	public:

		SFATRecursiveMutex();

		~SFATRecursiveMutex();

		void lock();

		void unlock();

		bool tryLock();

		bool isLocked() const;

		bool isSameThread() const;

		int32_t getLockCount() const;

	private:

#if (SPLIT_FAT_ENABLE_BEDROCK_MUTEX_IMPLEMENTATION == 1)
		Bedrock::Threading::RecursiveMutex	mMutex;
#else
		std::recursive_mutex mMutex;
#endif
		std::atomic<int32_t> mLockCount;
		std::thread::id mThreadId;
	};

} // namespace SFAT
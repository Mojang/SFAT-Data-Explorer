/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/

#pragma once

#include "Mutex.h"
#include <vector>

namespace SFAT {

	class MemoryBufferPool;

	using ResourceItem = std::vector<uint8_t>;

	class MemoryBufferHandle final {
		friend class MemoryBufferPool;
	public:
		MemoryBufferHandle(MemoryBufferPool& memoryBufferPool, std::unique_ptr<ResourceItem>&& object)
			: mMemoryBufferPool(memoryBufferPool)
			, mObject(std::move(object)) {
		}

		MemoryBufferHandle(const MemoryBufferHandle&) = delete;
		MemoryBufferHandle& operator=(MemoryBufferHandle&) = delete;
		MemoryBufferHandle(MemoryBufferHandle&&) = delete;
		MemoryBufferHandle& operator=(MemoryBufferHandle&&) = delete;

		~MemoryBufferHandle();
		ResourceItem& get() {
			return *mObject;
		}

	private:
		MemoryBufferPool& mMemoryBufferPool;
		std::unique_ptr<ResourceItem> mObject;
	};

	class MemoryBufferPool final {
		friend class MemoryBufferHandle;
	public:
		MemoryBufferPool(size_t startReourceCount, size_t bufferByteSize, size_t recommendedReourceMaxCount)
			: mBufferSize(bufferByteSize)
			, mRecommendedReourceMaxCount(recommendedReourceMaxCount)
			, mTotalCountUsed(0) {
			for (size_t i = 0; i < startReourceCount; ++i) {
				mFreeResourceBlocks.emplace_back(std::make_unique<ResourceItem>(mBufferSize));
			}
		}

		~MemoryBufferPool() = default;

		MemoryBufferPool(const MemoryBufferPool&) = delete;
		MemoryBufferPool& operator=(MemoryBufferPool&) = delete;
		MemoryBufferPool(MemoryBufferPool&&) = delete;
		MemoryBufferPool& operator=(MemoryBufferPool&&) = delete;
		
		std::unique_ptr<MemoryBufferHandle> acquireBuffer() {
			SFATLockGuard guard(mBufferUpdates);

			std::unique_ptr<ResourceItem> resourceItem;
			if (mFreeResourceBlocks.size() > 0) {
				resourceItem = std::move(mFreeResourceBlocks.back());
				mFreeResourceBlocks.pop_back();
			}
			else {
				resourceItem = std::make_unique<ResourceItem>(mBufferSize);
			}
			mTotalCountUsed++;

			return std::make_unique<MemoryBufferHandle>(*this, std::move(resourceItem));
		}

		size_t getCountFree() const {
			return mFreeResourceBlocks.size();
		}

		size_t getCountInUse() const {
			return mTotalCountUsed;
		}

	private:
		void recicleBuffer(MemoryBufferHandle& handle) {
			SFATLockGuard guard(mBufferUpdates);

			--mTotalCountUsed;
			if (mFreeResourceBlocks.size() < mRecommendedReourceMaxCount) {
				mFreeResourceBlocks.push_back(std::move(handle.mObject));
			}
		}

	private:
		std::vector<std::unique_ptr<ResourceItem>> mFreeResourceBlocks;
		const size_t mBufferSize;
		SFATMutex mBufferUpdates;
		const size_t mRecommendedReourceMaxCount;
		volatile int mTotalCountUsed;
	};

	inline MemoryBufferHandle::~MemoryBufferHandle() {
		mMemoryBufferPool.recicleBuffer(*this);
	}

} //namespace SFAT

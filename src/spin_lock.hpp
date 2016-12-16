#ifndef TENGINE_SPIN_LOCK_HPP
#define TENGINE_SPIN_LOCK_HPP

#include "allocator.hpp"

#include <atomic>

namespace tengine 
{
	class SpinLock : public Allocator
	{
	public:
		SpinLock() = default;

		SpinLock(const SpinLock&) = delete;

		SpinLock& operator= (const SpinLock&) = delete;

		void lock() {
			while (lock_.test_and_set(std::memory_order_acquire))
				;
		}
		void unlock() {
			lock_.clear(std::memory_order_release);
		}

	private:
		std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
	};

	class SpinHolder
	{
	public:
		SpinHolder(SpinLock& lock)
			: lock_(lock)
		{
			lock_.lock();
		}

		~SpinHolder()
		{
			lock_.unlock();
		}

	private:
		SpinLock& lock_;
	};
}

#endif //TENGINE_SPIN_LOCK_HPP
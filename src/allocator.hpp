#ifndef TENGINE_MALLOC_HPP
#define TENGINE_MALLOC_HPP

extern "C"
{
#include "ccmalloc.h"
}

#include <array>
#include <memory>

namespace tengine
{
	class Allocator
	{
	public:
		static void *operator new(size_t size)
		{
			return ccmalloc(size);
		}

		static void operator delete(void *ptr)
		{
			ccfree(ptr);
		}
	};

	class HandlerAllocator : public Allocator
	{
	public:
		HandlerAllocator()
			: in_use_(false)
		{
		}

		HandlerAllocator(const HandlerAllocator&) = delete;
		HandlerAllocator& operator=(const HandlerAllocator&) = delete;

		void* allocate(std::size_t size)
		{
			if (!in_use_ && size < sizeof(storage_))
			{
				in_use_ = true;
				return &storage_;
			}
			else
			{
				return ccmalloc(size);
			}
		}

		void deallocate(void* pointer)
		{
			if (pointer == &storage_)
			{
				in_use_ = false;
			}
			else
			{
				ccfree(pointer);
			}
		}

	private:

		typename std::aligned_storage<1024>::type storage_;

		bool in_use_;
	};

	template <typename Handler>
	class CustomAllocHandler
	{
	public:
		CustomAllocHandler(HandlerAllocator& a, Handler h)
			: allocator_(a)
			, handler_(h)
		{
		}

		template <typename ...Args>
		void operator()(Args&&... args)
		{
			handler_(std::forward<Args>(args)...);
		}

		friend void* asio_handler_allocate(std::size_t size,
			CustomAllocHandler<Handler>* this_handler)
		{
			return this_handler->allocator_.allocate(size);
		}

		friend void asio_handler_deallocate(void* pointer, std::size_t /*size*/,
			CustomAllocHandler<Handler>* this_handler)
		{
			this_handler->allocator_.deallocate(pointer);
		}

	private:
		HandlerAllocator& allocator_;
		Handler handler_;
	};

	template <typename Handler>
	inline CustomAllocHandler<Handler> make_custom_alloc_handler(
		HandlerAllocator& a, Handler h)
	{
		return CustomAllocHandler<Handler>(a, h);
	}

}

#endif

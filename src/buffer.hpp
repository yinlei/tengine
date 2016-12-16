#ifndef TENGINE_BUFFER_HPP
#define TENGINE_BUFFER_HPP

#include "allocator.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdint.h>

namespace tengine
{

	class Buffer : public Allocator
	{
	public:
		enum { kHeaderSize = 2 };
		enum { kMaxBodySize = 4098 };

		Buffer()
			: body_size_(0)
		{
		}

		const char* data() const
		{
			return data_;
		}

		char* data()
		{
			return data_;
		}

		uint16_t size() const
		{
			return body_length_ + kHeaderSize;
		}

		const char* body() const
		{
			return data_ + kHeaderSize;
		}

		char* body()
		{
			return data_ + kHeaderSize;
		}

		uint16_t body_size() const
		{
			return body_size_;
		}

		void body_size(uint16_t size)
		{
			body_size_ = size;
			if (body_size_ > kMaxBodySize)
				body_size_ = kMaxBodySize;
		}

		bool decode_header()
		{
			body_size_ = *(uint16_t*)data_;
			if (body_size_ > kMaxBodySize)
			{
				body_size_ = 0;
				return false;
			}

			return true;
		}

		void encode_header()
		{
			uint16_t size = body_size();
			std::memcpy(data_, &size, kHeaderSize);
		}

	private:
		char data_[kHeaderSize + kMaxBodySize];
		uint16_t body_size_;
	};

	typedef std::deque<Buffer> BufferDeque;

}

#endif

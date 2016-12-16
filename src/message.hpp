#ifndef TENGINE_MESSAGE_HPP
#define TENGINE_MESSAGE_HPP

#include "allocator.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

#include <stdint.h>

namespace tengine
{

	struct InternalMessage 
	{
		char *buf;
		size_t size;
		void *sender;
		int session;
	};

	class Message : public Allocator
	{
	public:
		enum { header_length = 2 };
		enum { max_body_length = 4098 };
		enum { reserve_length = 0 };

		Message()
			: body_length_(0)
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

		uint16_t length() const
		{
			return body_length_ + header_length;
		}

		const char* body() const
		{
			return data_ + header_length;
		}

		char* body()
		{
			return data_ + header_length;
		}

		uint16_t body_length() const
		{
			return body_length_;
		}

		void body_length(uint16_t new_length)
		{
			body_length_ = new_length;
			if (body_length_ > max_body_length)
				body_length_ = max_body_length;
		}

		bool decode_header()
		{
			body_length_ = *(short int*)data_;
			if (body_length_ > max_body_length)
			{
				body_length_ = 0;
				return false;
			}

			return true;
		}

		void encode_header()
		{
			uint16_t len = body_length();
			std::memcpy(data_, &len, header_length);
		}

	private:
		char data_[header_length + max_body_length];
		uint16_t body_length_;
	};

	typedef std::deque<Message> MessageDeque;

}

#endif

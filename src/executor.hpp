#ifndef TENGINE_EXECUTOR_HPP
#define TENGINE_EXECUTOR_HPP

#include "allocator.hpp"

#include "asio.hpp"

namespace tengine
{
	class Executor : public Allocator
	{
	public:
		Executor();

		virtual ~Executor();

		asio::io_service& io_service();

		asio::executor executor();

		int run(std::size_t num_threads = 1);

		void join();

		void stop();

	private:

		asio::io_service io_service_;

		asio::io_service::work work_;

		int thread_num_;

		asio::detail::thread_group threads_;
	};

}

#endif

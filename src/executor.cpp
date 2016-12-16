#include "executor.hpp"

#include "asio/ts/executor.hpp"

namespace tengine
{
	namespace
	{
		struct ThreadFunction
		{
			asio::io_service* io_service_;

			void operator()()
			{
				asio::error_code ec;
				io_service_->run(ec);
			}
		};
	}

	Executor::Executor()
		: io_service_()
		, work_(io_service_)
		, thread_num_(0)
		, threads_()
	{
		// ThreadFunction f = { &io_service_ };
		// std::size_t num_threads = asio::detail::thread::hardware_concurrency() * 2;
		// threads_.create_threads(f, num_threads ? num_threads : 2);
	}

	Executor::~Executor()
	{
		stop();
		join();
	}

	int Executor::run(std::size_t num_threads)
	{
		ThreadFunction f = { &io_service_ };
		threads_.create_threads(f, num_threads);

		return 0;
	}

	asio::io_service& Executor::io_service()
	{
		return io_service_;
	}

	asio::executor Executor::executor()
	{
		return std::move(io_service_.get_executor());
	}

	void Executor::join()
	{
		threads_.join();
	}

	void Executor::stop()
	{
		io_service_.stop();
	}

}

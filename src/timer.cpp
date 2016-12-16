#include "timer.hpp"

#include "context.hpp"

#include "dispatch.hpp"

#include <chrono>
#include <thread>
#include <functional>

using namespace std::chrono;

namespace tengine
{
	namespace
	{
		struct TimerFunction
		{
			asio::io_service& io_service_;

			TimerFunction(asio::io_service& io_service)
				: io_service_(io_service)
			{}

			void operator()()
			{
				asio::error_code ec;
				io_service_.run(ec);
			}
		};
	}

	Timer::Timer(Context& context)
		: Service(context)
		, io_service_()
		, work_(io_service_)
		, thread_(TimerFunction(io_service_))
		, timer_events_()
	{

	}

	Timer::~Timer()
	{
		io_service_.stop();
		thread_.join();
	}

	int Timer::init(const char* name)
	{
		Service::init(name);

		char key[256];
		snprintf(key, sizeof(key), "%s.register_name", name);

		const char *register_name = context_.config(key, "Timer");

		context_.register_name(this, register_name);

		return 0;
	}

	uint64_t Timer::now()
	{
		//return high_resolution_clock::now().time_since_epoch().count();

		//typedef duration<int, std::milli> milliseconds_type;
		time_point<system_clock, std::chrono::milliseconds> now =
			std::chrono::time_point_cast<std::chrono::milliseconds,
				system_clock>(system_clock::now());

		return now.time_since_epoch().count();
	}

	uint64_t Timer::micro_now()
	{
		time_point<system_clock, std::chrono::microseconds> now =
			std::chrono::time_point_cast<std::chrono::microseconds,
				system_clock>(system_clock::now());

		return now.time_since_epoch().count();
	}

	uint64_t Timer::nano_now()
	{
		time_point<system_clock, std::chrono::nanoseconds> now =
			std::chrono::time_point_cast<std::chrono::nanoseconds,
				system_clock>(system_clock::now());

		return now.time_since_epoch().count();
	}

	TimerId Timer::add_timer(uint64_t milliseconds, int src, int handler)
	{
		return create_timer(milliseconds, src, handler, true);
	}

	TimerId Timer::add_callback(uint64_t milliseconds, int src, int handler)
	{
		return create_timer(milliseconds, src, handler, false);
	}

	TimerId Timer::create_timer(
		uint64_t milliseconds, int src, int handler, bool recurrent)
	{
		TimerEvent *event = (TimerEvent*)ccmalloc(sizeof(*event));
		uint64_t interval = milliseconds;

		//high_resolution_clock::time_point future =
		//	high_resolution_clock::now() + std::chrono::milliseconds(milliseconds);
		time_point<system_clock, std::chrono::milliseconds> future =
			std::chrono::time_point_cast<std::chrono::milliseconds,
			system_clock>(system_clock::now()) +
				std::chrono::milliseconds(milliseconds);

		uint64_t a = system_clock::now().time_since_epoch().count();
		event->delivery_time = future.time_since_epoch().count();
		event->interval_time = recurrent ? milliseconds : 0;
		event->state = (int)TimerEvent::STATE_PENDING;
		event->src = src;
		event->handler = handler;
		event->timer = new asio::steady_timer(io_service_);

		event->timer->expires_from_now(std::chrono::milliseconds(milliseconds));
		event->timer->async_wait(std::bind(&Timer::on_timer, this, event, std::placeholders::_1));

		asio::post(io_service_.get_executor(),
			[=]
			{
				timer_events_.insert(event);
			});

		return event;
	}

	void Timer::destory_timer(TimerEvent* event)
	{
		if (event != nullptr)
		{
			timer_events_.erase(event);
			delete event->timer;
			ccfree(event);
		}
	}

	void Timer::on_timer(TimerEvent* event, const asio::error_code& ec)
	{
		if (!ec)
		{
			dispatch<MessageType::kMessageTimer, SandBox>(this, event->src, (void*)event, event->handler);

			if (event->interval_time > 0)
			{
				event->timer->expires_from_now(std::chrono::milliseconds(event->interval_time));
				event->timer->async_wait(std::bind(&Timer::on_timer, this, event, std::placeholders::_1));
			}
			else
			{
				destory_timer(event);
			}
		}
		else
		{
			destory_timer(event);
		}

	}

	void Timer::cancel(TimerId id)
	{
		asio::post(io_service_.get_executor(),
			[=]
			{
				TimerEvent *event = (TimerEvent*)id;
				TimerEventHolder::iterator iter = timer_events_.find(event);
				if (iter != timer_events_.end())
				{
					//event->state = (int)TimerEvent::STATE_CANCELLED;
					event->timer->cancel();
				}
			});
	}

}

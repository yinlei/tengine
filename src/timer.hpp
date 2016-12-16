#ifndef TENGINE_TIMER_HPP
#define TENGINE_TIMER_HPP

#include "asio.hpp"
#include "asio/steady_timer.hpp"

#include "service.hpp"

#include <set>
#include <thread>

#include <stdint.h>

namespace tengine
{
	typedef void* TimerId;

	class Timer : public Service
	{
	public:
		static constexpr int TIMER_KEY = 0;

		Timer(Context& context);

		virtual ~Timer();

		virtual int init(const char* name);

		TimerId add_timer(uint64_t milliseconds, int src, int handler);

		TimerId add_callback(uint64_t milliseconds, int src, int handler);

		void cancel(TimerId id);

		static uint64_t now();

		static uint64_t micro_now();

		static uint64_t nano_now();

		struct TimerEvent
		{
			uint64_t delivery_time;
			uint64_t interval_time;
			int state;
			int src;
			int handler;
			asio::steady_timer *timer;

			enum
			{
				STATE_PENDING = 0,
				STATE_EXECUTING = 1,
				STATE_CANCELLED = 2,
			};
		};

	private:

		typedef std::set<TimerEvent*> TimerEventHolder;

		TimerId create_timer(uint64_t microseconds, int src, int session, bool recurrent);

		void destory_timer(TimerEvent* event);

		void on_timer(TimerEvent* event, const asio::error_code& ec);

		asio::io_service io_service_;

		asio::io_service::work work_;

		std::thread thread_;

		TimerEventHolder timer_events_;
	};
}



#endif

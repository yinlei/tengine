#ifndef TENGINE_WATCHDOG_HPP
#define TENGINE_WATCHDOG_HPP

#include "service.hpp"

#include "asio.hpp"

#include <map>

#include <experimental/filesystem>
#ifdef _WIN32
namespace fs = std::tr2::sys;
#else
namespace fs = std::experimental::filesystem;
#endif

namespace tengine
{
	class Executor;

	class Watcher;

    class WatchDog : public Service
    {
		friend class Watcher;
	public:
		static constexpr int WATCHDOG_KEY = 0;

		WatchDog(Context& context);

		virtual ~WatchDog();

		virtual int init(const char* name);

		void* watch(ServiceAddress src, const fs::path &path);

		void unwatch(const fs::path &path);

		static void touch(const fs::path &path,
			fs::file_time_type time = fs::file_time_type::clock::now());

	private:

		void do_watch();

		Executor *executor_;

		asio::steady_timer timer_;

		std::map<std::string, Watcher*> file_watchers_;
	};
}

#endif // !TENGINE_WATCHDOG_HPP

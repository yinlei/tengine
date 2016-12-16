#ifndef TENGINE_CONTEXT_H
#define TENGINE_CONTEXT_H

#include "spin_lock.hpp"

#include "allocator.hpp"

#include "asio.hpp"

#include <unordered_map>
#include <deque>

struct lua_State;

namespace tengine
{
	class Executor;
	class Service;
	class SandBox;

	class Context : public Allocator
	{
	public:
		Context();

		Context(std::size_t num_threads);

		Context(const Context&) = delete;

		Context& operator=(const Context&) = delete;

		~Context();

		asio::io_service& io_service();

		asio::executor executor();

		int start(const char *config_file);

		void join();

		void stop();

		SandBox *LaunchSandBox(const char *name);

		int register_service(Service *service);

		int register_name(Service *s, const char *name);

		Service *query(int id);

		Service *query(const char *name);

		const char* ConfigString(const char* key);

		const char* ConfigString(const char* key, const char* opt);

		template<class T>
		T config(const char* key, T value);

		Executor &net_executor() { return *net_executor_; }

	private:

		asio::io_service io_service_;

		asio::io_service::work work_;

		int thread_num_;

		asio::detail::thread_group threads_;

		asio::strand<asio::executor> executor_;

		asio::signal_set signals_;

		typedef std::deque<Service*> ServicePool;
		ServicePool services_;

		typedef std::unordered_map<std::string, int> ServiceNameMap;
		ServiceNameMap name_services_;

		lua_State *config_;

		SpinLock service_lock_;

		SpinLock conf_lock_;

		Executor *net_executor_;
	};

	template<class T>
	T Context::config(const char* key, T value)
	{
		const char* result = ConfigString(key);

		if (result != NULL)
			return result;
		else
			return value;
	}

	template<>
	inline int Context::config(const char* key, int value)
	{
		const char* result = ConfigString(key);

		if (result != NULL)
			return ::atoi(result);
		else
			return value;
	}

	template<>
	inline double Context::config(const char* key, double value)
	{
		const char* result = ConfigString(key);

		if (result != NULL)
			return ::atof(result);
		else
			return value;
	}

}


#endif

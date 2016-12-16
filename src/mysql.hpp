#ifndef TENGINE_MYSQL_HPP
#define TENGINE_MYSQL_HPP

#include "service_proxy.hpp"

#include "asio.hpp"

#include "mysql.h"

#include <string>
#include <set>
#include <list>
#include <mutex>
#include <condition_variable>

namespace tengine
{
	class Service;

	class Executor;

	class MySql : public ServiceProxy
	{
	public:
		static constexpr int MYSQL_KEY = 0;

		MySql(Service *s);

		~MySql();

		int start(const char *conf);

		typedef std::function<void(MySql*, MYSQL*, const char*)> Handler;

		void query(const char *sql, std::size_t size, Handler handler);

		MYSQL* get();

		void put(MYSQL* mysql);

	private:

		Executor *executor_;

		std::mutex mysql_mutex_;

		std::set<MYSQL*> mysql_used_;

		std::list<MYSQL*> mysql_free_;

		std::condition_variable mysql_cond_;

	};
}

#endif

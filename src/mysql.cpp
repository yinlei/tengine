#include "mysql.hpp"

#include "context.hpp"
#include "executor.hpp"
#include "service.hpp"
#include "message.hpp"

#include "asio/ts/executor.hpp"

namespace tengine
{
	constexpr int MySql::MYSQL_KEY;

	MySql::MySql(Service* s)
		: ServiceProxy(s)
		, executor_(new Executor())
		, mysql_mutex_()
		, mysql_used_()
		, mysql_free_()
		, mysql_cond_()
	{

	}

	MySql::~MySql()
	{
		if (this->executor_ != nullptr)
		{
			delete executor_;
			executor_ = nullptr;
		}

		for (auto mysql : mysql_free_)
		{
			mysql_close(mysql);
		}
		mysql_free_.clear();

		for (auto mysql : mysql_used_)
		{
			mysql_close(mysql);
		}
		mysql_used_.clear();
	}

	int MySql::start(const char *conf)
	{

		char key[256];
		snprintf(key, sizeof(key), "%s.host", conf);

		std::string host = host_->context().config(key, "");
		if (host.empty())
			return 1;

		snprintf(key, sizeof(key), "%s.user", conf);
		std::string user = host_->context().config(key, "");
		if (user.empty())
			return 1;

		snprintf(key, sizeof(key), "%s.password", conf);
		std::string password = host_->context().config(key, "");
		if (password.empty())
			return 1;

		snprintf(key, sizeof(key), "%s.db", conf);
		std::string db = host_->context().config(key, "");
		if (db.empty())
			return 1;

		snprintf(key, sizeof(key), "%s.port", conf);
		int port = host_->context().config(key, 3306);

		snprintf(key, sizeof(key), "%s.connection_pool", conf);

		int connection_num = host_->context().config(key, 2);

		for (int i = 0; i < connection_num; i++)
		{
			MYSQL *mysql = mysql_init(NULL);

			if (!mysql)
				return 1;

			char timeout = 10;
			mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

			if (0 != mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8"))
			{
				mysql_error(mysql);
				return 1;
			}

			if (mysql_real_connect(mysql, host.c_str(), user.c_str(),
				password.c_str(), db.c_str(), port, NULL, 0) == NULL)
			{
				mysql_error(mysql);
				return 1;
			}
			else
			{
				const char sql[] = "set interactive_timeout=24*3600";
				int ret = mysql_real_query(
					mysql, sql, (unsigned long)sizeof(sql));
				if (ret != 0)
				{
					mysql_error(mysql);
					return 1;
				}
			}

			mysql_free_.push_back(mysql);
		}

		snprintf(key, sizeof(key), "%s.thread_num", conf);

		int thread_num = host_->context().config(key, 1);

		this->executor_->run(thread_num);

		return 0;
	}

	MYSQL* MySql::get()
	{
		std::unique_lock<std::mutex> lock(mysql_mutex_);

		while (mysql_free_.empty())
			mysql_cond_.wait(lock);

		auto conn = mysql_free_.front();
		mysql_used_.insert(conn);
		mysql_free_.pop_front();

		return conn;
	}

	void MySql::put(MYSQL* mysql)
	{
		std::unique_lock<std::mutex> lock(mysql_mutex_);

		mysql_used_.erase(mysql);
		mysql_free_.push_back(mysql);

		mysql_cond_.notify_one();
	}

	void MySql::query(const char *sql, std::size_t size, Handler handler)
	{
		asio::post(executor_->executor(),
			[=]
		{
			MYSQL *mysql = get();

			int ret = mysql_real_query(mysql, sql, size);

			if (ret != 0)
			{
				put(mysql);

				handler(this, nullptr, mysql_error(mysql));
				return;
			}

			asio::post(host_->executor(),
				[=]
			{
				handler(this, mysql, nullptr);

				this->put(mysql);
			});

		});
	}

}

#include "logger.hpp"

#include "context.hpp"
#include "executor.hpp"
#include "timer.hpp"
#include "dispatch.hpp"

#include "colorprintf/colorprintf.h"

#include <thread>
#include <chrono>

#include <stdio.h>
#include <stdlib.h>

namespace tengine
{
	struct logger
	{
		FILE *file;
		int close;
	};

	Logger::Logger(Context& context)
		: Service(context)
		, executor_(new Executor())
		, logger_(NULL)
        , loggers_()
	{

	}

	Logger::~Logger()
	{
		delete executor_;
		executor_ = nullptr;

		if (logger_)
		{
			fclose(logger_->file);
			ccfree(logger_);
			logger_ = 0;
		}

	}

	int Logger::init(const char* name)
	{
		Service::init(name);

		char key[256];

		snprintf(key, sizeof(key), "%s.log_file_enable", name);

		if (context_.config(key, 0) > 0)
		{
			snprintf(key, sizeof(key), "%s.log_file_name", name);
			const char *file_name = context_.config(key, "");
			if (strlen(file_name) > 0)
			{
				logger_ = (logger*)ccmalloc(sizeof(*logger_));
				if (logger_ != NULL)
				{
					logger_->file = fopen(file_name, "ab");
					if (logger_->file)
					{
						time_t now = ::time(NULL);
						fprintf(logger_->file, "open time: %s", ctime(&now));
						fflush(logger_->file);
						logger_->close = 1;
					}

				}
			}
		}

		context_.register_name(this, "Logger");

		executor_->run();

		return 0;
	}

	void Logger::log(const char *msg, ServiceAddress sender)
	{
		dispatch<MessageType::kMessageLogger, Logger>(sender, this->id(), msg);
	}

	void Logger::log(const std::string log, ServiceAddress sender)
	{
		dispatch<MessageType::kMessageLogger, Logger>(sender, this->id(), log);
	}

	void Logger::log(int level, const std::string &msg, ServiceAddress sender)
	{
		asio::post(executor(),
			[=]
		{
			asio::post(executor_->executor(),
				[=]
			{
				this->do_log(level, msg.c_str(), msg.size(), sender->name());
			});
		});
	}

	int Logger::async_log(ServiceAddress src, const char *msg)
	{
		asio::post(executor_->executor(),
			[=]
		{
			this->do_log(-1, msg, std::strlen(msg), src->name());
		});

		return 0;
	}

	int Logger::async_log(ServiceAddress src, const std::string msg)
	{
		/*
		fprintf(logger_->file, "[%s] ", src->Name());
		fwrite(msg.c_str(), msg.size(), 1, logger_->file);
		fprintf(logger_->file, "\n");
		fflush(logger_->file);

		return 0;
		*/

		asio::post(executor_->executor(),
			[=]
		{
			this->do_log(-1, msg.c_str(), msg.size(), src->name());
		});

		return 0;
	}

	int Logger::async_log(
		ServiceAddress src, int level, const char *msg, int size)
	{
		asio::post(executor_->executor(),
			[=]
		{
			this->do_log(level, msg, size, src->name());
		});

		return 0;
	}

	int Logger::do_log(int level, const char *msg, int size, const char *name)
	{
		if (logger_ && logger_->file)
		{
			fprintf(logger_->file, "[%s] ", name);
			fwrite(msg, size, 1, logger_->file);
			fprintf(logger_->file, "\n");
			fflush(logger_->file);
		}

		colorprintf(level, "[%s] %s\n", name, msg);
		return 0;
	}

}

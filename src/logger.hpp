#ifndef TENGINE_LOGGER_HPP
#define TENGINE_LOGGER_HPP

#include "service.hpp"
#include "context.hpp"

#include <map>

namespace tengine
{
	class Executor;

	struct logger;

	class Logger : public Service
	{
	public:
		Logger(Context& context);

		virtual ~Logger();

		virtual int init(const char* name);

		void log(const char *msg, ServiceAddress sender);

		void log(const std::string msg, ServiceAddress sender);

		void log(int level, const std::string &msg, ServiceAddress sender);

		template<int MessageType, class... Args>
		void handler(MessageTypeTrait<MessageType>, int src, Args... args);

	private:
		int async_log(ServiceAddress src, const char *msg);

		int async_log(ServiceAddress src, const std::string msg);

		int async_log(ServiceAddress src, int level, const char *msg, int size);

		int do_log(int level, const char *msg, int size, const char *name);

		Executor *executor_;

		logger *logger_;

        std::map<std::string, logger*> loggers_;

		int console_enable_;

		int file_enable_;

	};

	template<>
	inline void Logger::handler(MessageTypeTrait<MessageType::kMessageLogger>,
		int src, const char *msg)
	{
		Service* service = context_.query(src);
		if (!service)
			return;

		async_log(service, msg);
	}

	template<>
	inline void Logger::handler(MessageTypeTrait<MessageType::kMessageLogger>,
		int src, const std::string msg)
	{
		Service* service = context_.query(src);
		if (!service)
			return;

		async_log(service, msg);
	}

	template<>
	inline void Logger::handler(MessageTypeTrait<MessageType::kMessageLogger>,
		int src, int level, const char *msg, int size)
	{
		Service* service = context_.query(src);
		if (!service)
			return;

		async_log(service, level, msg, size);
	}

}


#endif

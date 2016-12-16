#ifndef TENGINE_REDIS_HPP
#define TENGINE_REDIS_HPP

#include "asio.hpp"

#include "service_proxy.hpp"

#include "hiredis/hiredis.h"

#include <string>
#include <set>
#include <list>
#include <mutex>
#include <condition_variable>

namespace tengine
{
	class Service;

	class Executor;

	class Redis : public ServiceProxy
	{
	public:
		static constexpr int REDIS_KEY = 0;

		enum
		{
			kRedisCommandMaxArgs= 256,
            kRedisPipelineMaxSize = 64,
		};

        typedef std::array<std::string, kRedisCommandMaxArgs> RedisCommandType;
		typedef std::array<RedisCommandType, kRedisPipelineMaxSize> RedisCommandQueue;

		Redis(Service *s);

		~Redis();

		int start(const char *conf);

		redisContext* get();

		void put(redisContext* redis);

		typedef std::function<void(redisContext*, redisReply*)> Handler;
		typedef std::function<void(redisContext*, redisReply**, std::size_t)> PipelineHander;

		void call(const char *c, std::size_t size, Handler handler);
		void call(const char** argv, std::size_t* argvlen, std::size_t nargs, Handler handler);

		void pipeline(const RedisCommandQueue& queue, std::size_t nargs, Handler handler);

		// 下面的只有配合cortione才能保证线程安全的
        void transaction();
        void pipeline(const char** argv, std::size_t* argvlen, std::size_t nargs);
		void commit(PipelineHander handler);

	private:

		Executor *executor_;

		std::mutex redis_mutex_;

		std::set<redisContext*> redis_used_;

		std::list<redisContext*> redis_free_;

		std::condition_variable redis_cond_;

        std::array<RedisCommandType, kRedisPipelineMaxSize> pipeline_;
		std::size_t pipeline_index_;
		std::size_t pipeline_args_[kRedisPipelineMaxSize];
	};
}

#endif

#include "redis.hpp"

#include "context.hpp"
#include "executor.hpp"
#include "service.hpp"
#include "message.hpp"

#include "asio/ts/executor.hpp"

namespace tengine
{
	constexpr int Redis::REDIS_KEY;

	Redis::Redis(Service* s)
		: ServiceProxy(s)
		, executor_(new Executor())
		, redis_mutex_()
		, redis_used_()
		, redis_free_()
		, redis_cond_()
		, pipeline_()
		, pipeline_index_(0)
	{

	}

	Redis::~Redis()
	{
		if (this->executor_ != nullptr)
		{
			delete executor_;
			executor_ = nullptr;
		}

		for (auto redis : redis_free_)
		{
			redisFree(redis);
		}

		redis_free_.clear();

		for (auto redis : redis_used_)
		{
			redisFree(redis);
		}

		redis_used_.clear();
	}

	int Redis::start(const char *conf)
	{
		char key[256];
		snprintf(key, sizeof(key), "%s.host", conf);

		std::string host = host_->context().config(key, "localhost");
		if (host.empty())
			return 1;

		snprintf(key, sizeof(key), "%s.port", conf);
		int port = host_->context().config(key, 6379);

		snprintf(key, sizeof(key), "%s.pool", conf);

		int pool = host_->context().config(key, 2);

		snprintf(key, sizeof(key), "%s.timeout", conf);

		int time = host_->context().config(key, 1000000);

		struct timeval timeout = { 1, time };

		for (int i = 0; i < pool; i++)
		{
			redisContext *redis = redisConnectWithTimeout(host.c_str(), port, timeout);

			if (!redis || redis->err)
			{
				if (redis)
					redisFree(redis);

				return 1;
			}

			redis_free_.push_back(redis);
		}

		snprintf(key, sizeof(key), "%s.thread_num", conf);

		int thread_num = host_->context().config(key, 1);

		this->executor_->run(thread_num);

		return 0;
	}

	redisContext* Redis::get()
	{
		std::unique_lock<std::mutex> lock(redis_mutex_);

		while (redis_free_.empty())
			redis_cond_.wait(lock);

		auto conn = redis_free_.front();
		redis_used_.insert(conn);
		redis_free_.pop_front();

		return conn;
	}

	void Redis::put(redisContext* mysql)
	{
		std::unique_lock<std::mutex> lock(redis_mutex_);

		redis_used_.erase(mysql);
		redis_free_.push_back(mysql);

		redis_cond_.notify_one();
	}

	void Redis::call(const char *c, std::size_t size, Handler handler)
	{
		std::string command = { c, size };
		asio::post(executor_->executor(),
			[=]
		{
			redisContext *redis = get();

			redisReply *reply = (redisReply*)redisCommand(redis, command.c_str());

			asio::post(host_->executor(),
				[=]
			{
				handler(redis, reply);

				freeReplyObject(reply);

				this->put(redis);
			});

		});
	}

	void Redis::call(const char** argv, std::size_t* argvlen, std::size_t nargs, Handler handler)
	{
		std::array<std::string, kRedisCommandMaxArgs> args;
		for (std::size_t i = 0; i < nargs; i++)
		{
			args[i] = { argv[i], argvlen[i] };
		}

		asio::post(executor_->executor(),
			[=]
		{
			redisContext *redis = get();

			const char* argv[Redis::kRedisCommandMaxArgs];
			std::size_t argvlen[Redis::kRedisCommandMaxArgs];

			for (std::size_t i = 0; i < nargs; i++)
			{
				argv[i] = args[i].c_str();
				argvlen[i] = args[i].size();
			}

			redisReply *reply = (redisReply*)redisCommandArgv(redis, nargs, argv, argvlen);

			asio::post(host_->executor(),
				[=]
			{
				handler(redis, reply);

				freeReplyObject(reply);

				this->put(redis);
			});

		});
	}

	void Redis::pipeline(const RedisCommandQueue& queue, std::size_t nargs, Handler handler)
	{
		asio::post(executor_->executor(),
			[=]
		{
			redisContext *redis = get();

			/*
			for (std::size_t i = 0; i < nargs; i++)
			{
				const char* argv[Redis::kRedisCommandMaxArgs];
				std::size_t argvlen[Redis::kRedisCommandMaxArgs];

				for (int j = 0; j < nargs; j++)
				{
					argv[j] = queue[i].c_str();
					argvlen[j] = queue[i].size();
				}

				redisAppendCommandArgv(redis, )
			}

			

			redisReply *reply = (redisReply*)redisCommandArgv(redis, nargs, argv, argvlen);
			
			asio::post(host_->executor(),
				[=]
			{
				handler(redis, reply);

				freeReplyObject(reply);

				this->put(redis);
			});
			*/
		});
	}


    void Redis::transaction()
    {
		pipeline_index_ = 0;
    }

    void Redis::pipeline(const char** argv, std::size_t* argvlen, std::size_t nargs)
    {
		RedisCommandType& args = pipeline_[pipeline_index_];

		for (std::size_t i = 0; i < nargs; i++)
		{
			args[i] = { argv[i], argvlen[i] };
		}

		pipeline_args_[pipeline_index_] = nargs;
		pipeline_index_++;
    }

    void Redis::commit(PipelineHander handler)
    {
        asio::post(executor_->executor(),
        [=]
        {
            redisContext *redis = get();

			for (std::size_t i = 0; i < pipeline_index_; i++)
			{
				std::size_t nargs = pipeline_args_[i];
				const char* argv[Redis::kRedisCommandMaxArgs];
				std::size_t argvlen[Redis::kRedisCommandMaxArgs];

				for (std::size_t j = 0; j < nargs; j++)
				{
					argv[j] = pipeline_[i][j].c_str();
					argvlen[j] = pipeline_[i][j].size();
				}

				redisAppendCommandArgv(redis, nargs, argv, argvlen);
			}

			std::array<redisReply*, kRedisPipelineMaxSize> rets;

			for (std::size_t i = 0; i < pipeline_index_; ++i)
			{
				redisGetReply(redis, reinterpret_cast<void**>(&(rets[i])));
			}

            asio::post(host_->executor(),
				[=]
				{
					handler(redis, const_cast<redisReply**>(rets.data()), pipeline_index_);
									
					for (std::size_t i = 0; i < pipeline_index_; ++i)
					{
						freeReplyObject(rets[i]);
					}

					this->put(redis);
				});

        });

    }


}

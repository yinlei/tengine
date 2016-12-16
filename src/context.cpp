#include "context.hpp"

#include "logger.hpp"
#include "timer.hpp"
#include "node.hpp"
#include "watchdog.hpp"
#include "network.hpp"
#include "sandbox.hpp"
#include "executor.hpp"

#include "asio/ts/executor.hpp"

#ifdef LUA_JIT
#include "c-api/compat-5.3.h"
#else
#include "lua.hpp"
#include "lualib.h"
#endif // LUA_JIT

namespace tengine
{
	namespace
	{
		struct ThreadFunction
		{
			asio::io_service* io_service_;

			void operator()()
			{
				asio::error_code ec;
				io_service_->run(ec);
			}
		};
	}

	Context::Context()
		: Context(0)
	{

	}

	Context::Context(std::size_t num_threads)
		: io_service_()
		, work_(io_service_)
		, thread_num_(num_threads)
		, threads_()
		, executor_(this->executor())
		, signals_(io_service_)
		, services_()
		, config_(0)
		, service_lock_()
		, conf_lock_()
		, net_executor_(nullptr)
	{
		signals_.add(SIGINT);
		signals_.add(SIGTERM);
#if defined(SIGQUIT)
		signals_.add(SIGQUIT);
#endif // defined(SIGQUIT)

		signals_.async_wait(
			[this](std::error_code ec, int signo)
			{
				this->stop();
			}
		);
	}

	Context::~Context()
	{
		stop();

		join();

		if (net_executor_ != nullptr)
		{
			delete net_executor_;
			net_executor_ = nullptr;
		}

		for (std::size_t i = 0; i < services_.size(); i++) {
			delete services_[i];
		}

		services_.clear();

		if (config_)
			lua_close(config_);

		config_ = NULL;
	}

	int Context::start(const char *config_file)
	{
		if (!config_file)
			return -1;

		struct lua_State *L = lua_newstate(cclalloc, NULL); //luaL_newstate(); //
		if (!L)
			return -1;

		config_ = L;

		//luaL_openlibs(L);

		int err = luaL_dofile(L, config_file);
		if (err != LUA_OK)
		{
			fprintf(stderr, "%s\n", lua_tostring(L, -1));
			return -1;
		}

		if (thread_num_ <= 0) {
			lua_getglobal(L, "thread_num");
			thread_num_ = (int)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}

		if (thread_num_ <= 0)
			thread_num_ = asio::detail::thread::hardware_concurrency() * 2;

		ThreadFunction f = { &io_service_ };
		threads_.create_threads(f, thread_num_ ? thread_num_ : 2);

		net_executor_ = new Executor();
		net_executor_->run();

		Logger *logger = new Logger(*this);
		if (logger == nullptr)
			return -1;

		if (logger->init("logger") != 0)
		{
			delete logger;
			return -1;
		}

		Timer *timer = new Timer(*this);
		if (timer == nullptr)
			return -1;

		if (timer->init("timer") != 0)
		{
			delete timer;
			return -1;
		}

		WatchDog *watchdog = new WatchDog(*this);
		if (watchdog == nullptr)
			return -1;

		if (watchdog->init("watchdog") != 0)
		{
			delete watchdog;
			return -1;
		}

		/*
		Node *node = new Node(*this);
		if (node == nullptr)
			return -1;

		if (node->init("node") != 0)
		{
			delete node;
			return -1;
		}
		*/
		Network *network = new Network(*this);
		if (network == nullptr)
			return -1;

		if (network->init("network") != 0)
		{
			delete network;
			return -1;
		}

		SandBox *sand_box = new SandBox(*this);
		if (sand_box == nullptr)
			return -1;

		const char *boot = this->config("boot", "launcher");

		if (sand_box->init(boot) != 0)
		{
			delete sand_box;
			return -1;
		}

		return 0;
	}

	asio::io_service& Context::io_service()
	{
		return io_service_;
	}

	asio::executor Context::executor()
	{
		return std::move(io_service_.get_executor());
	}

	void Context::join()
	{
		threads_.join();
	}

	void Context::stop()
	{
		io_service_.stop();

		if (net_executor_)
			net_executor_->stop();
	}

	SandBox *Context::LaunchSandBox(const char *name)
	{
		SandBox *sand_box = new SandBox(*this);
		if (sand_box == nullptr)
			return (SandBox *)NULL;

		if (sand_box->init(name) != 0)
		{
			delete sand_box;
			return (SandBox *)NULL;
		}

		this->register_name(sand_box, name);

		return sand_box;
	}

	int Context::register_service(Service *service)
	{
		if (service == nullptr)
			return 0;

		SpinHolder holder(service_lock_);
		for (size_t i = 0; i < services_.size(); i++)
		{
			if (service == services_[i])
				return (i + 1);
		}

		services_.push_back(service);
		return services_.size();
	}

	int Context::register_name(Service *s, const char *name)
	{
		if (s == nullptr || !name)
			return 0;

		SpinHolder holder(service_lock_);

		ServiceNameMap::iterator iter = name_services_.find(name);
		if (iter != name_services_.end())
			return 2;

		if (s->id() <= 0)
			return 3;

		name_services_[name] = s->id();
		return 0;
	}

	Service *Context::query(int id)
	{
		SpinHolder holder(service_lock_);
		if (id <= 0 || id > (int)services_.size())
			return nullptr;
		return services_[id - 1];
	}

	Service *Context::query(const char *name)
	{
		if (!name)
			return nullptr;

		SpinHolder holder(service_lock_);
		ServiceNameMap::iterator iter = name_services_.find(name);
		if (iter != name_services_.end())
		{
			return services_[iter->second - 1];
		}

		return nullptr;
	}

	const char* Context::ConfigString(const char* key)
	{
		char buff[512] = { 0 };

		::memcpy(buff, key, ::strlen(key));

		std::deque<std::string> keys;

		char *token;

		token = ::strtok(buff, ".");

		while (token)
		{
			keys.push_back(token);
			token = ::strtok(NULL, ".");
		}

		SpinHolder holder(conf_lock_);
		lua_State *L = config_;

		std::string k = keys.front();

		keys.pop_front();

		lua_getglobal(L, k.c_str());

		while (!keys.empty() && lua_istable(L, -1))
		{
			k = keys.front();

			lua_getfield(L, -1, k.c_str());

			keys.pop_front();
		}

		const char* result = lua_tostring(L, -1);
		lua_pop(L, 1);

		return result;
	}

	const char* Context::ConfigString(const char* key, const char* opt)
	{
		const char* result = ConfigString(key);

		if (result != NULL)
			return result;
		else
			return opt;
	}
}



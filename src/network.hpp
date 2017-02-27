#ifndef TENGINE_NETWORK_HPP
#define TENGINE_NETWORK_HPP

#include "service.hpp"
#include "service_proxy.hpp"
#include "spin_lock.hpp"

#include <thread>
#include <map>

namespace tengine
{
	class Executor;

	class Network : public Service
	{
	public:
		Network(Context& context);

		virtual ~Network();

		virtual int init(const char* name);

		const std::string sync_request(const char *type, const char *url,
			const char *path, const char* content);

		typedef std::function<void(const std::string)> Handler;

		void async_request(const char *type, const char *url, const char *path,
			const char* content, Handler handler);

	private:

		const std::string do_request(const std::string& type,
			const std::string& url, const std::string& path,
			const std::string& content);

		Executor *executor_;
	};


	class WebServer : public ServiceProxy
	{
	public:
		static constexpr int WEBSERVER_KEY = 0;

		WebServer(Service *s);

		~WebServer();

		typedef std::function<int (void*, const char*,
			const char*, const char*)> Handler;

		int start(uint16_t port, Handler handler);

		void response(void* response, const char* res, std::size_t size);

	private:
		uint16_t port_;

		void *server_;

		std::thread *worker_;
	};

	class WebSocket : public ServiceProxy
	{
	public:
		static constexpr int WEBSOCKET_KEY = 0;

		WebSocket(Service *s, uint16_t port);

		~WebSocket();

		int start(const std::string& path);

		void send(int session, const char *data, size_t len);

	private:
		uint16_t port_;

		void *server_;

		std::thread *worker_;

		SpinLock session_lock_;

		std::map<void*, void*> connections_;
	};

}

#endif

#ifndef TENGINE_NETWORK_HPP
#define TENGINE_NETWORK_HPP

#include "service.hpp"
#include "service_proxy.hpp"
#include "spin_lock.hpp"

#include "http_client.hpp"
#include "http_server.hpp"
#include "ws_server.hpp"

#include <thread>
#include <map>
#include <unordered_set>

namespace tengine
{
	class Executor;

	class HttpClient : public Service
	{
	public:
		HttpClient(Context& context);

		virtual ~HttpClient();

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


	class HttpServer : public ServiceProxy
	{
	public:
		static constexpr int HTTPSERVER_KEY = 0;

		HttpServer(Service *s);

		~HttpServer();

		typedef std::function<int (void*, const char*,
			const char*, const char*)> Handler;

		int start(uint16_t port, Handler handler);

		void response(void* response, const char* res, std::size_t size, bool remove = false);

	private:
		uint16_t port_;

		SimpleWeb::HttpServer *server_;

		std::thread *worker_;

		std::map<void*, std::shared_ptr<SimpleWeb::HttpServer::Response>> responses_;

		std::mutex mutex_;
	};

	class WebServer : public ServiceProxy
	{
	public:
		static constexpr int WEBSERVER_KEY = 0;

		WebServer(Service *s, uint16_t port);

		~WebServer();

		int start(const std::string& path);

		void send(int session, const char *data, size_t len);

	private:
		uint16_t port_;

		SimpleWeb::WebSocketServer *server_;

		std::thread *worker_;

		SpinLock session_lock_;

		std::map<void*, std::shared_ptr<SimpleWeb::HttpServer::Response>> connections_;
	};

}

#endif
